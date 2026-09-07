use super::*;

pub static mut RELIABLE_STREAM_CB: *mut rp_cb = const_default();
#[cfg(not(feature = "o3ds"))]
pub static mut RELIABLE_STREAM_CB_LOCK: Handle = 0;
#[cfg(not(feature = "o3ds"))]
pub static mut RELIABLE_STREAM_CB_EVT: Handle = 0;
#[cfg(not(feature = "o3ds"))]
pub static mut RELIABLE_STREAM_CB_INITED: AtomicBool = const_default();

#[cfg(not(feature = "o3ds"))]
pub static mut SEG_MEM_SEM: Handle = 0;
#[cfg(not(feature = "o3ds"))]
pub static mut SEG_MEM_LOCK: Handle = 0;
#[cfg(not(feature = "o3ds"))]
pub static mut SEG_MEM_TERM_SEM: Handle = 0;

#[named]
#[allow(unused_macros)]
pub unsafe fn once_reliable_stream_cb() -> Option<()> {
    unsafe {
        if let Some(m) = request_mem_from_pool::<{ mem::size_of::<rp_cb>() }>() {
            RELIABLE_STREAM_CB = m.to_ptr() as *mut rp_cb;
            let cb = &mut *RELIABLE_STREAM_CB;
            ptr::write_bytes(cb as *mut _ as *mut u8, 0, mem::size_of::<rp_cb>());
        } else {
            return None;
        }

        #[cfg(not(feature = "o3ds"))]
        {
            let res = svcCreateMutex(&mut RELIABLE_STREAM_CB_LOCK, false);
            if res != 0 {
                ns_dbg_print!(failed, c_str!("Create nwm mutex"), res);
                return None;
            }
            let res = create_event(&mut RELIABLE_STREAM_CB_EVT);
            if res != 0 {
                ns_dbg_print!(failed, c_str!("Create nwm recv event"), res);
                return None;
            }
            RELIABLE_STREAM_CB_INITED.store(true, Ordering::Release);
        }
    }
    Some(())
}

#[named]
#[cfg(not(feature = "o3ds"))]
pub unsafe fn init_reliable_stream_cb(qos: u32) -> Option<()> {
    unsafe {
        let syn = &mut (*RELIABLE_STREAM_CB);
        let cb = &mut syn.locked;
        if mp_init(
            (*cb.send_bufs.as_ptr()).len(),
            cb.send_bufs.len(),
            cb.send_bufs.as_mut_ptr().as_mut_ptr() as *mut _,
            &mut cb.send_pool,
        ) < 0
        {
            ns_dbg_print!(mp_init_failed, c_str!("send_pool"));
            return None;
        }

        if mp_init(
            (*cb.cur_send_bufs.as_ptr()).len(),
            cb.cur_send_bufs.len(),
            cb.cur_send_bufs.as_mut_ptr().as_mut_ptr() as *mut _,
            &mut cb.cur_send_pool,
        ) < 0
        {
            ns_dbg_print!(mp_init_failed, c_str!("cur_send_pool"));
            return None;
        }

        let res = rp_syn_init1(
            &mut syn.nwm_syn,
            0,
            ptr::null_mut(),
            0,
            ((RP_ARQ_BUFS_COUNT * qos + RP_QOS_MAX / 2) / RP_QOS_MAX) as i32,
            syn.nwm_syn_data.as_mut_ptr(),
        );
        if res != 0 {
            ns_dbg_print!(failed, c_str!("Nwm syn init"), res);
            return None;
        }
    }
    Some(())
}

pub unsafe fn init_ov_stats() {
    unsafe {
        ptr::write_bytes(
            config_consts::OV_STATS as *mut u8,
            0,
            mem::size_of_val(&*config_consts::OV_STATS),
        );
        #[cfg(not(feature = "o3ds"))]
        {
            (*config_consts::OV_STATS).kcp_mode = match entries::thread_nwm::get_reliable_stream() {
                entries::thread_nwm::ReliableStream::None => 0,
                entries::thread_nwm::ReliableStream::KCP => {
                    #[cfg(not(feature = "o3ds"))]
                    if entries::thread_nwm::get_reliable_stream_delta_prog() {
                        2
                    } else {
                        1
                    }

                    #[cfg(feature = "o3ds")]
                    {
                        1
                    }
                }
            };
        }
        #[cfg(feature = "o3ds")]
        {
            (*config_consts::OV_STATS).kcp_mode = 0;
        }
    }
}

#[named]
#[cfg(not(feature = "o3ds"))]
pub unsafe fn init_seg_mem_handles(qos: u32) -> Option<()> {
    unsafe {
        let res = svcCreateSemaphore(
            &mut SEG_MEM_SEM,
            ((RP_ARQ_ENCODE_BUFS_COUNT * qos + RP_QOS_MAX / 2) / RP_QOS_MAX) as s32,
            ((RP_ARQ_ENCODE_BUFS_COUNT * qos + RP_QOS_MAX / 2) / RP_QOS_MAX) as s32,
        );
        if res != 0 {
            ns_dbg_print!(create_semaphore_failed, c_str!("SEG_MEM_SEM"), res);
            return None;
        }
        let res = svcCreateMutex(&mut SEG_MEM_LOCK, false);
        if res != 0 {
            ns_dbg_print!(create_mutex_failed, c_str!("SEG_MEM_LOCK"), res);
            return None;
        }

        let term_sem_count = if get_reliable_stream_delta_prog() {
            WORK_COUNT as i32
        } else {
            1
        };
        let res = svcCreateSemaphore(&mut SEG_MEM_TERM_SEM, term_sem_count, term_sem_count);
        if res != 0 {
            ns_dbg_print!(create_semaphore_failed, c_str!("SEG_MEM_TERM_SEM"), res);
            return None;
        }
    }
    Some(())
}

#[cfg(not(feature = "o3ds"))]
pub fn cleanup_seg_mem_handles() {
    unsafe {
        let _ = svcCloseHandle(SEG_MEM_TERM_SEM);

        let _ = svcCloseHandle(SEG_MEM_LOCK);
        let _ = svcCloseHandle(SEG_MEM_SEM);
    }
}

pub type NwmHdr = [u8; entries::start_up::NwmHdr::N];
static mut CURRENT_NWM_HDR: NwmHdr = const_default();

pub fn get_current_nwm_hdr() -> *mut NwmHdr {
    unsafe { &mut CURRENT_NWM_HDR }
}

#[cfg(not(feature = "o3ds"))]
static mut KCP_CONV: u8 = 0;
#[unsafe(export_name = "rp_max_qos")]
static mut MAX_QOS: u32 = const_default();

#[unsafe(export_name = "rp_current_qos")]
static mut CURRENT_QOS: AtomicU32 = const_default();

#[cfg(not(feature = "o3ds"))]
#[allow(unused)]
pub fn rp_rel_stream_max_qos() -> u32 {
    unsafe { MAX_QOS }
}

#[cfg(not(feature = "o3ds"))]
pub fn rp_delta_q_qos() -> u32 {
    unsafe { CURRENT_QOS.load(Ordering::Acquire) }
}

