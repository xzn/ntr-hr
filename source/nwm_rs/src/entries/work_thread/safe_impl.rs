use super::*;

pub fn send_frame(t: &ThreadId, vars: ThreadVars) -> Option<()> {
    let v = match vars.work_begin_acquire() {
        Ok(mut v) => loop {
            let format_changed = bctx_init(&v);

            v.dma_sync();
            let is_top = v.v().is_top();

            let timing = unsafe { svcGetSystemTick() as u32_ };
            let last_timing = v.get_last_frame_timing();
            if timing - last_timing >= SYSCLOCK_ARM11 {
                unsafe { crate::entries::thread_screen::set_no_skip_frame(is_top) };
            }

            let skip_frame = !unsafe { crate::entries::thread_screen::reset_no_skip_frame(is_top) }
                && !format_changed
                && !v.frame_changed();

            if !skip_frame {
                if unsafe { entries::thread_nwm::get_reliable_stream_method() }
                    == entries::thread_nwm::ReliableStreamMethod::None
                    && !ready_nwm(&v)
                {
                    return None;
                }

                v.ready_next();
                if !ready_work(&v, &t) {
                    return None;
                }

                v.set_last_frame_timing(timing);

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

fn ready_nwm(v: &ThreadBeginVars) -> bool {
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

        let hdr = v.data_buf_hdr();
        let ctx = v.ctx();
        *hdr.get_unchecked_mut(0) = ctx.frame_id;
        *hdr.get_unchecked_mut(1) = ctx.is_top as u8_;
        *hdr.get_unchecked_mut(2) = 2;
        *hdr.get_unchecked_mut(3) = 0;

        crate::entries::thread_nwm::release_nwm_ready(&w);

        true
    }
}

fn ready_work(v: &ThreadBeginVars, t: &ThreadId) -> bool {
    unsafe {
        let ctx = v.ctx();
        let w = v.v().work_index();

        let core_count = get_core_count_in_use();
        let core_count_rest = core_count.get() - 1;
        let thread_id_last = ThreadId::init_unchecked(core_count_rest);

        let l = v.load_and_progresses().get_mut(&w);

        let mcu_size = crate::jpeg::vars::DCTSIZE as u32_ * JPEG_SAMP_FACTOR as u32_;
        let mcus_per_row = ctx.width() / mcu_size;
        let mcu_rows = ctx.height() / mcu_size;
        let mcu_rows_per_thread =
            core::intrinsics::unchecked_div(mcu_rows + core_count.get() - 1, core_count.get());

        let n = mcu_rows_per_thread;
        let n_last = mcu_rows - mcu_rows_per_thread * core_count_rest;

        let (v_adjusted, v_last_adjusted) = if core_count.get() > 1 {
            let mut rows;
            let mut rows_last;

            let mut load_max = 0.0;
            for j in ThreadId::up_to(&thread_id_last) {
                load_max = (*l.get(&j)).max(load_max);
            }
            let load_last = *l.get(&thread_id_last);

            let f = load_max * core_count_rest as f64 + load_last;
            if t.get() == thread_id_last.get() {
                rows_last = core::intrinsics::ceilf64((mcu_rows as f64 * load_last) / f) as u32;

                if rows_last < 1 {
                    rows_last = 1
                } else if rows_last > n_last {
                    rows_last = n_last
                }

                rows = mcu_rows - rows_last / core_count_rest;
            } else {
                rows = core::intrinsics::ceilf64((mcu_rows as f64 * load_max) / f) as u32;
            }

            if rows < mcu_rows_per_thread {
                rows = mcu_rows_per_thread
            } else {
                let rows_max = core::intrinsics::unchecked_div(mcu_rows - 1, core_count_rest);
                if rows > rows_max {
                    rows = rows_max;
                }
            }

            (rows, mcu_rows - rows * core_count_rest)
        } else {
            (n, n_last)
        };

        for j in ThreadId::up_to(&core_count) {
            let restart_in_rows = v_adjusted as s32;
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
                v_last_adjusted
            } else {
                v_adjusted
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
        let p = vars.load_and_progresses().get_mut(&w).get_mut(&t);
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

        let progress = || {};

        let (user, dst) = match entries::thread_nwm::get_reliable_stream_method() {
            entries::thread_nwm::ReliableStreamMethod::None => {
                let ninfo = vars.nwm_infos().get(&t);
                (
                    jpeg::WorkderDstUser {
                        info: ninfo as *const _,
                    },
                    *ninfo.info.pos.as_ptr(),
                )
            }
            entries::thread_nwm::ReliableStreamMethod::KCP => {
                let dst = if let Some(dst) = entries::thread_nwm::alloc_seg() {
                    dst.add((NWM_HDR_SIZE + ARQ_OVERHEAD_SIZE + ARQ_DATA_HDR_SIZE) as usize)
                } else {
                    return false;
                };

                let hdr = const_default();

                (jpeg::WorkderDstUser { hdr }, dst)
            }
        };
        let dst = crate::jpeg::WorkerDst {
            dst: dst as *mut u8,
            free_in_bytes: crate::entries::thread_nwm::get_packet_data_size() as u16,
            user,
        };

        let tick_before = svcGetSystemTick();
        worker.encode(dst, src, pre_progress, progress);
        let tick_after = svcGetSystemTick();

        let tick_diff = cmp::max(tick_after - tick_before, 1);
        *p = i_count as f64 / tick_diff as f64;

        true
    }
}

fn capture_screen(should_capture: &mut AtomicBool, vars: &ThreadDoVars) {
    if should_capture.swap(true, Ordering::Relaxed) == false {
        vars.capture_screen();
    }
}
