use crate::*;

pub const rp_config: *mut RP_CONFIG =
    (NS_CONFIG_ADDR as usize + mem::offset_of!(NS_CONFIG, rpConfig)) as *mut RP_CONFIG;

pub const rp_config_u32_count: usize = mem::size_of::<RP_CONFIG>() / mem::size_of::<u32_>();

pub const ntr_config: *mut NTR_CONFIG =
    (NS_CONFIG_ADDR as usize + mem::offset_of!(NS_CONFIG, ntrConfig)) as *mut NTR_CONFIG;

pub const JPEG_SAMP_FACTOR: c_int = 2;
pub const THREAD_WAIT_NS: s64 = NWM_THREAD_WAIT_NS as s64;

mod gpu;
mod ranged;

pub use gpu::*;
pub use ranged::*;

pub type CoreCount = IRanged<RP_CORE_COUNT_MIN, RP_CORE_COUNT_MAX>;

pub static mut thread_main_handle: Handle = 0;

pub const SCREEN_COUNT: u32_ = 2;
pub const WORK_COUNT: u32_ = NWM_WORK_COUNT;

pub type WorkIndex = Ranged<WORK_COUNT>;
pub type ThreadId = Ranged<RP_CORE_COUNT_MAX>;
pub type ScreenIndex = Ranged<SCREEN_COUNT>;

pub const IMG_BUFFER_SIZE: usize = 0x60000;
pub const NWM_BUFFER_SIZE: usize = (SEND_BUFS_SIZE / WORK_COUNT) as usize;

pub const RP_THREAD_PRIO_DEFAULT: u32 = RP_THREAD_PRIO_MAX;

pub static mut home_process_handle: Handle = 0;

#[derive(ConstDefault)]
pub struct PerWorkHandles {
    pub nwm_done: Handle,
    pub nwm_ready: Handle,
    pub work_done_count: AtomicU32,
    pub work_done: Handle,
    pub work_begin_flag: AtomicBool,
}

type WorkHandles = RangedArray<PerWorkHandles, WORK_COUNT>;

#[derive(ConstDefault)]
pub struct PerThreadHandles {
    pub work_ready: Handle,
    pub work_begin_ready: Handle,
}

type ThreadHandles = RangedArray<PerThreadHandles, RP_CORE_COUNT_MAX>;

type PortScreenHandles = RangedArray<Handle, SCREEN_COUNT>;

#[derive(ConstDefault)]
pub struct SynHandles {
    pub works: WorkHandles,
    pub threads: ThreadHandles,

    pub nwm_ready: Handle,
    pub port_screen_ready: PortScreenHandles,
    pub screen_ready: Handle,
}

pub static mut syn_handles: *mut SynHandles = ptr::null_mut();

pub const fn J_MAX_HALF_FACTOR(v: u32_) -> u32_ {
    v / 2
}

pub static mut reliable_stream_cb: *mut rp_cb = const_default();
pub static mut reliable_stream_cb_lock: Handle = 0;
pub static mut reliable_stream_cb_evt: Handle = 0;
pub static mut seg_mem_sem: Handle = 0;
pub static mut seg_mem_lock: Handle = 0;
pub static mut recv_seg_mem_inited: AtomicBool = const_default();
