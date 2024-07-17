use crate::*;

pub struct ThreadVars(());

impl ThreadVars {}

pub struct ThreadsStacks<'a> {
    aux1: &'a mut StackRegion<{ RP_THREAD_STACK_SIZE as usize }>,
    aux2: &'a mut StackRegion<{ RP_THREAD_STACK_SIZE as usize }>,
    nwm: &'a mut StackRegion<{ STACK_SIZE as usize }>,
    screen: &'a mut StackRegion<{ STACK_SIZE as usize }>,
    nwm_bufs: [*mut u8; WORK_COUNT as usize],
}

mod first_time_init {
    use super::*;

    unsafe fn init_jpeg_compress() -> Option<()> {
        let jpeg_mem = request_mem_from_pool::<{ mem::size_of::<crate::jpeg::Jpeg>() }>()?;
        crate::entries::work_thread::set_jpeg(jpeg_mem.0.assume_init_mut());

        Some(())
    }

    unsafe fn create_event(h: *mut Handle) -> Result {
        svcCreateEvent(h, RESET_ONESHOT)
    }

    #[named]
    pub unsafe fn entry<'a>() -> Option<ThreadsStacks<'a>> {
        let res;
        __system_initSyscalls();
        res = gspInit(1);
        if res != 0 {
            nsDbgPrint!(gspInitFailed, res);
            return None;
        }

        let mut nwm_bufs: [*mut u8; WORK_COUNT as usize] = const_default();

        if let Some(m) = request_mem_from_pool::<{ mem::size_of::<rp_cb>() }>() {
            reliable_stream_cb = m.to_ptr() as *mut rp_cb;
            let cb = &mut *reliable_stream_cb;
            let m = (cb.send_bufs).as_mut_ptr().as_mut_ptr();
            for i in WorkIndex::all() {
                *i.index_into_mut(&mut nwm_bufs) = m.add(NWM_BUFFER_SIZE * i.get() as usize);
            }

            cb.nwm_syn.sem = 0;
            cb.nwm_syn.mutex = 0;
        } else {
            return None;
        }

        for i in ScreenIndex::all() {
            for j in crate::entries::thread_screen::ImgWorkIndex::all() {
                if let Some(m) = request_mem_from_pool::<IMG_BUFFER_SIZE>() {
                    crate::entries::thread_screen::init_img_info(&i, &j, m);
                } else {
                    return None;
                }
            }
        }

        if init_jpeg_compress() == None {
            nsDbgPrint!(initJpegFailed);
            return None;
        }

        if let Some(m) =
            request_mem_from_pool::<{ align_to_page_size(mem::size_of::<SynHandles>()) }>()
        {
            syn_handles = m.to_ptr() as *mut SynHandles;
        } else {
            return None;
        }

        for i in ScreenIndex::all() {
            let res = create_event((*syn_handles).port_screen_ready.get_mut(&i));
            if res != 0 {
                nsDbgPrint!(createPortEventFailed, res);
                return None;
            }
        }

        let res = create_event(&mut (*syn_handles).nwm_ready);
        if res != 0 {
            nsDbgPrint!(createNwmEventFailed, res);
            return None;
        }

        let mut svc_thread = mem::MaybeUninit::uninit();
        let res = create_thread_from_pool::<{ SMALL_STACK_SIZE as usize }>(
            svc_thread.as_mut_ptr(),
            Some(handlePortThread),
            SVC_PORT_NWM.as_ptr() as u32_,
            0x10,
            1,
        )
        .0;
        if res != 0 {
            nsDbgPrint!(createNwmSvcFailed, res);
        }

        let res = svcCreateMutex(&mut reliable_stream_cb_lock, false);
        if res != 0 {
            nsDbgPrint!(createNwmMutexFailed, res);
            return None;
        }
        let res = create_event(&mut reliable_stream_cb_evt);
        if res != 0 {
            nsDbgPrint!(createNwmRecvEventFailed, res);
            return None;
        }

        let cb = &mut (*reliable_stream_cb);
        if mp_init(
            (*cb.recv_bufs.as_ptr()).len(),
            cb.recv_bufs.len(),
            cb.recv_bufs.as_mut_ptr().as_mut_ptr() as *mut _,
            &mut cb.recv_pool,
        ) < 0
        {
            nsDbgPrint!(mpInitFailed, c_str!("recv_pool"));
            return None;
        }
        let recv_bufs_len = cb.recv_bufs.len() as i32;
        let res = svcCreateSemaphore(&mut recv_seg_mem_sem, recv_bufs_len, recv_bufs_len);
        if res != 0 {
            nsDbgPrint!(createSemaphoreFailed, c_str!("recv_seg_mem_sem"), res);
            return None;
        }
        let res = svcCreateMutex(&mut recv_seg_mem_lock, false);
        if res != 0 {
            nsDbgPrint!(createMutexFailed, c_str!("recv_seg_mem_lock"), res);
            return None;
        }
        recv_seg_mem_inited.store(true, Ordering::Release);

        let aux1Stack = request_mem_from_pool::<{ RP_THREAD_STACK_SIZE as usize }>()?;
        let aux2Stack = request_mem_from_pool::<{ RP_THREAD_STACK_SIZE as usize }>()?;
        let nwmStack = request_mem_from_pool::<{ STACK_SIZE as usize }>()?;
        let screenStack = request_mem_from_pool::<{ STACK_SIZE as usize }>()?;

        Some(ThreadsStacks {
            aux1: stack_region_from_mem_region(aux1Stack),
            aux2: stack_region_from_mem_region(aux2Stack),
            nwm: stack_region_from_mem_region(nwmStack),
            screen: stack_region_from_mem_region(screenStack),
            nwm_bufs,
        })
    }
}

