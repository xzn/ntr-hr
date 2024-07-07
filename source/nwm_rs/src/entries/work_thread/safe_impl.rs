use super::*;

pub fn send_frame(t: &ThreadId, vars: ThreadVars) -> Option<()> {
    let v = match vars.work_begin_acquire() {
        Ok(mut v) => loop {
            let format_changed = bctx_init(&v);

            v.dma_sync();
            let is_top = v.v().is_top();

            let timing = unsafe { svcGetSystemTick() as u32_ };
            unsafe {
                let last_timing = crate::entries::thread_screen::get_last_frame_timing(is_top);
                if timing - last_timing >= SYSCLOCK_ARM11 {
                    crate::entries::thread_screen::set_no_skip_frame(is_top);
                }
            }

            let skip_frame = !unsafe { crate::entries::thread_screen::reset_no_skip_frame(is_top) }
                && !format_changed
                && !v.frame_changed();

            if !skip_frame {
                if !ready_nwm(&t, &v) {
                    return None;
                }

                v.ready_next();
                if !ready_work(&v) {
                    return None;
                }

                unsafe {
                    crate::entries::thread_screen::set_last_frame_timing(is_top, timing);
                }

                break v.release_and_capture_screen(&t);
            }

            if let Some(_) = v.release_skip_frame(&t) {
                v.v_mut().read_is_top();
            } else {
                return None;
            }
        },
        Err(v) => v.acquire(&t)?,
    };

    if reset_threads() {
        return None;
    }

    if !do_send_frame(&t, &v) {
        return None;
    }

    v.release();
    Some(())
}

fn ready_nwm(_t: &ThreadId, v: &ThreadBeginVars) -> bool {
    unsafe {
        let w = v.v().work_index();

        if !crate::entries::thread_nwm::wait_nwm_done(&w) {
            return false;
        }

        for j in ThreadId::up_to(&get_core_count_in_use()) {
            let ninfo = v.nwm_infos().get_mut(&j);
            let info = &mut ninfo.info;
            let buf = ninfo.buf;
            info.send_pos = buf;
            *info.pos.as_ptr() = buf;
            *info.flag.as_ptr() = 0;
        }

        crate::entries::thread_nwm::release_nwm_ready(&w);

        let hdr = v.data_buf_hdr();
        let ctx = v.ctx();
        *hdr.get_unchecked_mut(0) = ctx.frame_id;
        *hdr.get_unchecked_mut(1) = ctx.is_top as u8_;
        *hdr.get_unchecked_mut(2) = 2;
        *hdr.get_unchecked_mut(3) = 0;

        true
    }
}

