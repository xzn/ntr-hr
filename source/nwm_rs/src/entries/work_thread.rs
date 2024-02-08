use crate::*;

pub unsafe fn reset_threads() -> bool {
    AtomicBool::from_ptr(ptr::addr_of_mut!(crate::reset_threads)).load(Ordering::Relaxed)
}

pub unsafe fn no_skip_next_frames() {
    let _ = svcSignalEvent(*(*syn_handles).port_screen_ready.get_b(false));
    let _ = svcSignalEvent(*(*syn_handles).port_screen_ready.get_b(true));

    for i in Ranged::<IMG_WORK_COUNT>::all() {
        **img_infos.get_b_mut(false).bufs.get_mut(&i) += 1;
        **img_infos.get_b_mut(true).bufs.get_mut(&i) += 1;
    }
}

#[named]
pub unsafe fn work_thread_loop(t: ThreadId) {
    let mut w = WorkIndex::init();

    while !reset_threads() {
        let res = svcWaitSynchronization((*syn_handles).threads.get(&t).work_ready, THREAD_WAIT_NS);
        if res != 0 {
            if res != RES_TIMEOUT as s32 {
                nsDbgPrint!(waitForSyncFailed, c_str!("work_ready"), res);
                svcSleepThread(THREAD_WAIT_NS);
            }
            continue;
        }

        if send_frame(&t, &w) {
            w.next_wrapped();
        }
    }
}

