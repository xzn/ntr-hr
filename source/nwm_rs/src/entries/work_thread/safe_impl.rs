use super::*;

pub fn send_frame(t: &ThreadId, vars: ThreadVars) -> Option<()> {
    let v = match vars.work_begin_acquire() {
        Ok(mut v) => loop {
            let format_changed = bctx_init(&v);

            v.dma_sync();

            let skip_frame = !format_changed && !v.frame_changed();

            if !skip_frame {
                if !ready_nwm(&t, &v) {
                    return None;
                } else {
                    v.ready_next();
                    if !ready_work(&v) {
                        set_reset_threads_ar();
                        return None;
                    }
                }
            }

            if !skip_frame {
                break v.release_and_capture_screen(&t);
            } else {
                if let Some(_) = v.release_skip_frame(&t) {
                    v.v_mut().read_is_top();
                } else {
                    return None;
                }
            }
        },
        Err(v) => v.acquire(&t)?,
    };

    if reset_threads() {
        return None;
    }

    do_send_frame(&t, &v);

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

        let mcu_size = DCTSIZE * JPEG_SAMP_FACTOR as u32_;
        let mcus_per_row = ctx.height / mcu_size;
        let mcu_rows = ctx.width / mcu_size;
        let mcu_rows_per_thread = (mcu_rows + core_count.get() - 1) / core_count.get();

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
                let rows_max = (mcu_rows - 1) / core_count_rest;
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
            let cinfo: &mut jpeg_compress_struct = &mut (*ctx.cinfo).get_mut(&j).info;
            cinfo.image_width = ctx.height;
            cinfo.image_height = ctx.width;
            cinfo.input_components = if ctx.format == 0 { 4 } else { 3 };
            cinfo.in_color_space = match ctx.format {
                0 => JCS_EXT_XBGR,
                1 => JCS_EXT_BGR,
                2 => JCS_EXT_RGB565,
                3 => JCS_EXT_RGB5A1,
                _ => JCS_EXT_RGB4,
            };
            cinfo.restart_in_rows = l.v_adjusted as s32;
            cinfo.restart_interval = cinfo.restart_in_rows as u32 * mcus_per_row;

            if setjmp::setjmp(&mut cinfo.err_jmp_buf) != 0 {
                cinfo.has_err_jmp_buf = 0;
                set_reset_threads_ar();
                return false;
            }
            cinfo.has_err_jmp_buf = 1;

            if cinfo.global_state == JPEG_CSTATE_START {
                jpeg_start_compress(cinfo, 1);
            } else {
                jpeg_suppress_tables(cinfo, 0);
                jpeg_start_pass_prep(cinfo, 0);
                jpeg_start_pass_huff(cinfo, j.get() as i32);
                jpeg_start_pass_coef(cinfo, 0);
                jpeg_start_pass_main(cinfo, 0);
                cinfo.next_scanline = 0;
            }

            cinfo.has_err_jmp_buf = 0;

            *ctx.i_start.get_mut(&j) = cinfo.restart_in_rows as u32 * j.get();
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
        ctx.cinfo = v.cinfos();
        ctx.bpp = get_bpp_for_format(format);
        format &= 0xf;
        if ctx.format != format {
            ret = true;

            for j in ThreadId::up_to(&get_core_count_in_use()) {
                let info = (*ctx.cinfo).get_mut(&j);
                if info.info.global_state != JPEG_CSTATE_START {
                    ptr::copy_nonoverlapping(
                        ptr::addr_of!(info.alloc_stats.comp),
                        ptr::addr_of_mut!(info.info.alloc.stats),
                        1,
                    );
                    info.info.global_state = JPEG_CSTATE_START;
                }
            }
        }
        ctx.format = format;
        ctx.width = if v.v().is_top() { 400 } else { 320 };
        ctx.height = 240;
        ctx.src_pitch = ctx.bpp * ctx.height;
        ctx.src = v.v().img_src();
        ctx.frame_id = v.frame_id();

        *ctx.should_capture.as_ptr() = false;

        ret
    }
}

fn get_bpp_for_format(mut format: u32_) -> u32_ {
    format &= 0xf;
    if format == 0 {
        4
    } else if format == 1 {
        3
    } else {
        2
    }
}

