use crate::*;

#[cfg(not(feature = "o3ds"))]
struct ThreadsStacks<'a> {
    aux1: &'a mut StackRegion<{ RP_THREAD_STACK_SIZE as usize }>,
    aux2: &'a mut StackRegion<{ RP_THREAD_STACK_SIZE as usize }>,
    nwm: &'a mut StackRegion<{ STACK_SIZE as usize }>,
    screen: &'a mut StackRegion<{ STACK_SIZE as usize }>,
    audio: &'a mut StackRegion<{ RP_THREAD_STACK_SIZE as usize }>,
}

#[cfg(not(feature = "o3ds"))]
pub type NwmBufs = RangedArray<*mut u8, WORK_COUNT>;

struct ThreadsStorage<'a> {
    #[cfg(not(feature = "o3ds"))]
    stacks: ThreadsStacks<'a>,
    #[cfg(not(feature = "o3ds"))]
    nwm_bufs: NwmBufs,
    #[cfg(feature = "o3ds")]
    phantom: PhantomData<&'a ()>,
}

pub static mut THREAD_MAIN_HANDLE: Handle = 0;

static mut RESTART_SHUTDOWN: AtomicBool = const_default();
static mut RESTART_PENDING: AtomicBool = const_default();
static mut RESTART_READY_EVENT: Handle = const_default();
static mut RESTART_DONE_EVENT: Handle = const_default();

#[cfg(not(feature = "o3ds"))]
pub const NWM_BUFFER_SIZE: usize =
    (SEND_BUFS_SIZE / WORK_COUNT) as usize / mem::size_of::<usize>() * mem::size_of::<usize>();

fn once_encoder() -> Option<()> {
    let encoder = request_mem_from_pool::<{ mem::size_of::<encoder::Encoder>() }>()?;
    unsafe {
        encoder::ENCODER = encoder.to_ptr() as *mut encoder::Encoder;
        let encoder = &mut *encoder::ENCODER;
        encoder.once();
    }

    Some(())
}

