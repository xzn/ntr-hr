use crate::*;

#[named]
unsafe fn try_capture_screen(work_index: &WorkIndex, wait_sync: bool) {
    let work_syn = (*syn_handles).works.get_mut(&work_index);

    let synced = screens_synced.get_mut(&work_index);
    if !AtomicBool::from_mut(synced).load(Ordering::Relaxed) {
        let res = svcWaitSynchronization(
            work_syn.work_done,
            if wait_sync { THREAD_WAIT_NS } else { 0 },
        );
        if res != 0 {
            if wait_sync && res != RES_TIMEOUT as s32 {
                svcSleepThread(THREAD_WAIT_NS)
            }
            return;
        }
        *synced = true;
    }

    currently_updating = priority_is_top;
    let mut busy_wait = false;

    while !crate::entries::work_thread::reset_threads() {
        if AtomicU32::from_ptr(ptr::addr_of_mut!(port_game_pid)).load(Ordering::Relaxed) == 0 {
            if priority_factor != 0 {
                let frame_count = frame_counts.get_b_mut(currently_updating);

                if *frame_count >= priority_factor {
                    *frame_count -= priority_factor;
                    currently_updating = !currently_updating;
                } else {
                    *frame_count += 1;
                }
            }
            busy_wait = true;
            break;
        }

        let is_top = currently_updating;

        if priority_factor == 0 {
            let res = svcWaitSynchronization(
                *(*syn_handles).port_screen_ready.get_b(is_top),
                THREAD_WAIT_NS,
            );
            if res == 0 {
                break;
            }
            if res != RES_TIMEOUT as s32 {
                nsDbgPrint!(waitForSyncFailed, c_str!("port_screen_ready"), res);
                svcSleepThread(THREAD_WAIT_NS);
            }
            continue;
        }

        let prio = [get_prio_scaled(false), get_prio_scaled(true)];

        let get_factor = |b| -> u32_ {
            ((1 << SCALE_BITS) as u64_ * *frame_queues.get_b(b) as u64_ / prio[b as usize] as u64_)
                as u32_
        };
        let factor = [get_factor(false), get_factor(true)];

        if factor[false as usize] < priority_factor_scaled
            && factor[true as usize] < priority_factor_scaled
        {
            *frame_queues.get_b_mut(false) += priority_factor_scaled;
            *frame_queues.get_b_mut(true) += priority_factor_scaled;
        }

        let is_top = if factor[is_top as usize] >= factor[!is_top as usize] {
            is_top
        } else {
            !is_top
        };

        let try_dequeue = |b| -> bool {
            if *frame_queues.get_b(b) > prio[b as usize] {
                let res = svcWaitSynchronization(*(*syn_handles).port_screen_ready.get_b(b), 0);
                if res == 0 {
                    currently_updating = b;
                    *frame_queues.get_b_mut(b) -= prio[b as usize];
                    return true;
                } else if res != RES_TIMEOUT as s32 {
                    nsDbgPrint!(waitForSyncFailed, c_str!("port_screen_ready"), res);
                }
            }
            false
        };

        if try_dequeue(is_top) {
            break;
        }

        if try_dequeue(!is_top) {
            break;
        }

        let mut out = mem::MaybeUninit::uninit();
        let res = svcWaitSynchronizationN(
            out.as_mut_ptr(),
            (*syn_handles).port_screen_ready.as_mut_ptr(),
            SCREEN_COUNT as s32,
            false,
            THREAD_WAIT_NS,
        );
        if res != 0 {
            if res != RES_TIMEOUT as s32 {
                nsDbgPrint!(waitForSyncFailed, c_str!("port_screen_ready"), res);
                svcSleepThread(THREAD_WAIT_NS);
                break;
            }
            continue;
        }
        let is_top = out.assume_init() > 0;

        if *frame_queues.get_b(is_top) > prio[is_top as usize] {
            *frame_queues.get_b_mut(is_top) -= prio[is_top as usize];
        } else {
            *frame_queues.get_b_mut(is_top) = 0;
        }
        currently_updating = is_top;
        break;
    }

    if busy_wait {
        wait_for_vblank(currently_updating)
    }
    update_gpu_regs(currently_updating);

    if capture_screen(work_index, currently_updating) {
        *screens_captured.get_mut(work_index) = true;

        let mut count = mem::MaybeUninit::<s32>::uninit();

        if AtomicBool::from_mut(skip_frames.get_mut(&work_index)).load(Ordering::Relaxed) {
            let thread_id = AtomicU32::from_ptr(ptr::addr_of_mut!(screen_thread_id) as *mut u32_)
                .load(Ordering::Relaxed);

            let res = svcReleaseSemaphore(
                count.as_mut_ptr(),
                (*syn_handles)
                    .threads
                    .get(&ThreadId::init_unchecked(thread_id))
                    .work_ready,
                1,
            );
            if res != 0 {
                nsDbgPrint!(releaseSemaphoreFailed, c_str!("work_ready"), res);
            }
        } else {
            for j in ThreadId::up_to(&core_count_in_use) {
                let res = svcReleaseSemaphore(
                    count.as_mut_ptr(),
                    (*syn_handles).threads.get(&j).work_ready,
                    1,
                );
                if res != 0 {
                    nsDbgPrint!(releaseSemaphoreFailed, c_str!("work_ready"), res);
                }
            }
        }
    }
}