#[unsafe(no_mangle)]
extern "C" fn rp_set_qos(qos: u32) {
    unsafe { init_min_send_interval(qos) }
}

#[named]
#[unsafe(no_mangle)]
#[cfg(not(feature = "o3ds"))]
extern "C" fn rp_udp_output(buf: *mut u8, len: s32, tick: *mut u32, kcp: *mut ikcpcb) -> s32 {
    if len > PACKET_SIZE as s32 {
        ns_dbg_print!(failed, c_str!("Nwm output packet len overflow"), len);
        return -3;
    }

    let next_send_tick = unsafe { RP_OUTPUT_NEXT_TICK };
    let mut curr_tick = get_system_tick().get() as u32;
    let tick_diff = (next_send_tick - curr_tick) as s32;
    let duration_ns = if tick_diff > 0 {
        DurationTick::init(tick_diff as s64).get_ns()
    } else {
        DurationNs::init(0)
    };
    if duration_ns.get() > 0 {
        unsafe {
            nwm_cb_unlock(cname!());
        }
        sleep_thread(duration_ns);
        if nwm_cb_lock() == None {
            set_reset_threads();
            return -0x10 - 5;
        }

        if unsafe { (*kcp).rp_output_retry } {
            return 0;
        }
        curr_tick = if NWM_AGGRESSIVE_NEXT_TICK > 0 {
            next_send_tick
        } else {
            get_system_tick().get() as u32
        };
    }

    let next_interval =
        if unsafe { (*kcp).session_established } && NWM_PROPORTIONAL_MIN_INTERVAL > 0 {
            unsafe { MIN_SEND_INTERVAL_TICK * len as u32 / PACKET_SIZE }
        } else {
            unsafe { MIN_SEND_INTERVAL_TICK }
        };
    unsafe { *tick = curr_tick };
    unsafe { RP_OUTPUT_NEXT_TICK = curr_tick + next_interval };

    unsafe { nwm_output(buf.sub(NWM_HDR_SIZE as usize), len as usize) };
    return len;
}

#[cfg(not(feature = "o3ds"))]
unsafe fn rp_recv_data_buf()
-> Option<&'static mut [[c_char; RP_RECV_PACKET_SIZE as usize]; RP_RECV_BUF_N as usize]> {
    if !unsafe { RELIABLE_STREAM_CB_INITED.load(Ordering::Acquire) } {
        return None;
    }

    let cb = unsafe { &mut *RELIABLE_STREAM_CB };
    Some(&mut cb.locked.recv_buf)
}

#[named]
#[unsafe(no_mangle)]
#[cfg(not(feature = "o3ds"))]
extern "C" fn nsControlRecv(fd: c_int) -> c_int {
    let recv_bufs = if let Some(dst) = unsafe { rp_recv_data_buf() } {
        dst
    } else {
        return -1;
    };

    let mut recv_ret = 0;
    let mut recv_idx = 0;
    let mut recv_buf = ptr::null_mut::<u8>();
    loop {
        let buf = unsafe { recv_bufs.get_unchecked_mut(recv_idx) };
        let ret = unsafe {
            recv(
                fd,
                buf.as_mut_ptr() as *mut _,
                RP_RECV_PACKET_SIZE as usize,
                0,
            )
        };
        if ret == 0 {
            break;
        } else if ret < 0 {
            let err = unsafe { *__errno() };
            if err != ctru::EWOULDBLOCK as i32 || err != ctru::EAGAIN as i32 {
                ns_dbg_print!(failed, c_str!("Nwm input"), err);
                return -1;
            }
            break;
        }
        recv_ret = ret;
        recv_buf = buf.as_mut_ptr();
        recv_idx = (recv_idx + 1) % RP_RECV_BUF_N as usize;
    }

    if recv_ret == 0 {
        ns_dbg_print!(msg, c_str!("Nwm input nothing"));
        return 0;
    }

    let nwm_lock = if let Some(l) = NwmCbLock::lock(cname!()) {
        l
    } else {
        return -1;
    };

    match get_reliable_stream() {
        ReliableStream::None => {
            return 0;
        }
        ReliableStream::KCP => {
            let cb = unsafe { &mut *RELIABLE_STREAM_CB };
            let kcp = &mut cb.locked.ikcp;

            let ret = unsafe { ikcp_input(kcp, recv_buf, recv_ret as i32) };
            if ret < 0 {
                // Reset KCP
                if ret < -0x10 {
                    ns_dbg_print!(failed, c_str!("KCP input"), ret);
                }
                set_reset_threads();
                return -1;
            }
            drop(nwm_lock);
            let _ = unsafe { svcSignalEvent(RELIABLE_STREAM_CB_EVT) };
        }
    }

    0
}

#[named]
#[unsafe(no_mangle)]
#[cfg(not(feature = "o3ds"))]
extern "C" fn ikcp_seg_data_buf_malloc() -> *mut c_char {
    if let Some(dst) = loop {
        if unsafe { CUR_SEG_MEM_COUNT } == SEND_CUR_BUFS_COUNT {
            break None;
        }

        let cb = unsafe { &mut *RELIABLE_STREAM_CB };
        let dst = unsafe { mp_malloc(&mut cb.locked.cur_send_pool) } as *mut u8;
        if dst == ptr::null_mut() {
            ns_dbg_print!(msg, c_str!("Mem pool cur send alloc failed"));
            set_reset_threads();
            break None;
        }

        unsafe { CUR_SEG_MEM_COUNT += 1 };
        break Some(dst);
    } {
        return unsafe { dst.add((NWM_HDR_SIZE + ARQ_OVERHEAD_SIZE) as usize) };
    } else {
        return ptr::null_mut();
    }
}

#[named]
#[unsafe(no_mangle)]
#[cfg(not(feature = "o3ds"))]
extern "C" fn ikcp_seg_data_buf_free(dst: *const c_char) {
    let cb = unsafe { &mut *RELIABLE_STREAM_CB };
    if unsafe {
        mp_free(
            &mut cb.locked.cur_send_pool,
            dst.sub((NWM_HDR_SIZE + ARQ_OVERHEAD_SIZE) as usize) as *mut _,
        )
    } < 0
    {
        ns_dbg_print!(msg, c_str!("Mem pool cur send free failed"));
        set_reset_threads();
        return;
    }
    unsafe { CUR_SEG_MEM_COUNT -= 1 };
}

#[unsafe(no_mangle)]
#[cfg(not(feature = "o3ds"))]
extern "C" fn rp_seg_data_buf_free(data_buf: *const c_char) {
    unsafe { rp_data_buf_free(data_buf.sub((NWM_HDR_SIZE + ARQ_OVERHEAD_SIZE) as usize) as *mut _) }
}

#[cfg(not(feature = "o3ds"))]
const RP_KCP_TIMEOUT_TICK: s32 = 2 * SYSCLOCK_ARM11 as s32;