mod loop_main {
    use super::*;

    fn core_count() -> CoreCount {
        crate::entries::work_thread::get_core_count_in_use()
    }

    unsafe fn set_core_count(v: u32_) {
        crate::entries::work_thread::set_core_count_in_use(v);
    }

    struct Config(());

    macro_rules! config_ar {
        ($v:ident) => {
            AtomicU32::from_mut(&mut (*rp_config).$v).load(Ordering::Relaxed)
        };
    }

    macro_rules! set_config_ar {
        ($v:ident, $n:ident) => {
            AtomicU32::from_mut(&mut (*rp_config).$v).store($n, Ordering::Relaxed)
        };
    }

    impl Config {
        unsafe fn core_count_ar(&self) -> u32_ {
            config_ar!(coreCount)
        }

        unsafe fn mode_ar(&self) -> u32_ {
            config_ar!(mode)
        }

        unsafe fn set_core_count_ar(&self, v: u32_) {
            set_config_ar!(coreCount, v)
        }

        unsafe fn dst_port_ar(&self) -> u32_ {
            config_ar!(dstPort)
        }

        unsafe fn set_dst_port_ar(&self, v: u32_) {
            set_config_ar!(dstPort, v)
        }

        unsafe fn qos_ar(&self) -> u32_ {
            config_ar!(qos)
        }

        unsafe fn quality_ar(&self) -> u32_ {
            config_ar!(quality)
        }

        unsafe fn thread_prio_ar(&self) -> u32_ {
            config_ar!(threadPriority)
        }
    }

    struct InitVars {
        core_count: CoreCount,
        thread_prio: u32_,
    }

    struct InitCleanup(InitVars);

    impl InitCleanup {
        #[named]
        unsafe fn init(v: InitVars) -> Option<Self> {
            let res = svcCreateSemaphore(&mut (*syn_handles).screen_ready, 1, 1);
            if res != 0 {
                nsDbgPrint!(createSemaphoreFailed, c_str!("screen_ready"), res);
                return None;
            }

            for i in WorkIndex::all() {
                let work = (*syn_handles).works.get_mut(&i);

                let res = svcCreateSemaphore(&mut work.work_done, 1, 1);
                if res != 0 {
                    nsDbgPrint!(createSemaphoreFailed, c_str!("work_done"), res);
                    return None;
                }

                let res = svcCreateSemaphore(&mut work.nwm_done, 1, 1);
                if res != 0 {
                    nsDbgPrint!(createSemaphoreFailed, c_str!("nwm_done"), res);
                    return None;
                }

                let res = svcCreateSemaphore(&mut work.nwm_ready, 0, 1);
                if res != 0 {
                    nsDbgPrint!(createSemaphoreFailed, c_str!("nwm_ready"), res);
                    return None;
                }

                *work.work_begin_flag.as_ptr() = false;
                *work.work_done_count.as_ptr() = 0;
            }

            for j in ThreadId::up_to(&v.core_count) {
                let thread = (*syn_handles).threads.get_mut(&j);

                let res = svcCreateSemaphore(&mut thread.work_ready, 0, WORK_COUNT as i32);
                if res != 0 {
                    nsDbgPrint!(createSemaphoreFailed, c_str!("work_ready"), res);
                    return None;
                }

                let res = svcCreateSemaphore(&mut thread.work_begin_ready, 0, WORK_COUNT as i32);
                if res != 0 {
                    nsDbgPrint!(createSemaphoreFailed, c_str!("work_begin_ready"), res);
                    return None;
                }
            }

            let send_bufs_len = (*reliable_stream_cb).send_bufs.len() as i32;
            let res = svcCreateSemaphore(&mut seg_mem_sem, send_bufs_len, send_bufs_len);
            if res != 0 {
                nsDbgPrint!(createSemaphoreFailed, c_str!("seg_mem_sem"), res);
                return None;
            }
            let res = svcCreateMutex(&mut seg_mem_lock, false);
            if res != 0 {
                nsDbgPrint!(createMutexFailed, c_str!("seg_mem_lock"), res);
                return None;
            }

            Some(Self(v))
        }
    }

    impl Drop for InitCleanup {
        fn drop(&mut self) {
            unsafe {
                let _ = svcCloseHandle(seg_mem_lock);
                let _ = svcCloseHandle(seg_mem_sem);

                for j in ThreadId::up_to(&self.0.core_count) {
                    let thread = (*syn_handles).threads.get_mut(&j);

                    let _ = svcCloseHandle(thread.work_begin_ready);
                    let _ = svcCloseHandle(thread.work_ready);
                }

                for i in WorkIndex::all() {
                    let work = (*syn_handles).works.get_mut(&i);

                    let _ = svcCloseHandle(work.nwm_ready);
                    let _ = svcCloseHandle(work.nwm_done);
                    let _ = svcCloseHandle(work.work_done);
                }

                let _ = svcCloseHandle((*syn_handles).screen_ready);
            }
        }
    }