fn ready_work(v: &ThreadBeginVars) -> bool {
    unsafe {
        let ctx = v.ctx();
        let w = v.v().work_index();

        let mut work_index = w;
        work_index.prev_wrapped();

        let core_count = get_core_count_in_use();
        let core_count_rest = core_count.get() - 1;
        let thread_id_last = ThreadId::init_unchecked(core_count_rest);

        let l = v.load_and_progresses().get_mut(&w);
        for j in ThreadId::up_to(&core_count) {
            l.p.get_mut(&j).store(0, Ordering::Relaxed);
        }

        let mcu_size = crate::jpeg::vars::DCTSIZE as u32_ * JPEG_SAMP_FACTOR as u32_;
        let mcus_per_row = ctx.height() / mcu_size;
        let mcu_rows = ctx.width() / mcu_size;
        let mcu_rows_per_thread =
            core::intrinsics::unchecked_div(mcu_rows + core_count.get() - 1, core_count.get());

        l.n = Fix::fix32(mcu_rows_per_thread);
        l.n_last = Fix::fix32(mcu_rows - mcu_rows_per_thread * core_count_rest);

        let p = v.load_and_progresses().get(&work_index);

        if core_count.get() > 1 && p.n.0 > 0 {
            let mut rows = Fix::load_from_u32(l.n);
            let mut rows_last = Fix::load_from_u32(l.n_last);

            let s = p.p_snapshot;
            let progress_last = *s.get(&thread_id_last);

            let mut progress_all = Fix::fix(0);
            for j in ThreadId::up_to(&core_count) {
                progress_all = progress_all + Fix::fix(*s.get(&j));
            }
            progress_all = progress_all / Fix::fix(core_count.get());

            progress_all = cmp::max(progress_all, Fix::fix(1));

            if progress_last < progress_all.unfix() {
                rows_last = rows_last * Fix::fix(progress_last) / progress_all
                    * Fix::load_from_u32(l.n_last)
                    / Fix::load_from_u32(p.n_last);

                if rows_last < Fix::fix(1) {
                    rows_last = Fix::fix(1)
                } else if rows_last > Fix::load_from_u32(l.n_last) {
                    rows_last = Fix::load_from_u32(l.n_last)
                }

                rows = (Fix::fix(mcu_rows) - rows_last) / Fix::fix(core_count_rest);
            } else {
                let mut progress_rest = 0;
                for j in ThreadId::up_to(&thread_id_last) {
                    progress_rest += s.get(&j);
                }
                rows = rows * Fix::fix(progress_rest) / Fix::fix(core_count_rest) / progress_all
                    * Fix::load_from_u32(l.n)
                    / Fix::load_from_u32(p.n);
            }

            if rows < Fix::fix(mcu_rows_per_thread) {
                rows = Fix::fix(mcu_rows_per_thread)
            } else {
                let rows_max = core::intrinsics::unchecked_div(mcu_rows - 1, core_count_rest);
                if rows > Fix::fix(rows_max) {
                    rows = Fix::fix(rows_max);
                }
            }

            l.n_adjusted = rows.store_to_u32();
            l.n_last_adjusted =
                (Fix::fix(mcu_rows) - rows * Fix::fix(core_count_rest)).store_to_u32();
            l.v_adjusted = Fix::load_from_u32(l.n_adjusted).unfix();
            l.v_last_adjusted = mcu_rows - l.v_adjusted * core_count_rest;
        } else {
            l.n_adjusted = l.n;
            l.v_adjusted = Fix::load_from_u32(l.n).unfix();
            l.n_last_adjusted = l.n_last;
            l.v_last_adjusted = Fix::load_from_u32(l.n_last).unfix();
        }

        for j in ThreadId::up_to(&core_count) {
            let restart_in_rows = l.v_adjusted as s32;
            let restart_interval = restart_in_rows as u32 * mcus_per_row;

            let cinfo = crate::jpeg::CInfo {
                isTop: ctx.is_top,
                colorSpace: match ctx.format {
                    0 => crate::jpeg::vars::ColorSpace::XBGR,
                    1 => crate::jpeg::vars::ColorSpace::BGR,
                    2 => crate::jpeg::vars::ColorSpace::RGB565,
                    3 => crate::jpeg::vars::ColorSpace::RGB5A1,
                    _ => crate::jpeg::vars::ColorSpace::RGB4,
                },
                restartInterval: restart_interval as u16,
                workIndex: w,
            };

            get_jpeg().setInfo(cinfo);

            *ctx.i_start.get_mut(&j) = restart_in_rows as u32 * j.get();
            *ctx.i_count.get_mut(&j) = if j == thread_id_last {
                l.v_last_adjusted
            } else {
                l.v_adjusted
            };
        }

        true
    }
}

fn bctx_init(v: &ThreadBeginVars) -> bool {
    unsafe {
        let ctx = v.ctx();
        let mut ret = false;
        let mut format = v.v().format();

        ctx.is_top = v.v().is_top();
        format &= 0xf;
        if ctx.format != format {
            ret = true;
        }
        ctx.format = format;
        ctx.src = v.v().img_src();
        ctx.frame_id = v.frame_id();

        *ctx.should_capture.as_ptr() = false;

        ret
    }
}

fn do_send_frame(t: &ThreadId, vars: &ThreadDoVars) -> bool {
    unsafe {
        let ctx = vars.blit_ctx();
        let w = vars.v().work_index();
        let p = vars.load_and_progresses().get_mut(&w).p.get_mut(&t);
        let mut worker = get_jpeg().getWorker(w, *t);

        let src = ctx.src;
        let i_start = *ctx.i_start.get(&t);
        let i_count = *ctx.i_count.get(&t);
        let pitch = ctx.pitch();

        let j_start = crate::jpeg::vars::in_rows_blk * pitch as usize * i_start as usize;
        let j_count = crate::jpeg::vars::in_rows_blk * pitch as usize * i_count as usize;
        let i_count_half = J_MAX_HALF_FACTOR(i_count as u32_) as usize;

        let src = &slice::from_raw_parts(src, ctx.src_len() as usize)[j_start..(j_start + j_count)];

        let mut pre_progress_count = 0;
        let pre_progress = || {
            if pre_progress_count >= i_count_half {
                capture_screen(&mut ctx.should_capture, vars);
            }
            pre_progress_count += 1;
        };

        let mut progress_count = 0;
        let progress = || {
            progress_count += 1;
            p.store(progress_count, Ordering::Relaxed);
        };

        let ninfo = crate::entries::thread_nwm::get_nwm_infos()
            .get_mut(&w)
            .get_mut(t);

        let dst = *vars.nwm_infos().get(&t).info.pos.as_ptr() as *mut c_void;
        let dst = crate::jpeg::WorkerDst {
            dst: dst as *mut u8,
            free_in_bytes: crate::jpeg::vars::OUTPUT_BUF_SIZE as u16,
            info: ninfo,
        };

        worker.encode(dst, src, pre_progress, progress);
        true
    }
}

fn capture_screen(should_capture: &mut AtomicBool, vars: &ThreadDoVars) {
    if should_capture.swap(true, Ordering::Relaxed) == false {
        vars.capture_screen();
    }
}
