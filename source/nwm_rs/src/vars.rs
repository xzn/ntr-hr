use crate::*;

pub const rp_config: *mut RP_CONFIG =
    (NS_CONFIG_ADDR as usize + mem::offset_of!(NS_CONFIG, rpConfig)) as *mut RP_CONFIG;

pub const rp_config_u32_count: usize = mem::size_of::<RP_CONFIG>() / mem::size_of::<u32_>();

pub const ntr_config: *mut NTR_CONFIG =
    (NS_CONFIG_ADDR as usize + mem::offset_of!(NS_CONFIG, ntrConfig)) as *mut NTR_CONFIG;

// CSTATE_START from jpegint.h
pub const JPEG_CSTATE_START: c_int = 100;

pub const THREAD_WAIT_NS: s64 = 100_000_000;

// From Luma3DS
pub const GPU_FB_TOP_SIZE: u32_ = IoBasePdc + 0x45c;
pub const GPU_FB_TOP_LEFT_ADDR_1: u32_ = IoBasePdc + 0x468;
pub const GPU_FB_TOP_LEFT_ADDR_2: u32_ = IoBasePdc + 0x46C;
pub const GPU_FB_TOP_FMT: u32_ = IoBasePdc + 0x470;
pub const GPU_FB_TOP_SEL: u32_ = IoBasePdc + 0x478;
pub const GPU_FB_TOP_COL_LUT_INDEX: u32_ = IoBasePdc + 0x480;
pub const GPU_FB_TOP_COL_LUT_ELEM: u32_ = IoBasePdc + 0x484;
pub const GPU_FB_TOP_STRIDE: u32_ = IoBasePdc + 0x490;
pub const GPU_FB_TOP_RIGHT_ADDR_1: u32_ = IoBasePdc + 0x494;
pub const GPU_FB_TOP_RIGHT_ADDR_2: u32_ = IoBasePdc + 0x498;

pub const GPU_FB_BOTTOM_SIZE: u32_ = IoBasePdc + 0x55c;
pub const GPU_FB_BOTTOM_ADDR_1: u32_ = IoBasePdc + 0x568;
pub const GPU_FB_BOTTOM_ADDR_2: u32_ = IoBasePdc + 0x56C;
pub const GPU_FB_BOTTOM_FMT: u32_ = IoBasePdc + 0x570;
pub const GPU_FB_BOTTOM_SEL: u32_ = IoBasePdc + 0x578;
pub const GPU_FB_BOTTOM_COL_LUT_INDEX: u32_ = IoBasePdc + 0x580;
pub const GPU_FB_BOTTOM_COL_LUT_ELEM: u32_ = IoBasePdc + 0x584;
pub const GPU_FB_BOTTOM_STRIDE: u32_ = IoBasePdc + 0x590;

#[derive(Copy, Clone, ConstDefault, ConstParamTy, Eq, PartialEq)]
pub struct IRanged<const BEG: u32_, const END: u32_>(u32_);

pub struct IRangedIter<const BEG: u32_, const END: u32_>(u32_);

impl<const BEG: u32_, const END: u32_> Iterator for IRangedIter<BEG, END> {
    type Item = IRanged<BEG, END>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.0 > END {
            None
        } else {
            let r = unsafe { IRanged::<BEG, END>::init_unchecked(self.0) };
            self.0 += 1;
            Some(r)
        }
    }
}

pub struct IRangedIterN<const BEG: u32_, const END: u32_>(u32_, u32_);

impl<const BEG: u32_, const END: u32_> Iterator for IRangedIterN<BEG, END> {
    type Item = IRanged<BEG, END>;

    fn next(&mut self) -> Option<Self::Item> {
        if self.0 > self.1 {
            None
        } else {
            let r = unsafe { IRanged::<BEG, END>::init_unchecked(self.0) };
            self.0 += 1;
            Some(r)
        }
    }
}

impl<const BEG: u32_, const END: u32_> IRanged<BEG, END> {
    pub fn all() -> IRangedIter<BEG, END> {
        IRangedIter::<BEG, END>(BEG)
    }

    pub unsafe fn up_to_unchecked(n: u32_) -> IRangedIterN<BEG, END> {
        IRangedIterN::<BEG, END>(BEG, n)
    }
}