#[named]
unsafe fn send_frame(t: &ThreadId, w: &WorkIndex) -> bool {
    let ctx = blit_ctxes.get_mut(&w);
    let wsyn = (*syn_handles).works.get_mut(&w);
    let tsyn = (*syn_handles).threads.get_mut(&t);

    let mut skip_frame = true;

    if !AtomicBool::from_mut(&mut wsyn.work_begin_flag).swap(true, Ordering::Relaxed) {
        while skip_frame {
            ctx.is_top = currently_updating;
            ctx.cinfo = cinfos
                .get_mut(&Ranged::<SCREEN_COUNT>::init_unchecked(ctx.is_top as u32_))
                .get_mut(&w);
            let iinfo = img_infos.get_b_mut(ctx.is_top);
            let format_changed = bctx_init(
                ctx,
                if ctx.is_top { 400 } else { 320 },
                240,
                cap_info.format,
                *iinfo.bufs.get(&iinfo.index),
            );
            let current_frame_id = current_frame_ids.get_b_mut(ctx.is_top);
            ctx.frame_id = *current_frame_id;

            let res = svcWaitSynchronization(*cap_params.dmas.get(&w), THREAD_WAIT_NS);
            if res != 0 {
                if res != RES_TIMEOUT as s32 {
                    nsDbgPrint!(waitForSyncFailed, c_str!("dmas"), res);
                    svcSleepThread(THREAD_WAIT_NS);
                }
                continue;
            }

            let mut img_work_Index = iinfo.index;
            img_work_Index.prev_wrapped();

            let src_len = ctx.width * ctx.src_pitch;
            skip_frame = !format_changed
                && slice::from_raw_parts(ctx.src, src_len as usize)
                    == slice::from_raw_parts(*iinfo.bufs.get(&img_work_Index), src_len as usize);

            if !skip_frame {
                if !ready_nwm(&t, &w, ctx.frame_id, ctx.is_top) {
                    skip_frame = true;
                } else {
                    iinfo.index.next_wrapped();
                    *current_frame_id += 1;
                    ready_work(ctx, &w);
                }
            }

            AtomicBool::from_mut(skip_frames.get_mut(&w)).store(skip_frame, Ordering::Relaxed);

            let mut count = mem::MaybeUninit::uninit();
            if !skip_frame {
                AtomicBool::from_mut(screens_synced.get_mut(&w)).store(false, Ordering::Relaxed);

                for j in ThreadId::up_to_unchecked(core_count_in_use.get()) {
                    if j != *t {
                        let res = svcReleaseSemaphore(
                            count.as_mut_ptr(),
                            (*syn_handles).threads.get(&j).work_begin_ready,
                            1,
                        );
                        if res != 0 {
                            nsDbgPrint!(releaseSemaphoreFailed, c_str!("work_begin_ready"), res);
                        }
                    }
                }
            } else {
                AtomicU32::from_ptr(ptr::addr_of_mut!(screen_thread_id) as *mut _)
                    .store(t.get(), Ordering::Release);

                let res = svcReleaseSemaphore(count.as_mut_ptr(), (*syn_handles).screen_ready, 1);
                if res != 0 {
                    nsDbgPrint!(releaseSemaphoreFailed, c_str!("screen_ready"), res);
                }

                loop {
                    if reset_threads() {
                        return false;
                    }

                    let res = svcWaitSynchronization(tsyn.work_ready, THREAD_WAIT_NS);
                    if res != 0 {
                        if res != RES_TIMEOUT as s32 {
                            nsDbgPrint!(waitForSyncFailed, c_str!("work_ready"), res);
                            svcSleepThread(THREAD_WAIT_NS);
                        }
                        continue;
                    }
                    break;
                }
            }
        }
    } else {
        loop {
            if reset_threads() {
                return false;
            }
            let res = svcWaitSynchronization(tsyn.work_begin_ready, THREAD_WAIT_NS);
            if res != 0 {
                if res != RES_TIMEOUT as s32 {
                    nsDbgPrint!(waitForSyncFailed, c_str!("work_begin_ready"), res);
                    svcSleepThread(THREAD_WAIT_NS);
                }
                continue;
            }
            break;
        }
        skip_frame = AtomicBool::from_mut(skip_frames.get_mut(&w)).load(Ordering::Relaxed);
    }

    if !skip_frame {
        do_send_frame(ctx, &t, &w);
    }

    let f = AtomicU32::from_mut(&mut wsyn.work_done_count).fetch_add(1, Ordering::Relaxed);
    if f == 0 && !skip_frame {
        let p = load_and_progresses.get_mut(&w);
        for j in ThreadId::up_to_unchecked(core_count_in_use.get()) {
            *p.p_snapshot.get_mut(&j) =
                AtomicU32::from_mut(p.p.get_mut(&j)).load(Ordering::Relaxed);
        }
    }
    if f == core_count_in_use.get() - 1 {
        AtomicU32::from_mut(&mut wsyn.work_done_count).store(0, Ordering::Relaxed);

        AtomicBool::from_mut(&mut wsyn.work_begin_flag).store(false, Ordering::Relaxed);

        if !skip_frame {
            let mut count = mem::MaybeUninit::uninit();
            let res = svcReleaseSemaphore(count.as_mut_ptr(), wsyn.work_done, 1);
            if res != 0 {
                nsDbgPrint!(releaseSemaphoreFailed, c_str!("work_done"), res);
            }
        }
    }

    !skip_frame
}

#[named]
unsafe fn ready_nwm(_t: &ThreadId, w: &WorkIndex, id: u8_, is_top: bool) -> bool {
    let wsyn = (*syn_handles).works.get(&w);

    loop {
        if reset_threads() {
            return false;
        }

        let res = svcWaitSynchronization(wsyn.nwm_done, THREAD_WAIT_NS);
        if res != 0 {
            if res != RES_TIMEOUT as s32 {
                nsDbgPrint!(waitForSyncFailed, c_str!("nwm_done"), res);
                svcSleepThread(THREAD_WAIT_NS);
            }
            continue;
        }
        break;
    }

    for j in ThreadId::up_to_unchecked(core_count_in_use.get()) {
        let ninfo = nwm_infos.get_mut(&w).get_mut(&j);
        let info = &mut ninfo.info;
        let buf = ninfo.buf;
        info.send_pos = buf;
        info.pos = buf;
        info.flag = 0;
    }

    let mut count = mem::MaybeUninit::uninit();
    let res = svcReleaseSemaphore(count.as_mut_ptr(), wsyn.nwm_ready, 1);
    if res != 0 {
        nsDbgPrint!(releaseSemaphoreFailed, c_str!("nwm_ready"), res);
    }

    let hdr = data_buf_hdrs.get_mut(&w);
    *hdr.get_unchecked_mut(0) = id;
    *hdr.get_unchecked_mut(1) = is_top as u8_;
    *hdr.get_unchecked_mut(2) = 2;
    *hdr.get_unchecked_mut(3) = 0;

    true
}