#[named]
#[cfg(not(feature = "o3ds"))]
unsafe fn do_kcp_thread_nwm() -> bool {
    let audio_enable = unsafe { entries::thread_audio::AUDIO_ENABLE };
    if let Some(mut lock) = NwmCbLock::lock(cname!()) {
        let mut dst = mem::MaybeUninit::uninit();
        let mut has_dst = false;

        while !reset_threads() {
            entries::thread_audio::drain_audio();
            let next_send_tick = unsafe { RP_OUTPUT_NEXT_TICK };

            if (get_system_tick().get() as u32 - next_send_tick) as s32 >= RP_KCP_TIMEOUT_TICK {
                // Reset KCP
                ns_dbg_print!(msg, c_str!("KCP timeout"));
                set_reset_threads();
                return false;
            }

            let (can_queue, send_delay) = {
                let cb = lock.get();
                let kcp = &mut cb.ikcp;
                let can_queue = unsafe { ikcp_queue_get_free(kcp) > 0 };
                let send_delay = unsafe { ikcp_send_ready_and_get_delay(kcp) };
                (can_queue, send_delay)
            };

            let timeout_ns = if send_delay >= 0 {
                if send_delay == 0 {
                    0
                } else {
                    let delay = (next_send_tick - get_system_tick().get() as u32
                        + ((send_delay - 1) as u32 * unsafe { MIN_SEND_INTERVAL_TICK }))
                        as s32;
                    if delay > 0 {
                        delay as s64 * 1_000_000_000 / SYSCLOCK_ARM11 as s64
                    } else {
                        0
                    }
                }
            } else {
                if send_delay < -0x10 {
                    // Reset KCP
                    ns_dbg_print!(failed, c_str!("KCP send"), send_delay);
                    set_reset_threads();
                    return false;
                }
                if audio_enable {
                    entries::thread_audio::AUDIO_POLL_NS
                } else {
                    THREAD_WAIT_NS.get()
                }
            };

            let relock_nwm = timeout_ns != 0;

            let mut acq_dst = |mut lock: Option<NwmCbLock>| -> Option<(NwmCbLock, bool)> {
                let relock = |lock: Option<NwmCbLock>| -> Option<NwmCbLock> {
                    if let Some(lock) = lock {
                        Some(lock)
                    } else if let Some(lock) = NwmCbLock::lock(cname!()) {
                        Some(lock)
                    } else {
                        None
                    }
                };

                if can_queue && !has_dst {
                    while !reset_threads() {
                        let res = unsafe {
                            rp_syn_acq(
                                &mut (*RELIABLE_STREAM_CB).nwm_syn,
                                timeout_ns,
                                dst.as_mut_ptr(),
                            )
                        };
                        if res == 0 {
                            has_dst = true;
                            break;
                        }
                        if res != RES_TIMEOUT as s32 {
                            ns_dbg_print!(wait_syn_failed, c_str!("Wait for nwm_syn"), res);
                            set_reset_threads();
                            return None;
                        }
                        if audio_enable {
                            entries::thread_audio::drain_audio();
                        }
                        if send_delay >= 0 {
                            break;
                        }
                        let mut lock_next = relock(lock)?;
                        if unsafe {
                            ptr::read_volatile(&lock_next.get().ikcp.session_new_data_received)
                        } {
                            lock = Some(lock_next);
                            break;
                        }
                        lock = Some(lock_next);
                    }
                } else if timeout_ns > 0 {
                    if send_delay < 0 {
                        if wait_syn_ns(
                            cname!(),
                            unsafe { RELIABLE_STREAM_CB_EVT },
                            c_str!("RELIABLE_STREAM_CB_EVT"),
                            DurationNs::init(timeout_ns as s64),
                        )
                        .is_none()
                        {
                            return None;
                        }
                        if !has_dst {
                            return Some((relock(lock)?, true));
                        }
                    } else {
                        sleep_thread(DurationNs::init(timeout_ns as s64));
                    }
                }
                Some((relock(lock)?, false))
            };

            if if relock_nwm {
                drop(lock);
                if let Some((lock_next, retry)) = acq_dst(None) {
                    lock = lock_next;
                    retry
                } else {
                    return false;
                }
            } else {
                if let Some((lock_next, retry)) = acq_dst(Some(lock)) {
                    lock = lock_next;
                    retry
                } else {
                    return false;
                }
            } {
                continue;
            }

            let kcp = &mut lock.get().ikcp;

            let mut dst_queued = false;
            if has_dst {
                let dst = unsafe { dst.assume_init() } as *mut u8;

                let mut size: u32 = 0;
                unsafe {
                    ptr::copy_nonoverlapping(
                        dst.sub(mem::size_of::<u32>()) as *const _,
                        &mut size,
                        1,
                    )
                };

                let ret = unsafe { ikcp_queue(kcp, dst, size as i32) };
                if ret < 0 {
                    // Reset KCP
                    ns_dbg_print!(failed, c_str!("KCP queue"), ret);
                    set_reset_threads();
                    return false;
                } else if ret == 0 {
                    has_dst = false;
                    dst_queued = true;
                }
            }

            // Ready send again
            let ret = unsafe { ikcp_send_ready_and_get_delay(kcp) };
            if ret < -0x10 && dst_queued {
                // Reset KCP
                ns_dbg_print!(failed, c_str!("KCP send ready"), ret);
                set_reset_threads();
                return false;
            }
            if ret < 0 {
                if (get_system_tick().get() as u32 - next_send_tick) as s32 >= RP_KCP_TIMEOUT_TICK {
                    // Reset KCP
                    ns_dbg_print!(failed, c_str!("KCP timeout"), ret);
                    set_reset_threads();
                    return false;
                }
            } else {
                // Send next
                let ret = unsafe { ikcp_send_next(kcp) };
                if ret < 0 {
                    // Reset KCP
                    // rp_udp_output 0x10 * ikcp_send_cur 0x10 * ikcp_send_next 0x10
                    if ret < -0x1000 {
                        mem::forget(lock);
                    } else {
                        ns_dbg_print!(failed, c_str!("KCP send next"), ret);
                    }
                    set_reset_threads();
                    return false;
                }

                if !(*kcp).session_established {
                    unsafe { RP_OUTPUT_NEXT_TICK = next_send_tick + SYSCLOCK_ARM11 / 16 };
                }
            }
        }

        true
    } else {
        set_reset_threads();
        false
    }
}

#[cfg(not(feature = "o3ds"))]
pub extern "C" fn kcp_thread_nwm(_: *mut c_void) {
    unsafe {
        __system_initSyscalls();
        while !reset_threads() && do_kcp_thread_nwm() {}
        svcExitThread()
    }
}

#[cfg(not(feature = "o3ds"))]
#[named]
pub extern "C" fn thread_nwm(_: *mut c_void) {
    unsafe {
        __system_initSyscalls();
        while !reset_threads() {
            entries::thread_audio::drain_audio();
            if send_next_buffer() {
                sleep_thread(MIN_SEND_INTERVAL_NS);
                continue;
            }
            if wait_syn(cname!(), SYN_HANDLES.nwm_ready, c_str!("nwm_ready")).is_none() {
                break;
            }
        }
        svcExitThread()
    }
}

