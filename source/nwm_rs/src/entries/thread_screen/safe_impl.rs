use super::*;

fn wait_for_vblank(is_top: bool) {
    unsafe {
        gspWaitForEvent(
            if is_top {
                GSPGPU_EVENT_VBlank0
            } else {
                GSPGPU_EVENT_VBlank1
            },
            false,
        );
    }
}

fn update_gpu_regs(is_top: bool) -> CapInfo {
    unsafe {
        let mut cap_info: CapInfo = const_default();
        if is_top {
            cap_info.format = ptr::read_volatile(GPU_FB_TOP_FMT as *const u32_);
            cap_info.pitch = ptr::read_volatile(GPU_FB_TOP_STRIDE as *const u32_);

            let fb = ptr::read_volatile(GPU_FB_TOP_SEL as *const u32_);
            if fb & 1 == 0 {
                cap_info.src =
                    ptr::read_volatile(GPU_FB_TOP_LEFT_ADDR_1 as *const u32_) as *mut u8_;
            } else {
                cap_info.src =
                    ptr::read_volatile(GPU_FB_TOP_LEFT_ADDR_2 as *const u32_) as *mut u8_;
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
        cap_info
    }
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

unsafe fn capture_screen(is_top: bool, cap_info: &CapInfo, dst: u32, w: WorkIndex) -> bool {
    let phys = cap_info.src as u32_;

    let mut process = home_process_handle;

    let format = cap_info.format & 0xf;

    // Skip if handling of format unimplemented
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
        flags: (DMACFG_WAIT_AVAILABLE | DMACFG_DST_MEMORY_CONFIG | DMACFG_SRC_MEMORY_CONFIG) as u8_,
        endianSwapSize: 0,
        _padding: 0,
        srcDev: DmaDeviceConfig {
            deviceId: -1,
            allowedAlignments: 15,
        },
        dstMem: DmaMemoryConfig {
            burstSize: burst_size as s16,
            burstStride: burst_size as s16,
            transferSize: transfer_size as s16,
            transferStride: transfer_size as s16,
        },
        dstDev: DmaDeviceConfig {
            deviceId: -1,
            allowedAlignments: 15,
        },
        srcMem: DmaMemoryConfig {
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

    let dma = cap_params.dmas.get_mut(&w);
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

pub fn thread_screen_loop(sync: ScreenEncodeSync) -> Option<()> {
    loop {
        let vars_sync = sync.acquire()?;
        let vars = vars_sync.sync(true)?;

        loop {
            let mut is_top = vars.priority_is_top();
            let mut busy_wait = false;
            while !crate::entries::work_thread::reset_threads() {
                if vars.port_game_pid() == 0 {
                    if vars.priority_factor() != 0 {
                        let frame_count = vars.frame_count(is_top);

                        if *frame_count >= vars.priority_factor() {
                            *frame_count -= vars.priority_factor();
                            is_top = !is_top;
                        } else {
                            *frame_count += 1;
                        }
                    }
                    busy_wait = true;
                    break;
                }

                if vars.priority_factor() == 0 {
                    if vars.port_screen_sync(is_top, true) {
                        break;
                    }
                    continue;
                }

                let get_prio_scaled = |s| -> u32_ {
                    if s == vars.priority_is_top() {
                        1 << SCALE_BITS
                    } else {
                        vars.priority_factor_scaled()
                    }
                };

                let prio = [get_prio_scaled(false), get_prio_scaled(true)];

                let get_factor = |b| -> u32_ {
                    unsafe {
                        core::intrinsics::unchecked_div(
                            (1 << SCALE_BITS) as u64_ * *vars.frame_queue(b) as u64_,
                            prio[b as usize] as u64_,
                        ) as u32_
                    }
                };
                let factor = [get_factor(false), get_factor(true)];

                if factor[false as usize] < (1 << SCALE_BITS)
                    && factor[true as usize] < (1 << SCALE_BITS)
                {
                    *vars.frame_queue(false) += vars.priority_factor_scaled();
                    *vars.frame_queue(true) += vars.priority_factor_scaled();
                }

                is_top = if factor[is_top as usize] >= factor[!is_top as usize] {
                    is_top
                } else {
                    !is_top
                };

                let s = is_top;
                let mut try_dequeue = |b| -> bool {
                    if *vars.frame_queue(b) >= prio[b as usize] {
                        if vars.port_screen_sync(b, false) {
                            is_top = b;
                            *vars.frame_queue(b) -= prio[b as usize];
                            return true;
                        }
                    }
                    false
                };

                if try_dequeue(s) {
                    break;
                }

                if try_dequeue(!s) {
                    break;
                }

                if let Some(s) = vars.port_screens_sync() {
                    is_top = s;
                    if *vars.frame_queue(is_top) >= prio[is_top as usize] {
                        *vars.frame_queue(is_top) -= prio[is_top as usize];
                    } else {
                        *vars.frame_queue(is_top) = 0;
                    }
                    break;
                }
            }

            if crate::entries::work_thread::reset_threads() {
                return None;
            }

            if busy_wait {
                wait_for_vblank(is_top)
            }
            let cap_info = update_gpu_regs(is_top);

            let w = vars.screen_work_index();
            if unsafe { capture_screen(is_top, &cap_info, vars.img_dst(is_top), w) } {
                vars.release(is_top, cap_info.format, w);
                break;
            }
        }
    }
}