unsafe fn get_prio_scaled(is_top: bool) -> u32_ {
    if is_top == priority_is_top {
        1 << SCALE_BITS
    } else {
        priority_factor_scaled
    }
}

unsafe fn wait_for_vblank(is_top: bool) {
    gspWaitForEvent(
        if is_top {
            GSPGPU_EVENT_VBlank0
        } else {
            GSPGPU_EVENT_VBlank1
        },
        false,
    );
}

unsafe fn update_gpu_regs(is_top: bool) {
    if is_top {
        cap_info.format = ptr::read_volatile(GPU_FB_TOP_FMT as *const u32_);
        cap_info.pitch = ptr::read_volatile(GPU_FB_TOP_STRIDE as *const u32_);

        let fb = ptr::read_volatile(GPU_FB_TOP_SEL as *const u32_);
        if fb & 1 == 0 {
            cap_info.src = ptr::read_volatile(GPU_FB_TOP_LEFT_ADDR_1 as *const u32_) as *mut u8_;
        } else {
            cap_info.src = ptr::read_volatile(GPU_FB_TOP_LEFT_ADDR_2 as *const u32_) as *mut u8_;
        }

        let full_width = (cap_info.format & (7 << 4)) == 0;
        if full_width {
            cap_info.pitch *= 2;
        }
    } else {
        cap_info.format = ptr::read_volatile(GPU_FB_BOTTOM_FMT as *const u32_);
        cap_info.pitch = ptr::read_volatile(GPU_FB_BOTTOM_STRIDE as *const u32_);

        let fb = ptr::read_volatile(GPU_FB_BOTTOM_SEL as *const u32_);
        if fb & 1 == 0 {
            cap_info.src = ptr::read_volatile(GPU_FB_BOTTOM_ADDR_1 as *const u32_) as *mut u8_;
        } else {
            cap_info.src = ptr::read_volatile(GPU_FB_BOTTOM_ADDR_2 as *const u32_) as *mut u8_;
        }
    }
}

unsafe fn capture_screen(work_index: &WorkIndex, is_top: bool) -> bool {
    let phys = cap_info.src as u32_;
    let iinfo = img_infos.get_b_mut(is_top);
    let dst = *iinfo.bufs.get(&iinfo.index) as u32_;

    let mut process = cap_params.home;

    let format = cap_info.format & 0xf;

    if format > 3 {
        svcSleepThread(THREAD_WAIT_NS);
        return false;
    }

    let bpp: u32_;
    let mut burst_size: u32_ = 16;

    if format == 0 {
        bpp = 4;
        burst_size *= 4;
    } else if format == 1 {
        bpp = 3;
    } else {
        bpp = 2;
        burst_size *= 2;
    }
    let mut transfer_size = 240 * bpp;
    let mut pitch = cap_info.pitch;
    let buf_size = transfer_size * if is_top { 400 } else { 320 };

    if transfer_size == pitch {
        let mut mul = if is_top { 16 } else { 64 };
        transfer_size *= mul;
        while transfer_size >= (1 << 15) {
            transfer_size /= 2;
            mul /= 2;
        }

        burst_size *= mul;
        pitch = transfer_size;
    }

    let dma_conf = DmaConfig {
        channelId: -1,
        flags: (DMACFG_WAIT_AVAILABLE | DMACFG_USE_DST_CONFIG | DMACFG_USE_SRC_CONFIG) as u8_,
        endianSwapSize: 0,
        _padding: 0,
        dstCfg: DmaDeviceConfig {
            deviceId: -1,
            allowedAlignments: 15,
            burstSize: burst_size as s16,
            burstStride: burst_size as s16,
            transferSize: transfer_size as s16,
            transferStride: transfer_size as s16,
        },
        srcCfg: DmaDeviceConfig {
            deviceId: -1,
            allowedAlignments: 15,
            burstSize: burst_size as s16,
            burstStride: burst_size as s16,
            transferSize: transfer_size as s16,
            transferStride: pitch as s16,
        },
    };

    if buf_size > IMG_BUFFER_SIZE as u32_ {
        svcSleepThread(THREAD_WAIT_NS);
        return false;
    }

    let _ = svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE, dst, buf_size);
    let dma = cap_params.dmas.get_mut(&work_index);
    if *dma != 0 {
        let _ = svcCloseHandle(*dma);
        *dma = 0;
    }

    if is_in_vram(phys) {
        close_game_handle();
        let res = svcStartInterProcessDma(
            dma,
            CUR_PROCESS_HANDLE,
            dst,
            process,
            0x1f000000 + (phys - 0x18000000),
            buf_size,
            &dma_conf,
        );
        if res != 0 {
            *dma = 0;
            svcSleepThread(THREAD_WAIT_NS);
            return false;
        }
    } else if is_in_fcram(phys) {
        process = get_game_handle();
        if process == 0 {
            svcSleepThread(THREAD_WAIT_NS);
            return false;
        }
        let res = svcStartInterProcessDma(
            dma,
            CUR_PROCESS_HANDLE,
            dst,
            process,
            cap_params.game_fcram_base + (phys - 0x20000000),
            buf_size,
            &dma_conf,
        );
        if res != 0 {
            *dma = 0;
            close_game_handle();
            svcSleepThread(THREAD_WAIT_NS);
            return false;
        }
    }

    true
}