#[named]
fn once<'a>() -> Option<ThreadsStorage<'a>> {
    #[cfg(not(feature = "o3ds"))]
    {
        let res = unsafe { gspInit(1) };
        if res != 0 {
            ns_dbg_print!(failed, c_str!("GSP init"), res);
            return None;
        }
    }

    let res = create_event(unsafe { &mut RESTART_READY_EVENT });
    if res != 0 {
        ns_dbg_print!(failed, c_str!("Create restart ready event"), res);
        return None;
    }
    let res = create_event(unsafe { &mut RESTART_DONE_EVENT });
    if res != 0 {
        ns_dbg_print!(failed, c_str!("Create restart done event"), res);
        return None;
    }

    #[cfg(not(feature = "o3ds"))]
    {
        let gf256_ctx_mem = request_mem_from_pool::<{ mem::size_of::<gf256_ctx>() }>()?;
        unsafe { GF256Ctx = gf256_ctx_mem.to_ptr() as *mut gf256_ctx };

        if unsafe { fecal_init_(FECAL_VERSION as i32) } != 0 {
            ns_dbg_print!(failed, c_str!("FEC-AL init"), res);
            return None;
        }

        let fecal_encoder_mem =
            request_mem_from_pool_vsize(unsafe { fecal_encoder_size() } as usize)?;
        unsafe { rp_kcp_fecal_encoder = fecal_encoder_mem.as_mut_ptr() as *mut _ };
    }

    #[cfg(not(feature = "o3ds"))]
    unsafe {
        rp_svc_increase_limits()
    };

    unsafe { entries::thread_nwm::once_reliable_stream_cb() }?;

    #[cfg(not(feature = "o3ds"))]
    let mut nwm_bufs: NwmBufs = const_default();
    #[cfg(not(feature = "o3ds"))]
    unsafe {
        let cb = &mut *entries::thread_nwm::RELIABLE_STREAM_CB;

        let m = cb.locked.send_bufs.as_mut_ptr().as_mut_ptr();
        for i in WorkIndex::all() {
            *i.index_into_mut(nwm_bufs.arr()) = m.add(NWM_BUFFER_SIZE * i.get() as usize);
        }
    }

    #[cfg(not(feature = "mem3"))]
    unsafe { entries::thread_screen::once_img_infos() }?;

    if once_encoder() == None {
        ns_dbg_print!(failed, c_str!("Encoder init"), res);
        return None;
    }

    unsafe { entries::thread_screen::once_screen_handles() }?;

    #[cfg(not(feature = "o3ds"))]
    let aux1_stack = request_mem_from_pool::<{ RP_THREAD_STACK_SIZE as usize }>()?;
    #[cfg(not(feature = "o3ds"))]
    let aux2_stack = request_mem_from_pool::<{ RP_THREAD_STACK_SIZE as usize }>()?;
    #[cfg(not(feature = "o3ds"))]
    let nwm_stack = request_mem_from_pool::<{ STACK_SIZE as usize }>()?;
    #[cfg(not(feature = "o3ds"))]
    let screen_stack = request_mem_from_pool::<{ STACK_SIZE as usize }>()?;
    #[cfg(not(feature = "o3ds"))]
    let audio_stack = request_mem_from_pool::<{ RP_THREAD_STACK_SIZE as usize }>()?;

    // audio packet buffer allocated once (optional feature, failure is non-fatal)
    #[cfg(not(feature = "o3ds"))]
    entries::thread_audio::once_audio();

    #[cfg(not(feature = "o3ds"))]
    {
        let mut svc_thread: Handle = 0;
        let res = create_thread_from_pool::<{ SMALL_STACK_SIZE as usize }>(
            &mut svc_thread,
            Some(handlePortThread),
            SVC_PORT_NWM.as_ptr() as u32,
            0x10,
            1,
        )
        .0;
        if res != 0 {
            ns_dbg_print!(failed, c_str!("Create remote play service thread"), res);
        }
    }

    ns_dbg_print!(mem_usage, unsafe { plgGetMemoryUsage() });

    #[cfg(not(feature = "o3ds"))]
    return Some(ThreadsStorage {
        stacks: ThreadsStacks {
            aux1: stack_region_from_mem_region(aux1_stack),
            aux2: stack_region_from_mem_region(aux2_stack),
            nwm: stack_region_from_mem_region(nwm_stack),
            screen: stack_region_from_mem_region(screen_stack),
            audio: stack_region_from_mem_region(audio_stack),
        },
        nwm_bufs,
    });

    #[cfg(feature = "o3ds")]
    return Some(ThreadsStorage {
        phantom: PhantomData,
    });
}

#[cfg(not(feature = "o3ds"))]
struct InitVars {
    core_count: CoreCount,
    thread_prio: u32,
    qos: u32,
}

struct Init {
    #[cfg(not(feature = "o3ds"))]
    vars: InitVars,
}

impl Init {
    fn init(#[cfg(not(feature = "o3ds"))] vars: InitVars) -> Option<Self> {
        #[cfg(not(feature = "o3ds"))]
        unsafe {
            init_syn_handles(vars.core_count)?;
            entries::thread_nwm::init_seg_mem_handles(vars.qos)?;
        }

        #[cfg(not(feature = "o3ds"))]
        return Some(Init { vars });

        #[cfg(feature = "o3ds")]
        return Some(Init {});
    }
}

impl Drop for Init {
    fn drop(&mut self) {
        #[cfg(not(feature = "o3ds"))]
        unsafe {
            entries::thread_nwm::cleanup_seg_mem_handles();
            cleanup_syn_handles(self.vars.core_count);
        }
    }
}

struct Impl(());

#[named]
fn pause() -> Option<()> {
    unsafe {
        if RESTART_PENDING.load(Ordering::Acquire) {
            RESTART_PENDING.store(false, Ordering::Release);

            let res = svcSignalEvent(RESTART_READY_EVENT);
            if res != 0 {
                ns_dbg_print!(failed, c_str!("Signal restart event"), res);
            }

            if RESTART_SHUTDOWN.load(Ordering::Acquire) {
                return None;
            }

            let res = svcWaitSynchronization(RESTART_DONE_EVENT, -1);
            if res != 0 {
                ns_dbg_print!(failed, c_str!("Wait restart event"), res);
            }
        }
    }
    Some(())
}