#[cfg(not(feature = "o3ds"))]
#[named]
fn nwm_ready_acquire(w: WorkIndex) -> bool {
    let need_syn = unsafe { NWM_NEED_SYN.get_mut(&w) };
    if *need_syn {
        let audio_enable = unsafe { entries::thread_audio::AUDIO_ENABLE };
        if audio_enable {
            loop {
                if reset_threads() {
                    return false;
                }
                let res = wait_syn_ns(
                    cname!(),
                    unsafe { SYN_HANDLES.works.get(&w).nwm_ready },
                    c_str!("nwm_ready"),
                    DurationNs::init(entries::thread_audio::AUDIO_POLL_NS),
                );
                if res.is_none() {
                    return false;
                }
                if res == Some(true) {
                    break;
                }
                entries::thread_audio::drain_audio();
            }
        } else {
            if wait_syn(
                cname!(),
                unsafe { SYN_HANDLES.works.get(&w).nwm_ready },
                c_str!("nwm_ready"),
            )
            .is_none()
            {
                return false;
            }
        }
        *need_syn = false;
    }
    true
}

#[cfg(not(feature = "o3ds"))]
fn data_buffer_filled(dinfo: &DataInfo) -> (bool, *mut u8, u32) {
    let flag = dinfo.flag.load(Ordering::Acquire);
    let pos = dinfo.pos.load(Ordering::Acquire);
    (dinfo.send_pos < pos || flag > 0, pos, flag)
}

#[cfg(not(feature = "o3ds"))]
fn send_next_buffer() -> bool {
    let work_index = unsafe { NWM_WORK_INDEX };
    let mut thread_index = unsafe { NWM_THREAD_INDEX };

    loop {
        if !nwm_ready_acquire(work_index) {
            return false;
        }

        let ninfo = nwm_info(work_index).get(&thread_index);
        let dinfo = &ninfo.info;

        let (filled, pos, flag) = data_buffer_filled(dinfo);
        if !filled {
            return false;
        }

        if !do_send_next_buffer(unsafe { &mut NWM_WORK_INDEX }, thread_index, pos, flag) {
            return false;
        }

        if work_index != unsafe { ptr::read_volatile(&NWM_WORK_INDEX) } {
            return true;
        }

        let thread_index_next = unsafe { ptr::read_volatile(&NWM_THREAD_INDEX) };
        if thread_index != thread_index_next {
            thread_index = thread_index_next;
        }
    }
}

#[cfg(not(feature = "o3ds"))]
fn do_send_next_buffer(w: &mut WorkIndex, t: ThreadIndex, pos: *mut u8, flag: u32) -> bool {
    let curr_tick = get_system_tick().get() as u32;
    let next_tick = unsafe { RP_OUTPUT_NEXT_TICK };
    let tick_diff = next_tick as s32 - curr_tick as s32;

    if tick_diff > 0 {
        let sleep_value = DurationTick::init(tick_diff as s64).get_ns();
        sleep_thread(sleep_value);

        nwm_send_next_buffer(
            w,
            t,
            if NWM_AGGRESSIVE_NEXT_TICK > 0 {
                next_tick
            } else {
                get_system_tick().get() as u32
            },
            pos,
            flag,
        )
    } else {
        nwm_send_next_buffer(w, t, curr_tick, pos, flag)
    }
}

#[cfg(not(feature = "o3ds"))]
fn nwm_send_next_buffer(
    w: &mut WorkIndex,
    t: ThreadIndex,
    tick: u32,
    pos: *mut u8,
    flag: u32,
) -> bool {
    let core_count = core_count_in_use();
    let thread_index_last = thread_index_last(core_count);

    let winfo = nwm_info(*w);
    let ninfo = winfo.get_mut(&t);
    let dinfo = &ninfo.info;

    let send_pos = dinfo.send_pos;
    let data_buf = send_pos;

    let lossless = get_lossless_compression();
    let lossless_buf = if lossless {
        unsafe { data_buf.sub(RP_LOSSLESS_HDR_SIZE as usize) }
    } else {
        data_buf
    };
    let packet_buf = unsafe { lossless_buf.sub(DATA_HDR_SIZE as usize) };

    let size = unsafe { pos.offset_from_unsigned(send_pos) as u32 };
    let packet_data_size = get_packet_data_size() as u32;
    let size = cmp::min(size, packet_data_size);

    let thread_emptied = unsafe { send_pos.add(size as usize) } == pos;
    let thread_done = thread_emptied && flag > 0;

    if size < packet_data_size && !thread_done {
        return false;
    }

    let mut thread_end_index = t;
    let mut end_size = size;

    let mut thread_end_done = thread_done;

    if thread_done {
        thread_end_index.next_wrapped_n(&thread_index_last);
    }

    let mut total_size = size;

    if thread_done && thread_end_index.get() != 0 {
        loop {
            let ninfo = winfo.get_mut(&thread_end_index);
            let dinfo = &ninfo.info;

            let (filled, pos, flag) = data_buffer_filled(dinfo);
            if !filled {
                return false;
            }

            let data_buf_end = unsafe { data_buf.add(total_size as usize) };

            let send_pos = dinfo.send_pos;

            let remaining_size = packet_data_size - total_size;

            let size = unsafe { pos.offset_from_unsigned(send_pos) as u32 };
            let size = cmp::min(size, remaining_size);

            let thread_emptied = unsafe { send_pos.add(size as usize) } == pos;
            let thread_done = thread_emptied && flag > 0;

            if size < remaining_size && !thread_done {
                return false;
            }

            unsafe {
                ptr::copy_nonoverlapping(send_pos, data_buf_end, size as usize);
            }
            total_size += size;

            end_size = size;

            thread_end_done = thread_done;
            if thread_done {
                thread_end_index.next_wrapped_n(&thread_index_last);
                if thread_end_index.get() == 0 {
                    break;
                }
                continue;
            }
            break;
        }
    }

    let data_buf_hdr = unsafe { DATA_BUF_HDRS.get_mut(&w) };
    unsafe {
        ptr::copy(
            data_buf_hdr.0.as_ptr(),
            packet_buf as *mut u8,
            DATA_HDR_SIZE as usize,
        );
    }
    if thread_end_done && thread_end_index.get() == 0 {
        unsafe { *packet_buf.add(1) |= flag as u8 };
    }
    data_buf_hdr.0[3] += 1;

    if lossless {
        let lossless_data_hdr = unsafe { LOSSLESS_DATA_BUF_HDRS.get_mut(&w) };
        unsafe {
            ptr::copy(
                lossless_data_hdr.0.as_ptr(),
                lossless_buf as *mut u8,
                RP_LOSSLESS_HDR_SIZE as usize,
            );
            *lossless_buf.add(1) |= t.get() as u8 & 0x3;
        }
    }

    let packet_size = total_size + DATA_HDR_SIZE + if lossless { RP_LOSSLESS_HDR_SIZE } else { 0 };
    if unsafe { rp_output(packet_buf, packet_size as usize, tick) }.is_none() {
        return false;
    }

    if !thread_end_done {
        let send_pos = &mut winfo.get_mut(&thread_end_index).info.send_pos;
        *send_pos = unsafe { (*send_pos).add(end_size as usize) };
    }

    if thread_done {
        unsafe { NWM_THREAD_INDEX = thread_end_index };

        if thread_end_index.get() == 0 {
            nwm_done_release(w);
        }
    }

    true
}

