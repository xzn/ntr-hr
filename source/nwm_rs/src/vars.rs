use crate::*;

mod gpu;
mod ranged;

pub use gpu::*;
pub use ranged::*;

pub const RP_CORE_ID_MAIN: s32 = if cfg!(feature = "o3ds") { 1 } else { 2 };

#[allow(unused)]
macro_rules! rp_need_core_syn {
    () => {
        core_count_in_use().get() != RP_CORE_COUNT_MIN
    };
}

#[allow(unused)]
pub const FRAME_TIME_MAX_F: u32 = 4;
#[allow(unused)]
pub const FRAME_TIME_FACTOR: u32 = 3;

#[cfg(feature = "o3ds")]
pub static mut RP_CONFIG_SAVED: RP_CONFIG = const_default();

pub mod config_consts {
    use super::*;

    pub const OV_STATS: *mut OVERLAY_STATS_INFO =
        (NS_CONFIG_ADDR as usize + mem::offset_of!(NS_CONFIG, ovStats)) as *mut OVERLAY_STATS_INFO;

    pub const RP_CONFIG: *mut RP_CONFIG =
        (NS_CONFIG_ADDR as usize + mem::offset_of!(NS_CONFIG, rpConfig)) as *mut RP_CONFIG;

    #[allow(unused)]
    pub const RP_CONFIG_U32_COUNT: usize = mem::size_of::<RP_CONFIG>() / mem::size_of::<u32>();

    #[allow(unused)]
    pub const RP_CONFIG_QOS_OFFSET_COUNT: usize =
        mem::offset_of!(RP_CONFIG, qos) / mem::size_of::<u32>();

    #[allow(unused)]
    pub const RP_CONFIG_DSTADDR_OFFSET_COUNT: usize =
        mem::offset_of!(RP_CONFIG, dstAddr) / mem::size_of::<u32>();

    #[allow(unused)]
    pub const RP_CONFIG_QUALITY_OFFSET_COUNT: usize =
        mem::offset_of!(RP_CONFIG, quality) / mem::size_of::<u32>();

    pub const NTR_CONFIG: *mut NTR_CONFIG =
        (NS_CONFIG_ADDR as usize + mem::offset_of!(NS_CONFIG, ntrConfig)) as *mut NTR_CONFIG;
}

pub struct RpConfig(());

pub const RP_CONFIG: RpConfig = RpConfig(());

#[cfg(not(feature = "o3ds"))]
macro_rules! rp_config_field {
    ($v:ident) => {
        unsafe { AtomicU32::from_mut(&mut (*config_consts::RP_CONFIG).$v) }
    };
}

#[cfg(not(feature = "o3ds"))]
macro_rules! rp_config_screen_field {
    ($v:ident, $s:ident) => {
        unsafe {
            AtomicU32::from_mut(&mut (*config_consts::RP_CONFIG).screens[$s.get() as usize].$v)
        }
    };
}

#[cfg(feature = "o3ds")]
macro_rules! rp_config_field {
    ($v:ident) => {
        unsafe { AtomicU32::from_mut(&mut RP_CONFIG_SAVED.$v) }
    };
}

#[cfg(feature = "o3ds")]
macro_rules! rp_config_screen_field {
    ($v:ident, $s:ident) => {
        unsafe { AtomicU32::from_mut(&mut RP_CONFIG_SAVED.screens[$s.get() as usize].$v) }
    };
}

#[allow(unused)]
impl RpConfig {
    pub fn core_count(&self) -> &mut AtomicU32 {
        rp_config_field!(coreCount)
    }

    pub fn mode(&self) -> &mut AtomicU32 {
        rp_config_field!(mode)
    }

    pub fn dst_addr(&self) -> &mut AtomicU32 {
        rp_config_field!(dstAddr)
    }

    pub fn game_pid(&self) -> &mut AtomicU32 {
        rp_config_field!(gamePid)
    }

    pub fn dst_port(&self) -> &mut AtomicU32 {
        rp_config_field!(dstPort)
    }