impl<const BEG: u32_, const END: u32_> IRanged<BEG, END> {
    pub const fn init() -> Self {
        Self(0)
    }

    pub unsafe fn init_unchecked(v: u32_) -> Self {
        Self(v)
    }

    pub const fn get(&self) -> u32_ {
        self.0
    }

    pub fn set(&mut self, v: u32_) {
        if v < BEG {
            self.0 = BEG
        } else if v > END {
            self.0 = END
        } else {
            self.0 = v
        }
    }

    fn next_wrapped(&mut self) {
        if self.0 == END {
            self.0 = BEG
        } else {
            self.0 += 1
        }
    }
}

impl<const BEG: u32_, const END: u32_> IRanged<BEG, END>
where
    [(); { END - 1 } as usize]:,
{
    fn from_bool(v: bool) -> Self {
        unsafe { Self::init_unchecked(v as u32_) }
    }
}

pub type CoreCount = IRanged<RP_CORE_COUNT_MIN, RP_CORE_COUNT_MAX>;

pub static mut thread_main_handle: Handle = 0;
pub static mut reset_threads: bool = false;
pub static mut core_count_in_use: CoreCount = CoreCount::init();

pub static mut min_send_interval_tick: u32_ = 0;
pub static mut min_send_interval_ns: u32_ = 0;

pub const SCREEN_COUNT: u32_ = 2;
pub const WORK_COUNT: u32_ = 2;
pub const CINFOS_COUNT: u32_ = SCREEN_COUNT * WORK_COUNT * RP_CORE_COUNT_MAX;

pub type Ranged<const N: u32_> = IRanged<0, { N - 1 }>;

pub type WorkIndex = Ranged<WORK_COUNT>;
pub type ThreadId = Ranged<RP_CORE_COUNT_MAX>;

pub struct RangedArraySlice<'a, T, const N: u32_>(pub &'a mut RangedArray<T, N>)
where
    [(); N as usize]:;

#[derive(Copy, Clone, ConstDefault)]
pub struct RangedArray<T, const N: u32_>([T; N as usize])
where
    [(); N as usize]:;

impl<T: Copy, const N: u32_> RangedArray<T, N>
where
    [(); N as usize]:,
{
    pub fn as_mut_ptr(&mut self) -> *mut T {
        self.0.as_mut_ptr()
    }

    pub fn as_ptr(&self) -> *const T {
        self.0.as_ptr()
    }

    pub fn get(&self, i: &Ranged<N>) -> &T
    where
        [(); { N - 1 } as usize]:,
    {
        unsafe { self.0.get_unchecked(i.0 as usize) }
    }

    pub fn get_mut<const N2: u32_>(&mut self, i: &Ranged<N2>) -> &mut T
    where
        [(); { N - N2 } as usize]:,
        [(); { N2 - 1 } as usize]:,
    {
        unsafe { self.0.get_unchecked_mut(i.0 as usize) }
    }

    pub fn get_b(&self, b: bool) -> &T
    where
        [(); { N - 2 } as usize]:,
    {
        unsafe { self.0.get_unchecked(b as usize) }
    }

    pub fn get_b_mut(&mut self, b: bool) -> &mut T
    where
        [(); { N - 2 } as usize]:,
    {
        unsafe { self.0.get_unchecked_mut(b as usize) }
    }

    pub fn split_at_mut<const I: u32_>(
        &mut self,
    ) -> (RangedArraySlice<T, I>, RangedArraySlice<T, { N - I }>)
    where
        [(); I as usize]:,
        [(); { N - I } as usize]:,
    {
        unsafe {
            let a = self.0.as_mut_ptr();
            let b = a.add(I as usize);
            (
                RangedArraySlice::<T, I>(mem::transmute(a)),
                RangedArraySlice::<T, { N - I }>(mem::transmute(b)),
            )
        }
    }
}

#[derive(Copy, Clone, ConstDefault)]
pub struct AllocStats {
    pub qual: rp_alloc_stats,
    pub comp: rp_alloc_stats,
}

