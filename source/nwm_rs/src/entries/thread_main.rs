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
        info.buf = buf.to_ptr().add(NWM_HDR_SIZE as usize);
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
            info.user_work_next = (idx / RP_CORE_COUNT_MAX) % WORK_COUNT;
            info.user_thread_id = idx % RP_CORE_COUNT_MAX;
        }

        let (info0, info1): (
            RangedArraySlice<CInfo, 1>,
            RangedArraySlice<CInfo, { CINFOS_COUNT - 1 }>,
        ) = infos.split_at_mut::<1>();
        let info0 = &mut info0.0.get_mut(&Ranged::init()).info;

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

    struct InitCleanup {
        core_count: u32_,
        thread_prio: u32_,
    }

    #[named]
    unsafe fn reset_init() -> Option<InitCleanup> {
        reset_threads_clear();

        let config = Config(());

        set_core_count(config.core_count_ar());
        let core_count = core_count().get();
        config.set_core_count_ar(core_count);

        let mode = config.mode_ar();
        let isTop = (mode & 0xff00) > 0;
        let factor = mode & 0xff;
        priority_is_top = isTop;
        priority_factor = factor;
        priority_factor_scaled = FIX(core::intrinsics::log2f64((factor + 1) as c_double));
        crate::entries::work_thread::no_skip_next_frames();

        if config.dst_port_ar() == 0 {
            config.set_dst_port_ar(RP_DST_PORT_DEFAULT)
        }

        let qos = config.qos_ar();
        min_send_interval_tick =
            (SYSCLOCK_ARM11 as u64_ * PACKET_SIZE as u64_ / qos as u64_) as u32_;
        min_send_interval_ns =
            (min_send_interval_tick as u64_ * 1000_000_000 / SYSCLOCK_ARM11 as u64_) as u32_;

        reset_jpeg_compress();

        let thread_prio = config.thread_prio_ar();
        let res = svcSetThreadPriority(thread_main_handle, thread_prio as i32);
        if R_FAILED(res) {
            nsDbgPrint!(setThreadPriorityFailed, res);
        }

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

        for i in WorkIndex::all() {}

        Some(InitCleanup {
            core_count,
            thread_prio,
        })
    }

    fn reset_jpeg_compress() {
        todo!()
    }

    unsafe fn reset_threads_clear() {
        AtomicBool::from_ptr(ptr::addr_of_mut!(crate::reset_threads))
            .store(false, Ordering::Relaxed)
    }

    pub unsafe fn entry(_t: ThreadVars, s: &mut ThreadsStacks) -> Option<()> {
        loop {
            let vars = reset_init()?;

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