unsafe fn ready_work(ctx: &mut BlitCtx, w: &WorkIndex) {
    let mut work_index = *w;
    work_index.prev_wrapped();

    let core_count = core_count_in_use.get();
    let core_count_rest = core_count - 1;
    let thread_id_last = ThreadId::init_unchecked(core_count - 1);

    let l = load_and_progresses.get_mut(&w);
    for j in ThreadId::up_to_unchecked(core_count) {
        AtomicU32::from_mut(l.p.get_mut(&j)).store(0, Ordering::Relaxed);
    }

    let mcu_size = DCTSIZE * JPEG_SAMP_FACTOR as u32_;
    let mcus_per_row = ctx.height / mcu_size;
    let mcu_rows = ctx.width / mcu_size;
    let mcu_rows_per_thread = (mcu_rows + core_count_rest) / core_count;

    l.n = mcu_rows_per_thread;
    l.n_last = mcu_rows - l.n * core_count_rest;

    let p = load_and_progresses.get(&work_index);

    if core_count > 1 && p.n > 0 {
        let mut rows = l.n;
        let mut rows_last = l.n_last;

        let s = p.p_snapshot;
        let progress_last = *s.get(&thread_id_last);
        if progress_last < p.n_last_adjusted {
            rows_last = (rows_last * (1 << 16) * progress_last / p.n_last + (1 << 15)) >> 16;

            if rows_last == 0 {
                rows_last = 1
            } else if rows_last > l.n_last {
                rows_last = l.n_last
            }

            rows = (mcu_rows - rows_last) / core_count_rest;
        } else {
            let mut progress_rest = 0;
            for j in ThreadId::up_to_unchecked(thread_id_last.get()) {
                progress_rest += s.get(&j);
            }
            rows = (rows * (1 << 16) * progress_rest / p.n / core_count_rest + (1 << 15)) >> 16;
            if rows < l.n {
                rows = l.n
            } else {
                let rows_max = (mcu_rows - 1) / core_count_rest;
                if rows > rows_max {
                    rows = rows_max;
                }
            }
        }
        l.n_adjusted = rows;
        l.n_last_adjusted = mcu_rows - rows * core_count_rest;
    } else {
        l.n_adjusted = l.n;
        l.n_last_adjusted = l.n_last;
    }

    for j in ThreadId::up_to_unchecked(core_count) {
        let cinfo = &mut (*ctx.cinfo).get_mut(&j).info;
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
        cinfo.restart_in_rows = l.n_adjusted as i32;
        cinfo.restart_interval = cinfo.restart_in_rows as u32 * mcus_per_row;

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

        *ctx.i_start.get_mut(&j) = cinfo.restart_in_rows as u32 * j.get();
        *ctx.i_count.get_mut(&j) = if j == thread_id_last {
            l.n_last_adjusted
        } else {
            l.n_adjusted
        };
    }
    ctx.should_capture = false;
}

unsafe fn bctx_init(ctx: &mut BlitCtx, w: u32_, h: u32_, mut format: u32_, src: *mut u8_) -> bool {
    let mut ret = false;
    ctx.bpp = get_bpp_for_format(format);
    format &= 0xf;
    if ctx.format != format {
        ret = true;

        for j in ThreadId::up_to_unchecked(core_count_in_use.get()) {
            let info = (*ctx.cinfo).get_mut(&j);
            if info.info.global_state != JPEG_CSTATE_START {
                ptr::copy_nonoverlapping(
                    ptr::addr_of!(info.info.alloc.stats),
                    ptr::addr_of_mut!(info.alloc_stats.comp),
                    1,
                );
                info.info.global_state = JPEG_CSTATE_START;
            }
        }
    }
    ctx.format = format;
    ctx.width = w;
    ctx.height = h;
    ctx.src_pitch = ctx.bpp * ctx.height;
    ctx.src = src;

    ret
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

unsafe fn do_send_frame(ctx: &mut BlitCtx, t: &ThreadId, w: &WorkIndex) {
    let cinfo = &mut (*ctx.cinfo).get_mut(&t).info;

    cinfo.client_data = nwm_infos.get(&w).get(&t).info.pos as *mut _;
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
        w,
        t,
        &mut ctx.should_capture,
    );
    jpeg_finish_pass_huff(cinfo);

    if t.get() != core_count_in_use.get() - 1 {
        jpeg_emit_marker(cinfo, (JPEG_RST0 + t.get()) as s32);
    } else {
        jpeg_write_file_trailer(cinfo);
    }
    jpeg_term_destination(cinfo);
}