#[derive(Copy, Clone, ConstDefault)]
pub struct CInfo {
    pub info: jpeg_compress_struct,
    pub alloc_stats: AllocStats,
}

pub type CInfos =
    RangedArray<RangedArray<RangedArray<CInfo, RP_CORE_COUNT_MAX>, WORK_COUNT>, SCREEN_COUNT>;

pub type CInfosAll = RangedArray<CInfo, CINFOS_COUNT>;

pub static mut cinfos: CInfos = <CInfos as ConstDefault>::DEFAULT;
pub static mut cinfos_all: *mut CInfosAll = ptr::null_mut();

pub static mut jerr: jpeg_error_mgr = <jpeg_error_mgr as ConstDefault>::DEFAULT;

pub type RowIndexes = RangedArray<u32_, RP_CORE_COUNT_MAX>;

#[derive(Copy, Clone, ConstDefault)]
pub struct LoadAndProgress {
    pub n: u32_,
    pub n_last: u32_,
    pub n_adjusted: u32_,
    pub n_last_adjusted: u32_,
    pub p: RowIndexes,
    pub p_snapshot: RowIndexes,
}

pub type LoadAndProgresses = RangedArray<LoadAndProgress, WORK_COUNT>;
pub static mut load_and_progresses: LoadAndProgresses =
    <LoadAndProgresses as ConstDefault>::DEFAULT;

#[derive(Copy, Clone, ConstDefault)]
pub struct Buffers {
    pub prep: [JSAMPARRAY; MAX_COMPONENTS as usize],
    pub color: [JSAMPARRAY; MAX_COMPONENTS as usize],
    pub mcu: [JBLOCKROW; C_MAX_BLOCKS_IN_MCU as usize],
}

pub type WorkBuffers = RangedArray<RangedArray<Buffers, RP_CORE_COUNT_MAX>, WORK_COUNT>;
pub static mut work_buffers: WorkBuffers = <WorkBuffers as ConstDefault>::DEFAULT;

#[derive(Copy, Clone, ConstDefault)]
pub struct BlitCtx {
    pub width: u32_,
    pub height: u32_,
    pub format: u32_,
    pub src: *const u8_,
    pub src_pitch: u32_,
    pub bpp: u32_,

    pub frame_id: u8_,
    pub is_top: bool,

    pub cinfo: *mut CInfo,

    pub i_start: RowIndexes,
    pub i_count: RowIndexes,

    pub should_capture: bool,
}

pub type BlitCtxes = RangedArray<BlitCtx, WORK_COUNT>;
pub static mut blit_ctxes: BlitCtxes = <BlitCtxes as ConstDefault>::DEFAULT;

pub const NWM_HDR_SIZE: usize = 0x2a + 8;
pub const DATA_HDR_SIZE: usize = 4;
pub static mut last_send_tick: u32_ = 0;
pub type NwmHdr = [u8_; NWM_HDR_SIZE];
pub static mut current_nwm_hdr: NwmHdr = <NwmHdr as ConstDefault>::DEFAULT;

pub const PACKET_SIZE: usize = 1448;
pub const PACKET_DATA_SIZE: usize = PACKET_SIZE - DATA_HDR_SIZE;

#[derive(Copy, Clone, ConstDefault)]
pub struct DataBufInfo {
    pub send_pos: *const u8_,
    pub pos: *mut u8_,
    pub flag: u32_,
}

#[derive(Copy, Clone, ConstDefault)]
pub struct NwmInfo {
    pub buf: *mut u8_,
    pub buf_packet_last: *mut u8_,
    pub info: DataBufInfo,
}

pub type NwmThreadInfos = RangedArray<NwmInfo, RP_CORE_COUNT_MAX>;
pub type NwmInfos = RangedArray<NwmThreadInfos, WORK_COUNT>;
pub static mut nwm_infos: NwmInfos = <NwmInfos as ConstDefault>::DEFAULT;

pub static mut nwm_work_index: WorkIndex = WorkIndex::init();
pub static mut nwm_thread_id: ThreadId = ThreadId::init();

pub static mut screen_work_index: WorkIndex = WorkIndex::init();
pub static mut screen_thread_id: ThreadId = ThreadId::init();

