use crate::*;

pub struct ThreadVars(());

impl ThreadVars {}

pub struct ThreadsStacks<'a> {
    aux1: &'a mut StackRegion<{ RP_THREAD_STACK_SIZE as usize }>,
    aux2: &'a mut StackRegion<{ RP_THREAD_STACK_SIZE as usize }>,
    nwm: &'a mut StackRegion<{ STACK_SIZE as usize }>,
    screen: &'a mut StackRegion<{ STACK_SIZE as usize }>,
}

mod first_time_init {
    use super::*;

    unsafe fn set_nwm_data_buf(
        i: WorkIndex,
        j: ThreadId,
        buf: &'static mut MemRegion8<NWM_BUFFER_SIZE>,
    ) {
        let info = nwm_infos.get_mut(&i).get_mut(&j);
        info.buf = buf.to_ptr().add(NWM_HDR_SIZE + DATA_HDR_SIZE as usize);
        info.buf_packet_last = buf.to_ptr().add(NWM_BUFFER_SIZE - PACKET_DATA_SIZE);
    }

    fn cinfo_alloc_size(i: &Ranged<CINFOS_COUNT>) -> usize {
        let idx = i.get();
        if idx == 0 {
            0x18000
        } else if idx == 1 {
            0x10000
        } else {
            0x2000
        }
    }