    pub fn qos(&self) -> &mut AtomicU32 {
        rp_config_field!(qos)
    }

    pub fn quality(&self) -> &mut AtomicU32 {
        rp_config_field!(quality)
    }

    pub fn thread_prio(&self) -> &mut AtomicU32 {
        rp_config_field!(threadPriority)
    }

    pub fn separate_screen_config(&self) -> &mut AtomicU32 {
        rp_config_field!(separateScreenConfig)
    }

    pub fn audio_enable(&self) -> &mut AtomicU32 {
        rp_config_field!(audioEnable)
    }

    pub fn chroma_ss(&self, s: ScreenIndex) -> &mut AtomicU32 {
        rp_config_screen_field!(chromaSs, s)
    }

    pub fn downsample(&self, s: ScreenIndex) -> &mut AtomicU32 {
        rp_config_screen_field!(downsample, s)
    }

    pub fn fps_limit(&self, s: ScreenIndex) -> &mut AtomicU32 {
        rp_config_screen_field!(fpsLimit, s)
    }

    pub fn no_skip_frame(&self, s: ScreenIndex) -> &mut AtomicU32 {
        rp_config_screen_field!(noSkipFrame, s)
    }
}

pub const THREAD_WAIT_NS: DurationNs = DurationNs::init(NWM_THREAD_WAIT_NS as s64);
pub const RP_THREAD_PRIO_DEFAULT: u32 = RP_THREAD_PRIO_MAX;

pub const SCREEN_COUNT: u32 = RP_SCREEN_COUNT as u32;
pub const WORK_COUNT: u32 = NWM_WORK_COUNT;

pub type WorkIndex = Ranged<WORK_COUNT>;
pub type ThreadIndex = Ranged<RP_CORE_COUNT_MAX>;
pub type ScreenIndex = Ranged<SCREEN_COUNT>;
pub type CoreCount = IRanged<RP_CORE_COUNT_MIN, RP_CORE_COUNT_MAX>;

#[cfg(not(feature = "mem3"))]
pub const fn img_buffer_size(is_top: bool) -> usize {
    (GSP_SCREEN_WIDTH
        * 4 // max bpp
        * if is_top {
            GSP_SCREEN_HEIGHT_TOP
        } else {
            GSP_SCREEN_HEIGHT_BOTTOM
        }) as usize
}

#[allow(unused)]
#[derive(ConstDefault)]
pub struct WorkHandles {
    pub nwm_ready: Handle,
    pub nwm_done: Handle,
    pub work_ready_flag: AtomicBool,
    pub work_done_count: AtomicU32,
    pub work_done_flag: AtomicBool,
    pub work_done: Handle,
}

#[allow(unused)]
type WorksHandles = RangedArray<WorkHandles, WORK_COUNT>;

#[allow(unused)]
#[derive(ConstDefault)]
pub struct ThreadHandles {
    pub thread_ready: Handle,
    pub work_ready: Handle,
}

#[allow(unused)]
type ThreadsHandles = RangedArray<ThreadHandles, RP_CORE_COUNT_MAX>;

#[allow(unused)]
type ScreensHandles = RangedArray<Handle, SCREEN_COUNT>;

#[cfg(not(feature = "o3ds"))]
#[derive(ConstDefault)]
pub struct SynHandles {
    pub works: WorksHandles,
    pub threads: ThreadsHandles,

    pub nwm_ready: Handle,
    pub screen_ready: Handle,
    pub screens_port_ready: ScreensHandles,
}

#[cfg(not(feature = "o3ds"))]
pub static mut SYN_HANDLES: SynHandles = const_default();

pub fn thread_index_last(core_count: CoreCount) -> ThreadIndex {
    ThreadIndex::init(core_count.get() - 1)
}