pub static mut skip_frame: RangedArray<bool, WORK_COUNT> =
    <RangedArray<bool, WORK_COUNT> as ConstDefault>::DEFAULT;

pub static mut nwm_need_syn: RangedArray<bool, WORK_COUNT> =
    <RangedArray<bool, WORK_COUNT> as ConstDefault>::DEFAULT;

pub static mut data_buf_hdrs: RangedArray<NwmHdr, WORK_COUNT> =
    <RangedArray<NwmHdr, WORK_COUNT> as ConstDefault>::DEFAULT;

pub const IMG_BUFFER_SIZE: usize = 0x60000;
pub const NWM_BUFFER_SIZE: usize = 0x28000;

pub const IMG_WORK_COUNT: u32_ = 2;
pub type ImgWorkIndex = Ranged<IMG_WORK_COUNT>;
pub type ImgBufs = RangedArray<*mut u8_, IMG_WORK_COUNT>;

#[derive(Copy, Clone, ConstDefault)]
pub struct ImgInfo {
    pub bufs: ImgBufs,
    pub index: ImgWorkIndex,
}

pub type ImgInfos = RangedArray<ImgInfo, SCREEN_COUNT>;
pub static mut img_infos: ImgInfos = <ImgInfos as ConstDefault>::DEFAULT;

#[derive(Copy, Clone, ConstDefault)]
pub struct CapInfo {
    pub src: *mut u8_,
    pub pitch: u32_,
    pub format: u32_,
}

type DmaHandles = RangedArray<Handle, WORK_COUNT>;

#[derive(Copy, Clone, ConstDefault)]
pub struct CapParams {
    pub dmas: DmaHandles,
    pub game: Handle,
    pub home: Handle,
    pub game_pid: u32_,
    pub game_fcram_base: u32_,
}

pub static mut cap_params: CapParams = <CapParams as ConstDefault>::DEFAULT;

pub static mut cap_info: CapInfo = <CapInfo as ConstDefault>::DEFAULT;

pub static mut current_frame_ids: RangedArray<u8_, SCREEN_COUNT> =
    <RangedArray<u8_, SCREEN_COUNT> as ConstDefault>::DEFAULT;

pub static mut frame_counts: RangedArray<u32_, SCREEN_COUNT> =
    <RangedArray<u32_, SCREEN_COUNT> as ConstDefault>::DEFAULT;

pub static mut frame_queues: RangedArray<u32_, SCREEN_COUNT> =
    <RangedArray<u32_, SCREEN_COUNT> as ConstDefault>::DEFAULT;

pub static mut port_game_pid: u32_ = 0;

pub static mut currently_updating: bool = false;
pub static mut priority_is_top: bool = false;
pub static mut priority_factor: u32_ = 0;
pub static mut priority_factor_scaled: u32_ = 0;

pub static mut screens_captured: RangedArray<bool, WORK_COUNT> =
    <RangedArray<bool, WORK_COUNT> as ConstDefault>::DEFAULT;
pub static mut screens_synced: RangedArray<bool, WORK_COUNT> =
    <RangedArray<bool, WORK_COUNT> as ConstDefault>::DEFAULT;

#[derive(Copy, Clone, ConstDefault)]
pub struct PerWorkHandles {
    pub nwm_done: Handle,
    pub nwm_ready: Handle,
    pub work_done_count: u32_,
    pub work_done: Handle,
    pub work_begin_flag: bool,
}

type WorkHandles = RangedArray<PerWorkHandles, WORK_COUNT>;

#[derive(Copy, Clone, ConstDefault)]
pub struct PerThreadHandles {
    pub work_ready: Handle,
    pub work_begin_ready: Handle,
}

type ThreadHandles = RangedArray<PerThreadHandles, RP_CORE_COUNT_MAX>;

type PortScreenHandles = RangedArray<Handle, SCREEN_COUNT>;

#[derive(Copy, Clone, ConstDefault)]
pub struct SynHandles {
    pub works: WorkHandles,
    pub threads: ThreadHandles,

    pub nwm_ready: Handle,
    pub port_screen_ready: PortScreenHandles,
    pub screen_ready: Handle,
}

pub static mut syn_handles: *mut SynHandles = ptr::null_mut();