#[named]
fn init(#[cfg(not(feature = "o3ds"))] nwm_bufs: &NwmBufs) -> Option<Init> {
    clear_reset_threads();

    unsafe {
        #[cfg(feature = "o3ds")]
        {
            RP_CONFIG_SAVED = *config_consts::RP_CONFIG;
        }

        set_core_count_in_use(RP_CONFIG.core_count().load(Ordering::Acquire));
        #[cfg(not(feature = "o3ds"))]
        let core_count = core_count_in_use();

        let dst_port = RP_CONFIG.dst_port().load(Ordering::Acquire);
        let dst_flags = dst_port & RP_CONFIG_FLAGS_DATA_MASK;
        let dst_port = dst_port & RP_CONFIG_PORT_MASK;
        if dst_port == 0 {
            RP_CONFIG
                .dst_port()
                .store(RP_DST_PORT_DEFAULT | dst_flags, Ordering::Release)
        }

        #[cfg(not(feature = "o3ds"))]
        {
            let audio_enable = RP_CONFIG.audio_enable().load(Ordering::Acquire) > 0;
            entries::thread_audio::AUDIO_ENABLE = audio_enable && entries::thread_audio::init();
        }

        let qos = RP_CONFIG.qos().load(Ordering::Acquire);
        entries::thread_nwm::init(dst_flags, qos)?;

        let mode = RP_CONFIG.mode().load(Ordering::Acquire);
        entries::thread_screen::init(mode);

        let thread_prio = RP_CONFIG.thread_prio().load(Ordering::Acquire);
        let res = svcSetThreadPriority(THREAD_MAIN_HANDLE, thread_prio as i32);
        if res != 0 {
            ns_dbg_print!(failed, c_str!("Set thread priority"), res);
        }

        #[cfg(not(feature = "o3ds"))]
        entries::thread_nwm::init_reliable_stream_cb(qos)?;

        let encoder = &mut *encoder::ENCODER;
        let quality = RP_CONFIG.quality().load(Ordering::Acquire);
        let (chroma_ss, downsample, quality) = encoder::jpeg_get_params(quality);
        let color_bias = entries::thread_nwm::get_lossless_compression_bias();
        let color_bias = [color_bias, color_bias];
        #[cfg(not(feature = "o3ds"))]
        encoder.init(
            quality,
            core_count,
            chroma_ss,
            downsample,
            color_bias,
            entries::thread_nwm::get_reliable_stream() != entries::thread_nwm::ReliableStream::None,
            entries::thread_nwm::get_reliable_stream_delta_prog(),
        )?;

        #[cfg(feature = "o3ds")]
        encoder.init(quality, chroma_ss, downsample, color_bias)?;
        entries::work_thread::init(chroma_ss, downsample, color_bias);

        #[cfg(not(feature = "o3ds"))]
        entries::thread_nwm::init_nwm_infos(nwm_bufs, core_count);

        entries::thread_nwm::init_ov_stats();

        #[cfg(not(feature = "o3ds"))]
        return Init::init(InitVars {
            core_count,
            thread_prio,
            qos,
        });

        #[cfg(feature = "o3ds")]
        return Init::init();
    }
}