#[named]
#[cfg(not(feature = "o3ds"))]
pub unsafe fn init_syn_handles(core_count: CoreCount) -> Option<()> {
    unsafe {
        for i in ScreenIndex::all() {
            let res = create_event(SYN_HANDLES.screens_port_ready.get_mut(&i));
            if res != 0 {
                ns_dbg_print!(create_event_failed, c_str!("syn screens_port_ready"), res);
                return None;
            }
        }

        let res = create_event(&mut SYN_HANDLES.nwm_ready);
        if res != 0 {
            ns_dbg_print!(create_event_failed, c_str!("syn nwm_ready"), res);
            return None;
        }

        let res = svcCreateSemaphore(&mut SYN_HANDLES.screen_ready, 1, 1);
        if res != 0 {
            ns_dbg_print!(create_semaphore_failed, c_str!("syn screen_ready"), res);
            return None;
        }

        for i in WorkIndex::all() {
            let work = SYN_HANDLES.works.get_mut(&i);

            let res = svcCreateSemaphore(&mut work.work_done, 1, 1);
            if res != 0 {
                ns_dbg_print!(create_semaphore_failed, c_str!("work work_done"), res);
                return None;
            }

            let res = svcCreateSemaphore(&mut work.nwm_done, 1, 1);
            if res != 0 {
                ns_dbg_print!(create_semaphore_failed, c_str!("work nwm_done"), res);
                return None;
            }

            let res = svcCreateSemaphore(&mut work.nwm_ready, 0, 1);
            if res != 0 {
                ns_dbg_print!(create_semaphore_failed, c_str!("work nwm_ready"), res);
                return None;
            }

            work.work_ready_flag.store(false, Ordering::Release);
            work.work_done_count.store(0, Ordering::Release);
            work.work_done_flag.store(false, Ordering::Release);
        }

        for j in ThreadIndex::up_to(&thread_index_last(core_count)) {
            let thread = SYN_HANDLES.threads.get_mut(&j);

            let res = svcCreateSemaphore(&mut thread.thread_ready, 0, WORK_COUNT as i32);
            if res != 0 {
                ns_dbg_print!(create_semaphore_failed, c_str!("thread thread_ready"), res);
                return None;
            }

            let res = svcCreateSemaphore(&mut thread.work_ready, 0, WORK_COUNT as i32);
            if res != 0 {
                ns_dbg_print!(create_semaphore_failed, c_str!("thread work_ready"), res);
                return None;
            }
        }
    }
    Some(())
}

#[cfg(not(feature = "o3ds"))]
pub unsafe fn cleanup_syn_handles(core_count: CoreCount) {
    unsafe {
        for j in ThreadIndex::up_to(&thread_index_last(core_count)) {
            let thread = SYN_HANDLES.threads.get_mut(&j);

            let _ = svcCloseHandle(thread.work_ready);
            let _ = svcCloseHandle(thread.thread_ready);
        }

        for i in WorkIndex::all() {
            let work = SYN_HANDLES.works.get_mut(&i);

            let _ = svcCloseHandle(work.nwm_ready);
            let _ = svcCloseHandle(work.nwm_done);
            let _ = svcCloseHandle(work.work_done);
        }

        let _ = svcCloseHandle(SYN_HANDLES.screen_ready);
        let _ = svcCloseHandle(SYN_HANDLES.nwm_ready);

        for i in ScreenIndex::all() {
            let _ = svcCloseHandle(*SYN_HANDLES.screens_port_ready.get(&i));
        }
    }
}

static mut RESET_THREADS: AtomicBool = const_default();

#[must_use]
pub fn reset_threads() -> bool {
    unsafe { RESET_THREADS.load(Ordering::Acquire) }
}

pub fn set_reset_threads() {
    unsafe { RESET_THREADS.store(true, Ordering::Release) }
}

pub fn clear_reset_threads() {
    unsafe { RESET_THREADS.store(false, Ordering::Release) }
}

static mut CORE_COUNT_IN_USE: CoreCount = CoreCount::init(RP_CORE_COUNT_MAX);

pub fn core_count_in_use() -> CoreCount {
    unsafe { CORE_COUNT_IN_USE }
}

pub unsafe fn set_core_count_in_use(v: u32) {
    unsafe { CORE_COUNT_IN_USE.set(v) }
}