pub unsafe fn rp_output(
    packet_buf: *mut u8,
    packet_size: usize,
    #[cfg(not(feature = "o3ds"))] tick: u32,
) -> Option<()> {
    let nwm_buf = unsafe { packet_buf.sub(NWM_HDR_SIZE as usize) };
    unsafe {
        nwm_output(nwm_buf, packet_size);
    }

    #[cfg(not(feature = "o3ds"))]
    unsafe {
        RP_OUTPUT_NEXT_TICK = tick
            + if NWM_PROPORTIONAL_MIN_INTERVAL > 0 {
                MIN_SEND_INTERVAL_TICK * packet_size as u32 / PACKET_SIZE
            } else {
                MIN_SEND_INTERVAL_TICK
            };
    }

    Some(())
}

// rp_output tail-calls nwmSendPacket, a call before this is true is a null jump
#[cfg(not(feature = "o3ds"))]
pub fn nwm_send_ready() -> bool {
    unsafe { nwmSendPacket.is_some() }
}

// advisory liveness gate for the audio thread; plain udp has no session so it
// stays true, kcp reports its handshake state (an unlocked single-field read)
#[cfg(not(feature = "o3ds"))]
pub fn nwm_session_alive() -> bool {
    match get_reliable_stream() {
        ReliableStream::None => true,
        ReliableStream::KCP => unsafe {
            RELIABLE_STREAM_CB_INITED.load(Ordering::Acquire)
                && (*RELIABLE_STREAM_CB).locked.ikcp.session_established
        },
    }
}

// only the nwm thread calls this: video through its send loop, audio drained
// from thread_audio's ring, so nwmSendPacket stays single-threaded, no lock
unsafe fn nwm_output(nwm_buf: *mut u8, packet_size: usize) {
    unsafe {
        ptr::copy_nonoverlapping(
            get_current_nwm_hdr().as_mut_ptr(),
            nwm_buf,
            NWM_HDR_SIZE as usize,
        );
        let nwm_size = init_udp_packet(nwm_buf, packet_size as u32);
        nwmSendPacket.unwrap_unchecked()(nwm_buf, nwm_size);
    }
}

unsafe fn init_udp_packet(nwm_buf: *mut u8, mut len: u32) -> u32 {
    unsafe {
        len += 8;
        *(nwm_buf.add(0x22 + 8) as *mut u16) = utils::htons(RP_SRC_PORT as u16); // src port
        *(nwm_buf.add(0x24 + 8) as *mut u16) =
            utils::htons(RP_CONFIG.dst_port().load(Ordering::Acquire) as u16); // dest port
        *(nwm_buf.add(0x26 + 8) as *mut u16) = utils::htons(len as u16);
        *(nwm_buf.add(0x28 + 8) as *mut u16) = 0; // no checksum
        len += 20;

        *(nwm_buf.add(0x10 + 8) as *mut u16) = utils::htons(len as u16);
        *(nwm_buf.add(0x12 + 8) as *mut u16) = 0xaf01; // packet id is a random value since we won't use the fragment
        *(nwm_buf.add(0x14 + 8) as *mut u16) = 0x0040; // no fragment
        *(nwm_buf.add(0x16 + 8) as *mut u16) = 0x1140; // ttl 64, udp

        *(nwm_buf.add(0x18 + 8) as *mut u16) = 0;
        *(nwm_buf.add(0x18 + 8) as *mut u16) = ip_checksum(nwm_buf.add(0xE + 8), 0x14);

        len += 22;
        *(nwm_buf.add(12) as *mut u16) = utils::htons(len as u16);
    }

    len
}

unsafe fn ip_checksum(data: *mut u8, mut length: usize) -> u16 {
    // Cast the data pointer to one that can be indexed.
    // Initialise the accumulator.
    let mut acc: u32 = 0;

    if length % 2 != 0 {
        unsafe { *data.add(length) = 0 };
        length += 1;
    }

    length /= 2;
    let data = data as *mut u16;

    // Handle complete 16-bit blocks.
    for i in 0..length {
        acc += utils::ntohs(unsafe { *data.add(i) }) as u32;
    }
    acc = (acc & 0xffff) + (acc >> 16);
    acc += acc >> 16;

    // Return the checksum in network byte order.
    utils::htons(!acc as u16)
}

#[allow(unused_macros)]
#[named]
unsafe fn init_reliable_stream(flags: u32, qos: u32) -> Option<()> {
    #[cfg(not(feature = "o3ds"))]
    let mut nwm_lock = if let Some(l) = NwmCbLock::lock(cname!()) {
        l
    } else {
        return None;
    };

    unsafe {
        let reliable_stream = flags & RP_CONFIG_FLAG_RELIABLE_STREAM > 0;
        RELIABLE_STREAM.store(reliable_stream, Ordering::Release);

        #[cfg(not(feature = "o3ds"))]
        RELIABLE_STREAM_DELTA_PROG.store(
            reliable_stream && flags & RP_CONFIG_FLAG_RELIABLE_STREAM_DELTA > 0,
            Ordering::Release,
        );

        MAX_QOS = qos;

        set_packet_data_size();

        match get_reliable_stream() {
            ReliableStream::None => {}

            #[cfg(not(feature = "o3ds"))]
            ReliableStream::KCP => {
                let kcp = &mut nwm_lock.get().ikcp;

                if ikcp_create(kcp, KCP_CONV as u16) < 0 {
                    return None;
                }
                let sndwnd = ((ARQ_BUFS_COUNT * qos + RP_QOS_MAX / 2) / RP_QOS_MAX) as i32;
                let curwnd = ((ARQ_CUR_BUFS_COUNT * qos + RP_QOS_MAX / 2) / RP_QOS_MAX) as i32;
                if ikcp_wndsize(kcp, sndwnd, curwnd) != 0 {
                    return None;
                }

                KCP_CONV += 1;
            }
        }
    }

    Some(())
}

unsafe fn init_lossless_compression(flags: u32) {
    unsafe {
        let lossless_compression = flags & RP_CONFIG_FLAG_LOSSLESS > 0;
        LOSSLESS_COMPRESSION.store(lossless_compression, Ordering::Release);
        let bias = (flags & RP_CONFIG_FLAG_LOSSLESS_DATA) >> RP_CONFIG_FLAG_LOSSLESS_DATA_SHIFT;
        LOSSLESS_COMPRESSION_BIAS.store(bias as u8, Ordering::Release);
    }
}

static mut MIN_SEND_INTERVAL_TICK: u32 = const_default();
pub static mut MIN_SEND_INTERVAL_NS: DurationNs = const_default();