fn is_in_vram(phys: u32_) -> bool {
    if phys >= 0x18000000 {
        if phys < 0x18000000 + 0x00600000 {
            return true;
        }
    }
    false
}

fn is_in_fcram(phys: u32_) -> bool {
    if phys >= 0x20000000 {
        if phys < 0x20000000 + 0x10000000 {
            return true;
        }
    }
    false
}

unsafe fn close_game_handle() {
    if cap_params.game != 0 {
        let _ = svcCloseHandle(cap_params.game);
        cap_params.game = 0;
        cap_params.game_fcram_base = 0;
        cap_params.game_pid = 0;

        crate::entries::work_thread::no_skip_next_frames();
    }
}
unsafe fn get_game_handle() -> Handle {
    let game_pid = AtomicU32::from_mut(&mut (*rp_config).gamePid).load(Ordering::Relaxed);
    if game_pid != cap_params.game_pid {
        close_game_handle();
        cap_params.game_pid = game_pid;
    }

    let mut process = mem::MaybeUninit::uninit();

    if cap_params.game == 0 {
        if game_pid != 0 {
            let res = svcOpenProcess(process.as_mut_ptr(), game_pid);
            if res == 0 {
                cap_params.game = process.assume_init();
            }
        }
        if cap_params.game == 0 {
            let mut process_count = mem::MaybeUninit::uninit();
            let mut pids: [mem::MaybeUninit<u32_>; LOCAL_PID_BUF_COUNT as usize] =
                mem::MaybeUninit::uninit_array();
            let res = svcGetProcessList(
                process_count.as_mut_ptr(),
                pids.as_mut_ptr() as *mut u32_,
                LOCAL_PID_BUF_COUNT as s32,
            );
            if res == 0 {
                for i in 0..process_count.assume_init() {
                    let pid = pids.get_unchecked(i as usize).assume_init();
                    if pid < 0x28 {
                        continue;
                    }

                    let res = svcOpenProcess(process.as_mut_ptr(), pid);
                    if res == 0 {
                        let process = process.assume_init();
                        let mut tid = mem::MaybeUninit::<[u32_; 2]>::uninit();
                        let res = getProcessTIDByHandle(process, tid.as_mut_ptr() as *mut _) as s32;
                        if res == 0 {
                            if tid.assume_init().get_unchecked(1) & 0xffff == 0 {
                                cap_params.game = process;
                                break;
                            }
                        }
                        let _ = svcCloseHandle(process);
                    }
                }
            }
        }
        if cap_params.game == 0 {
            return 0;
        }
    }
    if cap_params.game_fcram_base == 0 {
        if svcFlushProcessDataCache(cap_params.game, 0x14000000, 0x1000) == 0 {
            cap_params.game_fcram_base = 0x14000000;
        } else if svcFlushProcessDataCache(cap_params.game, 0x30000000, 0x1000) == 0 {
            cap_params.game_fcram_base = 0x30000000;
        } else {
            close_game_handle();
            return 0;
        }
    }

    cap_params.game
}

#[named]
unsafe fn thread_screen_loop() -> Option<()> {
    while !crate::entries::work_thread::reset_threads() {
        let res = svcWaitSynchronization((*syn_handles).screen_ready, THREAD_WAIT_NS);
        if res != 0 {
            if res != RES_TIMEOUT as s32 {
                nsDbgPrint!(waitForSyncFailed, c_str!("screen_ready"), res);
                svcSleepThread(THREAD_WAIT_NS);
            }
            continue;
        }

        let work_index = AtomicU32::from_ptr(ptr::addr_of_mut!(screen_work_index) as *mut u32_)
            .load(Ordering::Relaxed);

        let work_index = &WorkIndex::init_unchecked(work_index);

        let captured = screens_captured.get_mut(work_index);
        while !*captured {
            if crate::entries::work_thread::reset_threads() {
                return None;
            }

            try_capture_screen(work_index, true);
        }
        *captured = false;
    }
    Some(())
}

pub extern "C" fn thread_screen(_: *mut c_void) {
    unsafe {
        thread_screen_loop();
        svcExitThread()
    }
}