    unsafe fn init_jpeg_compress() -> Option<()> {
        cinfos_all = ptr::addr_of_mut!(cinfos) as *mut CInfosAll;

        const _assert_cond: bool = mem::size_of::<CInfosAll>() == mem::size_of::<CInfos>();
        let _assert = <[(); _assert_cond as usize - 1]>::default();

        let infos = &mut *cinfos_all;

        for i in Ranged::<CINFOS_COUNT>::all() {
            let info = &mut infos.get_mut(&i).info;

            let alloc_size = cinfo_alloc_size(&i);

            let buf = request_mem_from_pool_vsize(alloc_size)?;
            info.alloc.buf = buf.as_mut_ptr();
            info.alloc.stats.offset = 0;
            info.alloc.stats.remaining = buf.len() as u32_;

            info.err = jpeg_std_error(ptr::addr_of_mut!(jerr));

            info.mem_pool_manual = 1;
            jpeg_CreateCompress(
                info,
                JPEG_LIB_VERSION as i32,
                mem::size_of::<jpeg_compress_struct>(),
            );
            jpeg_stdio_dest(info, ptr::null_mut());

            info.in_color_space = JCS_RGB;
            info.defaults_skip_tables = 1;
            jpeg_set_defaults(info);
            info.dct_method = JDCT_IFAST;
            info.skip_markers = 1;
            info.skip_buffers = 1;
            info.skip_init_dest = 1;

            info.input_components = 3;
            info.jpeg_color_space = JCS_YCbCr;
            info.num_components = 3;
            info.color_reuse = 1;

            let idx = i.get();
            info.user_work_index = (idx / RP_CORE_COUNT_MAX) % WORK_COUNT;
            info.user_thread_id = idx % RP_CORE_COUNT_MAX;
        }

        let (info0, info1): (
            RangedArraySlice<CInfo, 1>,
            RangedArraySlice<CInfo, { CINFOS_COUNT - 1 }>,
        ) = infos.split_at_mut::<1>();
        let info0 = &mut info0.0.get_mut(&Ranged::<1>::init()).info;

        jpeg_std_huff_tables(info0 as j_compress_ptr as j_common_ptr);
        for j in Ranged::<{ CINFOS_COUNT - 1 }>::all() {
            for i in Ranged::<NUM_HUFF_TBLS>::all() {
                let idx = i.get() as usize;
                let info = &mut info1.0.get_mut(&j).info;

                *info.dc_huff_tbl_ptrs.get_unchecked_mut(idx) =
                    *info0.dc_huff_tbl_ptrs.get_unchecked(idx);

                *info.ac_huff_tbl_ptrs.get_unchecked_mut(idx) =
                    *info0.ac_huff_tbl_ptrs.get_unchecked(idx);
            }
        }

        jpeg_jinit_color_converter(info0);
        for j in Ranged::<{ CINFOS_COUNT - 1 }>::all() {
            let info = &mut info1.0.get_mut(&j).info;
            info.cconvert = info0.cconvert;
        }

        jpeg_rgb_ycc_start(info0);

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
        if R_FAILED(res) {
            nsDbgPrint!(gspInitFailed, res);
            return None;
        }

        for i in WorkIndex::all() {
            for j in ThreadId::all() {
                if let Some(m) = request_mem_from_pool::<NWM_BUFFER_SIZE>() {
                    set_nwm_data_buf(i, j, m);
                } else {
                    return None;
                }
            }
        }

        for j in Ranged::<SCREEN_COUNT>::all() {
            for i in ImgWorkIndex::all() {
                if let Some(m) = request_mem_from_pool::<IMG_BUFFER_SIZE>() {
                    *img_infos.get_mut(&j).bufs.get_mut(&i) = m.to_ptr();
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

        for i in Ranged::<SCREEN_COUNT>::all() {
            let res = create_event((*syn_handles).port_screen_ready.get_mut(&i));
            if R_FAILED(res) {
                nsDbgPrint!(createPortEventFailed, res);
                return None;
            }
        }

        let res = create_event(&mut (*syn_handles).nwm_ready);
        if R_FAILED(res) {
            nsDbgPrint!(createNwmEventFailed, res);
            return None;
        }

        if R_FAILED(svcOpenProcess(
            &mut cap_params.home,
            (*ntr_config).HomeMenuPid,
        )) {
            nsDbgPrint!(openProcessFailed, res);
            return None;
        }

        let mut svc_thread = mem::MaybeUninit::uninit();
        let res = create_thread_from_pool::<{ STACK_SIZE as usize }>(
            svc_thread.as_mut_ptr(),
            Some(handlePortThread),
            0,
            0x10,
            1,
        )
        .0;
        if R_FAILED(res) {
            nsDbgPrint!(createNwmSvcFailed, res);
        }

        let aux1Stack = request_mem_from_pool::<{ RP_THREAD_STACK_SIZE as usize }>()?;
        let aux2Stack = request_mem_from_pool::<{ RP_THREAD_STACK_SIZE as usize }>()?;
        let nwmStack = request_mem_from_pool::<{ STACK_SIZE as usize }>()?;
        let screenStack = request_mem_from_pool::<{ STACK_SIZE as usize }>()?;

        Some(ThreadsStacks {
            aux1: stack_region_from_mem_region(aux1Stack),
            aux2: stack_region_from_mem_region(aux2Stack),
            nwm: stack_region_from_mem_region(nwmStack),
            screen: stack_region_from_mem_region(screenStack),
        })
    }
}

mod loop_main {
    use super::*;

    unsafe fn core_count() -> CoreCount {
        core_count_in_use
    }

    unsafe fn set_core_count(v: u32_) {
        core_count_in_use.set(v);
    }

    struct Config(());

    macro_rules! config_ar {
        ($v:ident) => {
            AtomicU32::from_ptr(ptr::addr_of_mut!((*rp_config).$v)).load(Ordering::Relaxed)
        };
    }

    macro_rules! set_config_ar {
        ($v:ident, $n:ident) => {
            AtomicU32::from_ptr(ptr::addr_of_mut!((*rp_config).$v)).store($n, Ordering::Relaxed)
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
        core_count: u32_,
        thread_prio: u32_,
    }

    struct InitCleanup(InitVars);

    impl InitCleanup {
        #[named]
        unsafe fn init(v: InitVars) -> Option<Self> {
            let res = svcCreateSemaphore(&mut (*syn_handles).screen_ready, 1, 1);
            if R_FAILED(res) {
                nsDbgPrint!(createSemaphoreFailed, c_str!("screen_ready"), res);
                return None;
            }

            for i in WorkIndex::all() {
                let work = (*syn_handles).works.get_mut(&i);

                let res = svcCreateSemaphore(&mut work.work_done, 1, 1);
                if R_FAILED(res) {
                    nsDbgPrint!(createSemaphoreFailed, c_str!("work_done"), res);
                    return None;
                }

                let res = svcCreateSemaphore(&mut work.nwm_done, 1, 1);
                if R_FAILED(res) {
                    nsDbgPrint!(createSemaphoreFailed, c_str!("nwm_done"), res);
                    return None;
                }

                let res = svcCreateSemaphore(&mut work.nwm_ready, 0, 1);
                if R_FAILED(res) {
                    nsDbgPrint!(createSemaphoreFailed, c_str!("nwm_ready"), res);
                    return None;
                }

                work.work_begin_flag = false;
                work.work_done_count = 0;
            }

            for j in ThreadId::up_to_unchecked(v.core_count) {
                let thread = (*syn_handles).threads.get_mut(&j);

                let res = svcCreateSemaphore(&mut thread.work_ready, 0, WORK_COUNT as i32);
                if R_FAILED(res) {
                    nsDbgPrint!(createSemaphoreFailed, c_str!("work_ready"), res);
                    return None;
                }

                let res = svcCreateSemaphore(&mut thread.work_begin_ready, 0, WORK_COUNT as i32);
                if R_FAILED(res) {
                    nsDbgPrint!(createSemaphoreFailed, c_str!("work_begin_ready"), res);
                    return None;
                }
            }

            Some(Self(v))
        }
    }

    impl Drop for InitCleanup {
        fn drop(&mut self) {
            unsafe {
                for j in ThreadId::up_to_unchecked(self.0.core_count) {
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

    macro_rules! LOG {
        ($v:expr) => {
            unsafe { FIX(core::intrinsics::log2f64(($v + 1) as c_double)) }
        };
    }

    macro_rules! LOG8 {
        ($v:expr) => {
            [
                LOG!($v),
                LOG!($v + 1),
                LOG!($v + 2),
                LOG!($v + 3),
                LOG!($v + 4),
                LOG!($v + 5),
                LOG!($v + 6),
                LOG!($v + 7),
            ]
        };
    }

    macro_rules! LOG64 {
        ($v:literal) => {
            [
                LOG8!($v),
                LOG8!($v + 8),
                LOG8!($v + 16),
                LOG8!($v + 24),
                LOG8!($v + 32),
                LOG8!($v + 40),
                LOG8!($v + 48),
                LOG8!($v + 56),
            ]
        };
    }

    #[named]
    unsafe fn reset_init() -> Option<InitCleanup> {
        clear_reset_threads_ar();

        let config = Config(());

        set_core_count(config.core_count_ar());
        let core_count = core_count().get();
        config.set_core_count_ar(core_count);

        let log_scaled_tab_nested: [[[u32_; 8]; 8]; 4] =
            [LOG64!(0), LOG64!(64), LOG64!(128), LOG64!(192)];
        let log_scaled_tab: &[u32_; 256] = mem::transmute(&log_scaled_tab_nested);

        let mode = config.mode_ar();
        let is_top = (mode & 0xff00) > 0;
        let factor = mode & 0xff;
        priority_is_top = is_top;
        priority_factor = factor;
        priority_factor_scaled = *log_scaled_tab.get_unchecked(factor as usize);
        crate::entries::work_thread::no_skip_next_frames();

        if config.dst_port_ar() == 0 {
            config.set_dst_port_ar(RP_DST_PORT_DEFAULT)
        }

        let qos = config.qos_ar();
        min_send_interval_tick =
            (SYSCLOCK_ARM11 as u64_ * PACKET_SIZE as u64_ / qos as u64_) as u32_;
        min_send_interval_ns =
            (min_send_interval_tick as u64_ * 1000_000_000 / SYSCLOCK_ARM11 as u64_) as u32_;

        let thread_prio = config.thread_prio_ar();
        let res = svcSetThreadPriority(thread_main_handle, thread_prio as i32);
        if R_FAILED(res) {
            nsDbgPrint!(setThreadPriorityFailed, res);
        }

        let vars = InitVars {
            core_count,
            thread_prio,
        };
        reset_jpeg_compress(&config, &vars);

        currently_updating = priority_is_top;
        for i in Ranged::<SCREEN_COUNT>::all() {
            *frame_counts.get_mut(&i) = 1;
            *frame_queues.get_mut(&i) = priority_factor_scaled;
        }

        for i in WorkIndex::all() {
            *screens_captured.get_mut(&i) = false;
            *screens_synced.get_mut(&i) = false;
        }

        last_send_tick = svcGetSystemTick() as u32_;

        for i in WorkIndex::all() {
            let load = load_and_progresses.get_mut(&i);

            for j in ThreadId::up_to_unchecked(core_count) {
                let info = nwm_infos.get_mut(&i).get_mut(&j);
                let buf = info.buf;
                let info = &mut info.info;
                info.send_pos = buf;
                info.pos = buf;
                info.flag = 0;

                *load.p.get_mut(&j) = 0;
                *load.p_snapshot.get_mut(&j) = 0;
            }

            load.n = 0;
            load.n_last = 0;
            load.n_adjusted = 0;
            load.n_last_adjusted = 0;

            *nwm_need_syn.get_mut(&i) = true;
        }
        nwm_work_index = WorkIndex::init();
        nwm_thread_id = ThreadId::init();
        screen_work_index = WorkIndex::init();

        InitCleanup::init(vars)
    }

    unsafe fn reset_jpeg_compress(config: &Config, _vars: &InitVars) {
        let infos = &mut *cinfos_all;

        for i in Ranged::<CINFOS_COUNT>::all() {
            infos.get_mut(&i).info.global_state = JPEG_CSTATE_START;
        }

        {
            let (info0, info1): (
                RangedArraySlice<CInfo, 1>,
                RangedArraySlice<CInfo, { CINFOS_COUNT - 1 }>,
            ) = infos.split_at_mut::<1>();
            let info0 = &mut info0.0.get_mut(&Ranged::<1>::init()).info;

            jpeg_set_quality(info0, config.quality_ar() as c_int, 1);
            for j in Ranged::<{ CINFOS_COUNT - 1 }>::all() {
                for i in Ranged::<NUM_QUANT_TBLS>::all() {
                    let idx = i.get() as usize;
                    let info = &mut info1.0.get_mut(&j).info;

                    *info.quant_tbl_ptrs.get_unchecked_mut(idx) =
                        *info0.quant_tbl_ptrs.get_unchecked(idx);
                }
            }
        }

        for i in Ranged::<CINFOS_COUNT>::all() {
            let info = infos.get_mut(&i);

            let alloc_stats = &mut info.alloc_stats;
            let cinfo = &mut info.info;

            if alloc_stats.qual.offset == 0 {
                ptr::copy_nonoverlapping(
                    ptr::addr_of!(cinfo.alloc.stats),
                    ptr::addr_of_mut!(alloc_stats.qual),
                    1,
                );
            } else {
                ptr::copy_nonoverlapping(
                    ptr::addr_of!(alloc_stats.qual),
                    ptr::addr_of_mut!(cinfo.alloc.stats),
                    1,
                );
            }
        }

        {
            let (info0, info1): (
                RangedArraySlice<CInfo, 1>,
                RangedArraySlice<CInfo, { CINFOS_COUNT - 1 }>,
            ) = infos.split_at_mut::<1>();
            let info0 = &mut info0.0.get_mut(&Ranged::<1>::init()).info;

            jpeg_jinit_forward_dct(info0);
            info0.fdct_reuse = 1;

            for j in Ranged::<{ CINFOS_COUNT - 1 }>::all() {
                let info = &mut info1.0.get_mut(&j).info;

                info.fdct = info0.fdct;
                info.fdct_reuse = 1;
            }

            jpeg_start_pass_fdctmgr(info0);
        }

        for h in WorkIndex::all() {
            let info = &mut infos.get_mut(&h).info as j_compress_ptr as j_common_ptr;

            for i in ThreadId::all() {
                let bufs = work_buffers.get_mut(&h).get_mut(&i);

                for ci in Ranged::<MAX_COMPONENTS>::all() {
                    *bufs.prep.get_unchecked_mut(ci.get() as usize) = jpeg_alloc_sarray(
                        info,
                        JPOOL_IMAGE as c_int,
                        240,
                        MAX_SAMP_FACTOR * DCTSIZE,
                    );
                }

                for ci in Ranged::<MAX_COMPONENTS>::all() {
                    *bufs.color.get_unchecked_mut(ci.get() as usize) =
                        jpeg_alloc_sarray(info, JPOOL_IMAGE as c_int, 240, MAX_SAMP_FACTOR);
                }

                let buf = jpeg_alloc_large(
                    info,
                    JPOOL_IMAGE as c_int,
                    C_MAX_BLOCKS_IN_MCU as usize * mem::size_of::<JBLOCK>(),
                ) as JBLOCKROW;
                for b in Ranged::<C_MAX_BLOCKS_IN_MCU>::all() {
                    let b_i = b.get() as usize;
                    *bufs.mcu.get_unchecked_mut(b_i) = buf.add(b_i);
                }
            }
        }

        for i in Ranged::<CINFOS_COUNT>::all() {
            let info = infos.get_mut(&i);

            let alloc_stats = &mut info.alloc_stats;
            let cinfo = &mut info.info;

            ptr::copy_nonoverlapping(
                ptr::addr_of!(cinfo.alloc.stats),
                ptr::addr_of_mut!(alloc_stats.comp),
                1,
            );
        }
    }

    unsafe fn clear_reset_threads_ar() {
        AtomicBool::from_ptr(ptr::addr_of_mut!(crate::reset_threads))
            .store(false, Ordering::Relaxed)
    }

    pub unsafe fn entry(_t: ThreadVars, s: &mut ThreadsStacks) -> Option<()> {
        loop {
            let init = reset_init()?;

            let vars = &init.0;

            let core_count = vars.core_count;

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
                Some(crate::entries::thread_nwm::thread_nwm),
                0,
                s.nwm,
                0x8,
                2,
            )?);

            let _screen = JoinThread::create(CreateThread::create(
                Some(crate::entries::thread_screen::thread_screen),
                0,
                s.screen,
                0x8,
                2,
            )?);

            let t = crate::ThreadId::init_unchecked(0);
            crate::entries::work_thread::work_thread_loop(t);
        }
    }
}

pub extern "C" fn encode_thread_main(_: *mut c_void) {
    unsafe {
        if let Some(mut s) = first_time_init::entry() {
            loop_main::entry(ThreadVars(()), &mut s);
        }
        svcExitThread()
    }
}