pub unsafe fn init_min_send_interval(qos: u32) {
    unsafe {
        (*config_consts::OV_STATS).kcp_qos = qos;
        CURRENT_QOS.store(qos, Ordering::Release);
        let tick = (SYSCLOCK_ARM11 as u64 * PACKET_SIZE as u64) / qos as u64;
        MIN_SEND_INTERVAL_TICK = tick as u32;
        MIN_SEND_INTERVAL_NS = DurationTick::init(tick as s64).get_ns();
    }
}

#[cfg(not(feature = "o3ds"))]
static mut NWM_WORK_INDEX: WorkIndex = WorkIndex::init(0);
#[cfg(not(feature = "o3ds"))]
static mut NWM_THREAD_INDEX: ThreadIndex = ThreadIndex::init(0);

#[cfg(not(feature = "o3ds"))]
static mut NWM_NEED_SYN: RangedArray<bool, WORK_COUNT> = const_default();
#[cfg(not(feature = "o3ds"))]
static mut CUR_SEG_MEM_COUNT: u32 = 0;

pub static mut RP_OUTPUT_NEXT_TICK: u32 = const_default();

pub unsafe fn init(dst_flags: u32, qos: u32) -> Option<()> {
    unsafe {
        init_lossless_compression(dst_flags);
        init_reliable_stream(dst_flags, qos)?;
        init_min_send_interval(qos);

        #[cfg(not(feature = "o3ds"))]
        {
            for i in WorkIndex::all() {
                *NWM_NEED_SYN.get_mut(&i) = true;
            }
            NWM_WORK_INDEX = WorkIndex::init(0);
            NWM_THREAD_INDEX = ThreadIndex::init(0);

            RP_OUTPUT_NEXT_TICK = get_system_tick().get() as u32 + MIN_SEND_INTERVAL_TICK;
            CUR_SEG_MEM_COUNT = 0;
        }
        RP_OUTPUT_NEXT_TICK = get_system_tick().get() as u32 + MIN_SEND_INTERVAL_TICK;
    }

    Some(())
}

#[derive(PartialEq, Eq)]
pub enum ReliableStream {
    None,

    #[cfg(not(feature = "o3ds"))]
    KCP,
}

static mut RELIABLE_STREAM: AtomicBool = const_default();
#[cfg(not(feature = "o3ds"))]
static mut RELIABLE_STREAM_DELTA_PROG: AtomicBool = const_default();

static mut LOSSLESS_COMPRESSION: AtomicBool = const_default();
static mut LOSSLESS_COMPRESSION_BIAS: AtomicU8 = const_default();

pub fn get_reliable_stream() -> ReliableStream {
    #[cfg(not(feature = "o3ds"))]
    if unsafe { RELIABLE_STREAM.load(Ordering::Acquire) } {
        ReliableStream::KCP
    } else {
        ReliableStream::None
    }
    #[cfg(feature = "o3ds")]
    ReliableStream::None
}

#[cfg(not(feature = "o3ds"))]
pub fn get_reliable_stream_delta_prog() -> bool {
    unsafe { RELIABLE_STREAM_DELTA_PROG.load(Ordering::Acquire) }
}

pub fn get_lossless_compression() -> bool {
    unsafe { LOSSLESS_COMPRESSION.load(Ordering::Acquire) }
}

pub fn get_lossless_compression_bias() -> u8 {
    unsafe { LOSSLESS_COMPRESSION_BIAS.load(Ordering::Acquire) }
}

#[cfg(not(feature = "o3ds"))]
#[derive(ConstDefault)]
pub struct DataInfo {
    pub send_pos: *mut u8,
    pub pos: AtomicPtr<u8>,
    pub flag: AtomicU32,
}

#[cfg(not(feature = "o3ds"))]
#[derive(ConstDefault)]
pub struct NwmThreadInfo {
    pub buf: *mut u8,
    pub buf_packet_last: *mut u8,
    pub info: DataInfo,
}

#[cfg(not(feature = "o3ds"))]
pub type NwmWorkInfo = RangedArray<NwmThreadInfo, RP_CORE_COUNT_MAX>;
#[cfg(not(feature = "o3ds"))]
pub type NwmInfo = RangedArray<NwmWorkInfo, WORK_COUNT>;
#[cfg(not(feature = "o3ds"))]
static mut NWM_INFOS: NwmInfo = const_default();
static mut PACKET_DATA_SIZE: usize = 0;

pub fn get_packet_data_size() -> usize {
    unsafe { PACKET_DATA_SIZE }
}

pub const fn get_packet_data_size_v(rel_stream: bool) -> usize {
    if rel_stream {
        get_packet_data_size_const::<true>()
    } else {
        get_packet_data_size_const::<false>()
    }
}

pub const fn get_packet_data_size_const<const REL_STREAM: bool>() -> usize {
    if REL_STREAM {
        ARQ_RP_DATA_SIZE as usize
    } else {
        RP_DATA_SIZE as usize
    }
}

pub const fn get_lossless_packet_data_size_v(rel_stream: bool) -> usize {
    if rel_stream {
        get_lossless_packet_data_size_const::<true>()
    } else {
        get_lossless_packet_data_size_const::<false>()
    }
}

pub const fn get_lossless_packet_data_size_const<const REL_STREAM: bool>() -> usize {
    if REL_STREAM {
        ARQ_RP_DATA_SIZE as usize
    } else {
        RP_LOSSLESS_DATA_SIZE as usize
    }
}

unsafe fn set_packet_data_size() {
    unsafe {
        PACKET_DATA_SIZE = if get_lossless_compression() {
            match get_reliable_stream() {
                ReliableStream::None => get_lossless_packet_data_size_v(false),
                #[cfg(not(feature = "o3ds"))]
                ReliableStream::KCP => get_lossless_packet_data_size_v(true),
            }
        } else {
            match get_reliable_stream() {
                ReliableStream::None => get_packet_data_size_v(false),
                #[cfg(not(feature = "o3ds"))]
                ReliableStream::KCP => get_packet_data_size_v(true),
            }
        }
    }
}

#[cfg(not(feature = "o3ds"))]
pub unsafe fn init_nwm_infos(nwm_bufs: &entries::thread_main::NwmBufs, core_count: CoreCount) {
    let packet_data_size = get_packet_data_size();
    let lossless = get_lossless_compression();
    let packet_size = if lossless {
        packet_data_size + RP_LOSSLESS_HDR_SIZE as usize
    } else {
        packet_data_size
    };

    unsafe {
        for i in WorkIndex::all() {
            for j in ThreadIndex::up_to(&thread_index_last(core_count)) {
                let ninfo = NWM_INFOS.get_mut(&i).get_mut(&j);
                let buf_size = (entries::thread_main::NWM_BUFFER_SIZE / core_count.get() as usize
                    - RP_CB_HDR_SIZE)
                    / packet_size
                    * packet_size
                    + RP_CB_HDR_SIZE;
                let buf = nwm_bufs.get(&i).add(j.get() as usize * buf_size);

                ninfo.buf = buf.add(
                    RP_CB_HDR_SIZE
                        + if lossless {
                            RP_LOSSLESS_HDR_SIZE as usize
                        } else {
                            0
                        },
                );
                ninfo.buf_packet_last = buf.add(buf_size as usize - packet_data_size);
            }
        }
    }
}