unsafe fn really_do_send_frame(
    cinfo: &mut jpeg_compress_struct,
    src: *mut u8_,
    pitch: u32_,
    i_start: u32_,
    i_count: u32_,
    w: &WorkIndex,
    t: &ThreadId,
    should_capture: &mut bool,
) {
    const in_rows_blk: u32_ = DCTSIZE * JPEG_SAMP_FACTOR as u32_;
    const in_rows_blk_half: u32_ = in_rows_blk / 2;

    let bufs = work_buffers.get_mut(&w).get_mut(&t);
    let output_buf = &mut bufs.prep;
    let color_buf = &mut bufs.color;

    let mut input_buf: [_; in_rows_blk_half as usize] =
        mem::MaybeUninit::<JSAMPROW>::uninit_array();

    let j_max = in_rows_blk * (i_start + i_count);
    let _j_max = cmp::min(j_max, cinfo.image_height);
    let j_max_half = in_rows_blk * (i_start + i_count / 2);
    let j_max_half = cmp::min(j_max_half, cinfo.image_height);

    let j_start = in_rows_blk * i_start;
    if j_max_half == j_start {
        capture_screen(&t, should_capture, &w);
    }

    let mut j = j_start;
    let mut progress = 0;
    let p = load_and_progresses.get_mut(&w).p.get_mut(&t);
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
            capture_screen(&t, should_capture, &w);
        }

        let mcu_buffer = &mut bufs.mcu;
        for k in 0..cinfo.MCUs_per_row {
            jpeg_compress_data(cinfo, output_buf.as_mut_ptr(), mcu_buffer.as_mut_ptr(), k);
            jpeg_encode_mcu_huff(cinfo, mcu_buffer.as_mut_ptr());
        }

        progress += 1;
        AtomicU32::from_mut(p).store(progress, Ordering::Relaxed);

        if j == j_max {
            break;
        }
    }
}

#[named]
unsafe fn capture_screen(_t: &ThreadId, should_capture: &mut bool, w: &WorkIndex) {
    if !AtomicBool::from_mut(should_capture).swap(true, Ordering::Relaxed) {
        let mut w = *w;
        w.next_wrapped();

        AtomicU32::from_ptr(ptr::addr_of_mut!(screen_work_index) as *mut u32_)
            .store(w.get(), Ordering::Relaxed);

        let mut count = mem::MaybeUninit::uninit();
        let res = svcReleaseSemaphore(count.as_mut_ptr(), (*syn_handles).screen_ready, 1);
        if res != 0 {
            nsDbgPrint!(releaseSemaphoreFailed, c_str!("screen_ready"), res);
        }
    }
}

#[named]
#[no_mangle]
extern "C" fn rpMalloc(cinfo: j_common_ptr, size: u32_) -> *mut c_void {
    unsafe {
        let info = &mut *cinfo;
        let ret = info.alloc.buf.add(info.alloc.stats.offset as usize);
        let mut total_size = size;

        if total_size % 32 != 0 {
            total_size += 32 - (total_size % 32);
        }

        if info.alloc.stats.remaining < total_size {
            let alloc_size = info.alloc.stats.offset + info.alloc.stats.remaining;
            nsDbgPrint!(allocFailed, total_size, alloc_size);
            return ptr::null_mut();
        }

        info.alloc.stats.offset += total_size;
        info.alloc.stats.remaining -= total_size;

        return ret as *mut _;
    }
}

#[no_mangle]
extern "C" fn rpFree(_: j_common_ptr, _: *mut c_void) {}