#[named]
fn main(_impl_: Impl, #[cfg(not(feature = "o3ds"))] s: &mut ThreadsStorage) -> Option<()> {
    pause()?;

    let use_dbg = unsafe { (*config_consts::NTR_CONFIG).ex.nsUseDbg != 0 };

    let init = init(
        #[cfg(not(feature = "o3ds"))]
        &s.nwm_bufs,
    )?;
    #[cfg(feature = "o3ds")]
    let _ = init;
    #[cfg(not(feature = "o3ds"))]
    let core_count = init.vars.core_count.get();
    {
        #[cfg(not(feature = "o3ds"))]
        let _aux1 = if core_count >= 2 {
            Some(JoinThread::create(CreateThread::create(
                Some(entries::thread_aux::thread_aux),
                1,
                s.stacks.aux1,
                init.vars.thread_prio as i32,
                3,
            )?))
        } else {
            None
        };

        #[cfg(not(feature = "o3ds"))]
        let _aux2 = if core_count >= 3 {
            Some(JoinThread::create(CreateThread::create(
                Some(entries::thread_aux::thread_aux),
                2,
                s.stacks.aux2,
                RP_THREAD_PRIO_MAX as s32,
                1,
            )?))
        } else {
            None
        };

        #[cfg(not(feature = "o3ds"))]
        let _nwm = JoinThread::create(CreateThread::create(
            Some(match entries::thread_nwm::get_reliable_stream() {
                entries::thread_nwm::ReliableStream::None => entries::thread_nwm::thread_nwm,
                entries::thread_nwm::ReliableStream::KCP => entries::thread_nwm::kcp_thread_nwm,
            }),
            0,
            s.stacks.nwm,
            RP_THREAD_PRIO_MIN as s32,
            RP_CORE_ID_MAIN,
        )?);

        #[cfg(not(feature = "o3ds"))]
        let _screen = JoinThread::create(CreateThread::create(
            Some(entries::thread_screen::thread_screen),
            0,
            s.stacks.screen,
            RP_THREAD_PRIO_MIN as s32,
            RP_CORE_ID_MAIN,
        )?);

        // NTR-HR+ audio capture thread (opt-in via RP_CONFIG.audioEnable),
        // high priority so every dsp frame is captured
        // audio is optional: a failed create must not kill remote play
        #[cfg(not(feature = "o3ds"))]
        let _audio = if unsafe { entries::thread_audio::AUDIO_ENABLE } {
            CreateThread::create(
                Some(entries::thread_audio::thread_audio),
                0,
                s.stacks.audio,
                RP_THREAD_PRIO_MIN as s32,
                1,
            )
            .map(JoinThread::create)
        } else {
            None
        };

        #[cfg(not(feature = "o3ds"))]
        unsafe {
            if use_dbg {
                rp_svc_print_limits();
            }
        }

        let t = ThreadIndex::init(0);
        unsafe {
            entries::work_thread::work_thread_loop(t);
        }
        set_reset_threads();
    }

    if use_dbg {
        ns_dbg_print!(msg, c_str!("Nwm main loop restarted"));
    }

    Some(())
}

fn main_loop(#[cfg(not(feature = "o3ds"))] s: &mut ThreadsStorage) -> Option<()> {
    loop {
        main(
            Impl(()),
            #[cfg(not(feature = "o3ds"))]
            s,
        )?
    }
}

#[named]
pub extern "C" fn encode_thread_main(_: *mut c_void) {
    unsafe {
        __system_initSyscalls();
        #[cfg(not(feature = "o3ds"))]
        if let Some(mut storage) = once() {
            main_loop(&mut storage);
        }
        #[cfg(feature = "o3ds")]
        if let Some(_) = once() {
            main_loop();
        }
        ns_dbg_print!(msg, c_str!("Nwm main loop exited"));
        svcExitThread()
    }
}

#[unsafe(no_mangle)]
#[named]
extern "C" fn nwmPause(shutdown: bool) {
    unsafe { RESTART_SHUTDOWN.store(shutdown, Ordering::Release) };
    unsafe { RESTART_PENDING.store(true, Ordering::Release) };
    set_reset_threads();
    let res = unsafe { svcWaitSynchronization(RESTART_READY_EVENT, -1) };
    if res != 0 {
        ns_dbg_print!(failed, c_str!("Wait restart event"), res);
    }
}

#[unsafe(no_mangle)]
#[named]
extern "C" fn nwmUnpause() {
    let res = unsafe { svcSignalEvent(RESTART_DONE_EVENT) };
    if res != 0 {
        ns_dbg_print!(failed, c_str!("Signal restart event"), res);
    }
}