#[cfg(not(feature = "o3ds"))]
pub fn nwm_info(work_index: WorkIndex) -> &'static mut NwmWorkInfo {
    unsafe { NWM_INFOS.get_mut(&work_index) }
}

const RP_CB_HDR_SIZE: usize = encoder::jround_up(
    NWM_HDR_SIZE as usize + DATA_HDR_SIZE as usize,
    mem::size_of::<usize>(),
);

#[cfg(feature = "o3ds")]
pub unsafe fn nwm_info() -> *mut u8 {
    unsafe { (*RELIABLE_STREAM_CB).buf.as_mut_ptr().add(RP_CB_HDR_SIZE) }
}

#[derive(ConstDefault)]
struct DataHdr([u8; DATA_HDR_SIZE as usize]);
static mut DATA_BUF_HDRS: RangedArray<DataHdr, WORK_COUNT> = const_default();

impl DataHdr {
    fn init(frame_id: u8, is_top: bool, downsample: u8, lossless: u8) -> Self {
        Self([
            frame_id,
            is_top as u8,
            2 | downsample << 2 | lossless as u8,
            0,
        ])
    }
}

pub fn rp_lossless_huffman_hdr(is_table: bool, huff_tbl_no: u8) -> u8 {
    is_table as u8 | ((huff_tbl_no & 0x7) << 1)
}

#[derive(ConstDefault)]
pub struct LosslessDataHdr([u8; RP_LOSSLESS_HDR_SIZE as usize]);
pub static mut LOSSLESS_DATA_BUF_HDRS: RangedArray<LosslessDataHdr, WORK_COUNT> = const_default();

impl LosslessDataHdr {
    pub fn init(huff_tbl_no: u8, chroma_ss: u8, color_bias: u8, rest_int: u8) -> Self {
        Self([
            rp_lossless_huffman_hdr(false, huff_tbl_no)
                | ((chroma_ss & 0x3) << 4)
                | ((color_bias & 0x3) << 6),
            (rest_int & 0xf) << 2,
        ])
    }
}

#[named]
#[cfg(not(feature = "o3ds"))]
fn nwm_done_release(w: &mut WorkIndex) {
    unsafe {
        release_sem(
            cname!(),
            SYN_HANDLES.works.get(w).nwm_done,
            c_str!("nwm_done"),
        );

        *NWM_NEED_SYN.get_mut(w) = true;
        w.next_wrapped();
    }
}

#[named]
#[cfg(not(feature = "o3ds"))]
pub unsafe fn nwm_done_acquire(w: WorkIndex, frame_id: u8, is_top: bool, downsample: u8) -> bool {
    unsafe {
        if wait_syn(
            cname!(),
            SYN_HANDLES.works.get(&w).nwm_done,
            c_str!("nwm_done"),
        )
        .is_none()
        {
            return false;
        }

        let ninfo = NWM_INFOS.get_mut(&w);
        for j in ThreadIndex::up_to(&thread_index_last(core_count_in_use())) {
            let ninfo = ninfo.get_mut(&j);
            let info = &mut ninfo.info;
            let buf = ninfo.buf;

            *info = DataInfo {
                send_pos: buf,
                pos: AtomicPtr::new(buf),
                flag: AtomicU32::new(0),
            };
        }

        let hdr = DATA_BUF_HDRS.get_mut(&w);
        let lossless = get_lossless_compression();
        *hdr = DataHdr::init(frame_id, is_top, downsample, lossless as u8);

        true
    }
}

#[cfg(feature = "o3ds")]
pub unsafe fn nwm_start_frame(frame_id: u8, is_top: bool, downsample: u8) {
    unsafe {
        let hdr = DATA_BUF_HDRS.get_mut(&WorkIndex::init(0));
        let lossless = get_lossless_compression();
        *hdr = DataHdr::init(frame_id, is_top, downsample, lossless as u8);
    }
}

#[named]
#[cfg(not(feature = "o3ds"))]
pub unsafe fn nwm_ready_release(w: &WorkIndex) {
    unsafe {
        release_sem(
            cname!(),
            SYN_HANDLES.works.get(&w).nwm_ready,
            c_str!("nwm_ready"),
        )
    }
}

#[cfg(not(feature = "o3ds"))]
#[derive(PartialEq, Eq)]
pub struct NwmCbLock(CName);

#[cfg(not(feature = "o3ds"))]
impl NwmCbLock {
    pub fn lock(cname: CName) -> Option<Self> {
        nwm_cb_lock()?;
        Some(Self(cname))
    }

    pub fn get(&mut self) -> &mut rp_cb_locked {
        unsafe { &mut (*RELIABLE_STREAM_CB).locked }
    }
}

#[cfg(not(feature = "o3ds"))]
impl Drop for NwmCbLock {
    fn drop(&mut self) {
        unsafe { nwm_cb_unlock(self.0) };
    }
}

#[cfg(not(feature = "o3ds"))]
#[named]
fn nwm_cb_lock() -> Option<()> {
    wait_syn(
        cname!(),
        unsafe { RELIABLE_STREAM_CB_LOCK },
        c_str!("RELIABLE_STREAM_CB_LOCK"),
    )?;
    Some(())
}

#[cfg(not(feature = "o3ds"))]
unsafe fn nwm_cb_unlock(cname: CName) {
    unsafe {
        release_mutex(
            cname,
            RELIABLE_STREAM_CB_LOCK,
            c_str!("RELIABLE_STREAM_CB_LOCK"),
        );
    }
}

static mut RP_FRAME_COMPRESSED_SIZE: [AtomicU32; WORK_COUNT as usize] = const_default();

#[cfg(not(feature = "o3ds"))]
pub const JPEG_COMP_COUNT_SIZE_NBITS: u32 = 19;
#[cfg(not(feature = "o3ds"))]
pub const JPEG_COMP_COUNT_BLKN_NBITS: u32 = 13;

#[cfg(not(feature = "o3ds"))]
const _JPEG_COMP_COUNT_NBITS_ASSERT: () = {
    assert!(JPEG_COMP_COUNT_SIZE_NBITS + JPEG_COMP_COUNT_BLKN_NBITS <= u32::BITS);
};

#[cfg(not(feature = "o3ds"))]
pub fn rp_dq_update_size(comp_size: &mut AtomicU32, size: u32, blkn: u16) {
    let mut curr = comp_size.load(Ordering::Acquire);
    loop {
        let prev_size = curr & ((1 << JPEG_COMP_COUNT_SIZE_NBITS) - 1);
        let prev_blkn =
            (curr >> JPEG_COMP_COUNT_SIZE_NBITS) & ((1 << JPEG_COMP_COUNT_BLKN_NBITS) - 1);

        let next_size = prev_size + size;
        let next_blkn = prev_blkn + blkn as u32;
        let next = (next_size & ((1 << JPEG_COMP_COUNT_SIZE_NBITS) - 1))
            | ((next_blkn & ((1 << JPEG_COMP_COUNT_BLKN_NBITS) - 1)) << JPEG_COMP_COUNT_SIZE_NBITS);

        match comp_size.compare_exchange_weak(curr, next, Ordering::AcqRel, Ordering::Acquire) {
            Ok(_) => break,
            Err(temp) => curr = temp,
        }
    }
}