    #[named]
    unsafe fn reset_init(nwm_bufs: &[*mut u8; WORK_COUNT as usize]) -> Option<InitCleanup> {
        crate::entries::work_thread::clear_reset_threads_ar();

        let config = Config(());

        set_core_count(config.core_count_ar());
        let core_count = core_count();
        config.set_core_count_ar(core_count.get());

        let mode = config.mode_ar();
        crate::entries::thread_screen::reset_thread_vars(mode);

        let dst_port = config.dst_port_ar();
        let dst_flags = dst_port & 0xffff0000;
        let dst_port = dst_port & 0xffff;
        if dst_port == 0 {
            config.set_dst_port_ar(RP_DST_PORT_DEFAULT | dst_flags)
        }

        let qos = config.qos_ar();

        let thread_prio = config.thread_prio_ar();
        let res = svcSetThreadPriority(thread_main_handle, thread_prio as i32);
        if res != 0 {
            nsDbgPrint!(setThreadPriorityFailed, res);
        }

        let vars = InitVars {
            core_count,
            thread_prio,
        };
        let jpeg = crate::entries::work_thread::get_jpeg();
        jpeg.reset(config.quality_ar(), vars.core_count);

        let cb = &mut *reliable_stream_cb;
        if mp_init(
            (*cb.send_bufs.as_ptr()).len(),
            cb.send_bufs.len(),
            cb.send_bufs.as_mut_ptr().as_mut_ptr() as *mut _,
            &mut cb.send_pool,
        ) < 0
        {
            nsDbgPrint!(mpInitFailed, c_str!("send_pool"));
            return None;
        }
        if rp_syn_init1(
            &mut cb.nwm_syn,
            0,
            ptr::null_mut(),
            0,
            cb.nwm_syn_data.len() as i32,
            cb.nwm_syn_data.as_mut_ptr(),
        ) != 0
        {
            nsDbgPrint!(rpSynInitFailed);
            return None;
        }

        crate::entries::work_thread::reset_vars();
        crate::entries::thread_nwm::reset_vars(dst_flags, qos)?;

        for i in WorkIndex::all() {
            for j in ThreadId::up_to(&core_count) {
                let info = crate::entries::thread_nwm::get_nwm_infos()
                    .get_mut(&i)
                    .get_mut(&j);
                let buf_size = NWM_BUFFER_SIZE as u32 / core_count.get();
                let buf = nwm_bufs[i.get() as usize].add((j.get() * buf_size) as usize);
                info.buf = buf.add(NWM_HDR_SIZE as usize + DATA_HDR_SIZE as usize);
                info.buf_packet_last =
                    buf.add(buf_size as usize - crate::entries::thread_nwm::get_packet_data_size());
            }
        }

        InitCleanup::init(vars)
    }

    #[named]
    pub unsafe fn entry(_t: ThreadVars, s: &mut ThreadsStacks) -> Option<()> {
        loop {
            let init = reset_init(&s.nwm_bufs)?;

            let vars = &init.0;

            let core_count = vars.core_count.get();

            let _aux1 = if core_count >= 2 {
                Some(JoinThread::create(CreateThread::create(
                    Some(crate::entries::thread_aux::thread_aux),
                    1,
                    s.aux1,
                    vars.thread_prio as i32,
                    3,
                )?))
            } else {
                None
            };

            let _aux2 = if core_count >= 3 {
                Some(JoinThread::create(CreateThread::create(
                    Some(crate::entries::thread_aux::thread_aux),
                    2,
                    s.aux2,
                    0x3f,
                    1,
                )?))
            } else {
                None
            };

            let _nwm = JoinThread::create(CreateThread::create(
                Some(match entries::thread_nwm::get_reliable_stream_method() {
                    entries::thread_nwm::ReliableStreamMethod::None => {
                        crate::entries::thread_nwm::thread_nwm
                    }
                    entries::thread_nwm::ReliableStreamMethod::KCP => {
                        crate::entries::thread_nwm::kcp_thread_nwm
                    }
                }),
                0,
                s.nwm,
                0xc,
                -2,
            )?);

            let _screen = JoinThread::create(CreateThread::create(
                Some(crate::entries::thread_screen::thread_screen),
                0,
                s.screen,
                0xc,
                2,
            )?);

            let t = crate::ThreadId::init();
            crate::entries::work_thread::work_thread_loop(t);

            nsDbgPrint!(mainLoopReset);
        }
    }
}

#[named]
pub extern "C" fn encode_thread_main(_: *mut c_void) {
    unsafe {
        if let Some(mut s) = first_time_init::entry() {
            loop_main::entry(ThreadVars(()), &mut s);
        }
        nsDbgPrint!(mainLoopExit);
        svcExitThread()
    }
}