fn do_send_frame(t: &ThreadId, vars: &ThreadDoVars) -> bool {
    unsafe {
        let ctx = vars.blit_ctx();
        let cinfo = &mut (*ctx.cinfo).get_mut(&t).info;

        if setjmp::setjmp(&mut cinfo.err_jmp_buf) != 0 {
            cinfo.has_err_jmp_buf = 0;
            set_reset_threads_ar();
            return false;
        }
        cinfo.has_err_jmp_buf = 1;

        cinfo.client_data = *vars.nwm_infos().get(&t).info.pos.as_ptr() as *mut c_void;
        jpeg_init_destination(cinfo);

        if t.get() == 0 {
            jpeg_write_file_header(cinfo);
            jpeg_write_frame_header(cinfo);
            jpeg_write_scan_header(cinfo);
        }
        really_do_send_frame(
            cinfo,
            ctx.src,
            ctx.src_pitch,
            *ctx.i_start.get(&t),
            *ctx.i_count.get(&t),
            t,
            vars,
            &mut ctx.should_capture,
        );
        jpeg_finish_pass_huff(cinfo);

        if t.get() != get_core_count_in_use().get() - 1 {
            jpeg_emit_marker(cinfo, (JPEG_RST0 + t.get()) as s32);
        } else {
            jpeg_write_file_trailer(cinfo);
        }
        jpeg_term_destination(cinfo);

        cinfo.has_err_jmp_buf = 0;
        true
    }
}

unsafe fn really_do_send_frame(
    cinfo: &mut jpeg_compress_struct,
    src: *mut u8_,
    pitch: u32_,
    i_start: u32_,
    i_count: u32_,
    t: &ThreadId,
    vars: &ThreadDoVars,
    should_capture: &mut AtomicBool,
) {
    let w = vars.v().work_index();
    let p = vars.load_and_progresses().get_mut(&w).p.get_mut(&t);

    const in_rows_blk: u32_ = DCTSIZE * JPEG_SAMP_FACTOR as u32_;
    const in_rows_blk_half: u32_ = in_rows_blk / 2;

    let bufs = work_buffers.get_mut(&w).get_mut(&t);
    let output_buf = &mut bufs.prep;
    let color_buf = &mut bufs.color;

    let mut input_buf: [_; in_rows_blk_half as usize] =
        mem::MaybeUninit::<JSAMPROW>::uninit_array();

    let j_max = in_rows_blk * (i_start + i_count);
    let j_max: u32 = cmp::min(j_max, cinfo.image_height);
    let j_max_half = in_rows_blk * (i_start + J_MAX_HALF_FACTOR(i_count));
    let j_max_half = cmp::min(j_max_half, cinfo.image_height);

    let j_start = in_rows_blk * i_start;
    if j_max_half == j_start {
        capture_screen(should_capture, vars);
    }

    let mut j = j_start;
    let mut progress = 0;
    loop {
        let mut pre_process = |h| {
            for i in 0..in_rows_blk_half {
                (*input_buf.get_unchecked_mut(i as usize))
                    .write(src.add((j * pitch) as usize) as *mut _);
                j += 1;
            }
            jpeg_pre_process(
                cinfo,
                mem::MaybeUninit::array_assume_init(input_buf).as_mut_ptr(),
                color_buf.as_mut_ptr(),
                output_buf.as_mut_ptr(),
                h,
            );
        };
        pre_process(0);
        pre_process(1);

        if j_max_half == j {
            capture_screen(should_capture, vars);
        }

        let mcu_buffer = &mut bufs.mcu;
        for k in 0..cinfo.MCUs_per_row {
            jpeg_compress_data(cinfo, output_buf.as_mut_ptr(), mcu_buffer.as_mut_ptr(), k);
            jpeg_encode_mcu_huff(cinfo, mcu_buffer.as_mut_ptr());
        }

        progress += 1;
        p.store(progress, Ordering::Relaxed);

        if j == j_max {
            break;
        }
    }
}

fn capture_screen(should_capture: &mut AtomicBool, vars: &ThreadDoVars) {
    if should_capture.swap(true, Ordering::Relaxed) == false {
        vars.capture_screen();
    }
}