#[cfg(not(feature = "o3ds"))]
pub fn rp_update_size(w: WorkIndex, size: u32) {
    unsafe {
        RP_FRAME_COMPRESSED_SIZE
            .get_unchecked_mut(w.get() as usize)
            .fetch_add(size, Ordering::AcqRel);
    }
}

#[cfg(not(feature = "o3ds"))]
pub fn rp_get_size(w: WorkIndex) -> u32 {
    unsafe {
        RP_FRAME_COMPRESSED_SIZE
            .get_unchecked_mut(w.get() as usize)
            .load(Ordering::Acquire)
    }
}

pub fn rp_clear_size(w: WorkIndex) {
    unsafe {
        RP_FRAME_COMPRESSED_SIZE
            .get_unchecked_mut(w.get() as usize)
            .store(0, Ordering::Release)
    }
}

#[cfg(feature = "o3ds")]
pub unsafe fn rp_send_buffer(dst: &mut encoder::WorkerDst, term: bool) -> bool {
    let mut size = get_packet_data_size();
    const TERM_FLAG: u8 = 0x10;
    if term {
        size -= dst.free_in_bytes as usize;
    }

    let lossless = get_lossless_compression();

    let data_buf = unsafe { dst.dst.sub(size) };
    let lossless_buf = if lossless {
        unsafe { data_buf.sub(RP_LOSSLESS_HDR_SIZE as usize) }
    } else {
        data_buf
    };
    let packet_buf = unsafe { lossless_buf.sub(DATA_HDR_SIZE as usize) };
    let data_buf_hdr = unsafe { DATA_BUF_HDRS.get_mut(&WorkIndex::init(0)) };
    unsafe {
        ptr::copy(
            data_buf_hdr.0.as_ptr(),
            packet_buf as *mut u8,
            DATA_HDR_SIZE as usize,
        );
    }
    if lossless {
        let lossless_data_hdr = unsafe { LOSSLESS_DATA_BUF_HDRS.get_mut(&WorkIndex::init(0)) };
        unsafe {
            ptr::copy(
                lossless_data_hdr.0.as_ptr(),
                lossless_buf as *mut u8,
                RP_LOSSLESS_HDR_SIZE as usize,
            );
        }
    }
    if term {
        unsafe { *packet_buf.add(1) |= TERM_FLAG };
    }
    data_buf_hdr.0[3] += 1;

    let packet_size = size as u32 + DATA_HDR_SIZE + if lossless { RP_LOSSLESS_HDR_SIZE } else { 0 };
    if unsafe { rp_output(packet_buf, packet_size as usize) }.is_none() {
        return false;
    }

    dst.dst = unsafe { nwm_info().add(if lossless { RP_LOSSLESS_HDR_SIZE } else { 0 } as usize) };
    dst.free_in_bytes = get_packet_data_size() as u16;

    true
}

#[named]
#[cfg(not(feature = "o3ds"))]
pub unsafe fn rp_send_buffer(dst: &mut encoder::WorkerDst, term: bool, rel_stream: bool) -> bool {
    let rp_packet_data_size = get_packet_data_size();
    let mut size = rp_packet_data_size;
    const TERM_FLAG: u8 = 0x10;
    if term {
        size -= dst.free_in_bytes as usize;
    }

    dst.dst = if !rel_stream {
        let ninfo = unsafe { dst.user.ninfo };
        let ninfo = unsafe { &*ninfo };
        let dinfo = &ninfo.info;

        let pos = dinfo.pos.fetch_ptr_add(size, Ordering::AcqRel);
        let mut pos_next = unsafe { pos.add(size) };
        if term {
            dinfo.flag.store(TERM_FLAG as u32, Ordering::Release);
        }

        if !term && pos_next > ninfo.buf_packet_last {
            pos_next = ninfo.buf_packet_last;
            ns_dbg_print!(msg, c_str!("Send buffer overflow"));
        }

        let res = unsafe { svcSignalEvent(SYN_HANDLES.nwm_ready) };
        if res != 0 {
            ns_dbg_print!(failed, c_str!("Signal nwm ready"), res);
        }

        pos_next
    } else {
        let hdr = unsafe { dst.user.kcp_hdr };
        let mut dst = if term {
            unsafe {
                dst.dst
                    .sub(rp_packet_data_size - dst.free_in_bytes as usize)
            }
        } else {
            unsafe { dst.dst.sub(rp_packet_data_size) }
        };

        let mut size = size as u32;
        if !term {
            dst = unsafe { dst.sub(ARQ_DATA_HDR_SIZE as usize) };
            size += ARQ_DATA_HDR_SIZE;
            unsafe {
                hdr.write_hdr(dst);
            }
        }

        unsafe {
            ptr::copy_nonoverlapping(&size, dst.sub(mem::size_of::<u32>()) as *mut _, 1);
        }

        if term {
            return unsafe { entries::work_thread::set_term_dst(dst, hdr.w, hdr.t) };
        } else {
            let cb = unsafe { &mut *RELIABLE_STREAM_CB };
            while !reset_threads() {
                let res = unsafe { rp_syn_rel1(&mut cb.nwm_syn, dst as *mut _) };
                if res == 0 {
                    break;
                }
                if res != RES_TIMEOUT as s32 {
                    ns_dbg_print!(failed, c_str!("Release nwm_syn"), res);
                    set_reset_threads();
                    return false;
                }
            }

            if let Some(dst) = unsafe { rp_data_buf_malloc() } {
                rp_data_buf_data(dst)
            } else {
                return false;
            }
        }
    };
    dst.free_in_bytes = rp_packet_data_size as u16;
    true
}

#[named]
#[cfg(not(feature = "o3ds"))]
pub unsafe fn rp_data_buf_malloc() -> Option<*mut c_char> {
    unsafe {
        wait_syn(cname!(), SEG_MEM_SEM, c_str!("SEG_MEM_SEM"))?;
        entries::work_thread::rp_term_data_buf_malloc()
    }
}

#[named]
#[cfg(not(feature = "o3ds"))]
unsafe fn rp_data_buf_free(dst: *const ::libc::c_char) {
    unsafe {
        entries::work_thread::rp_term_data_buf_free_base(dst);
        release_sem(cname!(), SEG_MEM_SEM, c_str!("SEG_MEM_SEM"));
    }
}

#[cfg(not(feature = "o3ds"))]
pub fn rp_data_buf_data(dst: *mut c_char) -> *mut c_char {
    unsafe { dst.add((NWM_HDR_SIZE + ARQ_OVERHEAD_SIZE + ARQ_DATA_HDR_SIZE) as usize) }
}
