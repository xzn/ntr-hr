use super::*;

#[cfg(not(feature = "o3ds"))]
pub type ImgWorkIndex = Ranged<IMG_WORK_COUNT>;
#[cfg(not(feature = "o3ds"))]
pub type ImgBufs = RangedArray<*mut u8, IMG_WORK_COUNT>;
#[cfg(not(feature = "o3ds"))]
pub const IMG_WORK_COUNT: u32 = 2;

#[cfg(not(feature = "o3ds"))]
#[derive(ConstDefault)]
pub struct ImgInfo {
    pub bufs: ImgBufs,
    pub index: ImgWorkIndex,
}

#[cfg(not(feature = "o3ds"))]
pub type ImgInfos = RangedArray<ImgInfo, SCREEN_COUNT>;
#[cfg(not(feature = "o3ds"))]
pub static mut IMG_INFOS: ImgInfos = const_default();

#[cfg(feature = "o3ds")]
pub static mut IMG_INFO: *mut u8 = const_default();

#[cfg(not(feature = "o3ds"))]
pub unsafe fn once_img_infos() -> Option<()> {
    for i in ScreenIndex::all() {
        let is_top = if i.get() == 0 { true } else { false };
        for j in ImgWorkIndex::all() {
            if let Some(m) = request_mem_from_pool_vsize(img_buffer_size(is_top)) {
                unsafe {
                    *IMG_INFOS.get_mut(&i).bufs.get_mut(&j) = m.as_mut_ptr();
                }
            } else {
                return None;
            }
        }
    }
    Some(())
}

#[cfg(all(feature = "o3ds", not(feature = "mem3")))]
pub unsafe fn once_img_infos() -> Option<()> {
    if let Some(m) = request_mem_from_pool_vsize(img_buffer_size(true)) {
        unsafe {
            IMG_INFO = m.as_mut_ptr();
        }
    } else {
        return None;
    }
    Some(())
}

#[derive(ConstDefault)]
pub struct Config {
    pub priority_is_top: bool,
    pub priority_factor: u32,
    pub priority_factor_scaled: u32,
    pub frame_counts: FrameCounts,
    pub frame_queues: FrameQueues,
    pub frame_timing_allowance: u32,
}

type FrameCounts = RangedArray<u32, SCREEN_COUNT>;
type FrameQueues = RangedArray<u32, SCREEN_COUNT>;

static mut CONFIG: Config = const_default();
static mut FULL_WIDTH: u32 = const_default();

#[allow(unused)]
#[derive(PartialEq, Eq)]
pub enum TopScreenWidth {
    Left,
    Right,
    Both,
}

#[allow(unused)]
pub fn top_screen_width() -> TopScreenWidth {
    unsafe {
        if FULL_WIDTH >= 2 {
            #[cfg(not(feature = "o3ds"))]
            {
                TopScreenWidth::Both
            }
            #[cfg(feature = "o3ds")]
            {
                TopScreenWidth::Left
            }
        } else if FULL_WIDTH >= 1 {
            TopScreenWidth::Right
        } else {
            TopScreenWidth::Left
        }
    }
}

#[cfg(not(feature = "o3ds"))]
const FRAME_TIMING_FACTOR_DQ: u32 = 2;
#[cfg(not(feature = "o3ds"))]
pub fn frame_timing_allowance() -> u32 {
    unsafe { CONFIG.frame_timing_allowance }
}

#[derive(ConstDefault)]
pub struct Params {
    #[cfg(not(feature = "o3ds"))]
    work_index: WorkIndex,
    #[cfg(not(feature = "o3ds"))]
    thread_index: ThreadIndex,

    work_ready: WorkReady,

    #[cfg(not(feature = "o3ds"))]
    skip_frames: SkipFrames,
}

#[cfg(not(feature = "o3ds"))]
pub unsafe fn work_index_next_wrapped() {
    unsafe {
        let a = PARAMS.work_index.atomic();
        let mut curr = a.load(Ordering::Acquire);
        loop {
            let mut w = WorkIndex::init_unchecked(curr);
            w.next_wrapped();
            match a.compare_exchange_weak(curr, w.get(), Ordering::AcqRel, Ordering::Acquire) {
                Ok(_) => break,
                Err(temp) => curr = temp,
            }
        }
    }
}

static mut PARAMS: Params = const_default();

pub fn close_handles() {
    if let Some(lock) = screen_params_lock() {
        close_game_handle(lock.params())
    }
}

#[cfg(not(feature = "o3ds"))]
static mut PORT_GAME_PID: AtomicU32 = const_default();

#[cfg(not(feature = "o3ds"))]
pub fn set_port_game_pid(v: u32) {
    unsafe { PORT_GAME_PID.store(v, Ordering::Release) }
}

#[cfg(not(feature = "o3ds"))]
pub fn port_game_pid() -> u32 {
    unsafe { PORT_GAME_PID.load(Ordering::Acquire) }
}

#[cfg(not(feature = "o3ds"))]
static mut NO_SKIP_FRAMES: RangedArray<AtomicBool, SCREEN_COUNT> = const_default();

#[cfg(not(feature = "o3ds"))]
pub fn reset_no_skip_frame(is_top: bool) {
    unsafe {
        NO_SKIP_FRAMES
            .get_mut(&is_top_index(is_top))
            .store(false, Ordering::Release);
    }
}

#[cfg(not(feature = "o3ds"))]
pub fn set_no_skip_frame(is_top: bool) {
    unsafe {
        NO_SKIP_FRAMES
            .get_mut(&is_top_index(is_top))
            .store(true, Ordering::Release);
    }
}

#[cfg(not(feature = "o3ds"))]
pub fn no_skip_frame(is_top: bool) -> bool {
    unsafe {
        NO_SKIP_FRAMES
            .get(&is_top_index(is_top))
            .load(Ordering::Acquire)
    }
}

pub static mut SCREEN_HANDLES_LOCK: Handle = const_default();
pub static mut SCREEN_HANDLES_INITED: AtomicBool = const_default();

#[named]
pub unsafe fn once_screen_handles() -> Option<()> {
    unsafe {
        let res = svcCreateMutex(&mut SCREEN_HANDLES_LOCK, false);
        if res != 0 {
            ns_dbg_print!(failed, c_str!("Create screen handles mutex"), res);
            return None;
        }
        SCREEN_HANDLES_INITED.store(true, Ordering::Release);
    }
    Some(())
}

#[cfg(not(feature = "o3ds"))]
pub fn thread_screen_loop() -> Option<()> {
    loop {
        safe_impl::thread_screen(Impl(()))?
    }
}

#[cfg(not(feature = "o3ds"))]
pub extern "C" fn thread_screen(_: *mut c_void) {
    unsafe {
        __system_initSyscalls();
        thread_screen_loop();
        svcExitThread()
    }
}

#[cfg(not(feature = "o3ds"))]
pub fn set_no_skip_frames() {
    set_no_skip_frame(true);
    set_no_skip_frame(false);

    let _ = unsafe { svcSignalEvent(*SYN_HANDLES.screens_port_ready.get(&is_top_index(true))) };
    let _ = unsafe { svcSignalEvent(*SYN_HANDLES.screens_port_ready.get(&is_top_index(false))) };
}

pub unsafe fn init(mode: u32, full_width: u32) {
    unsafe { FULL_WIDTH = full_width };

    let is_top = (mode & 0xff00) > 0;
    let factor = mode & 0xff;

    let conf = unsafe { &mut CONFIG };
    conf.priority_is_top = is_top;
    conf.priority_factor = factor;
    conf.priority_factor_scaled = fix(factor as c_double);

    #[cfg(not(feature = "o3ds"))]
    set_no_skip_frames();

    for i in ScreenIndex::all() {
        *conf.frame_counts.get_mut(&i) = 1;
        *conf.frame_queues.get_mut(&i) = conf.priority_factor_scaled;
    }

    #[cfg(not(feature = "o3ds"))]
    {
        let params = unsafe { &mut PARAMS };
        for i in WorkIndex::all() {
            *params.skip_frames.get_mut(&i) = false;
        }
        params.work_index = WorkIndex::init(0);
        params.thread_index = ThreadIndex::init(0);
    }

    #[cfg(not(feature = "o3ds"))]
    for i in WorkIndex::all() {
        unsafe {
            SYN_HANDLES
                .works
                .get_mut(&i)
                .work_done_flag
                .store(false, Ordering::Release)
        };
    }

    #[cfg(not(feature = "o3ds"))]
    {
        conf.frame_timing_allowance = if entries::thread_nwm::get_reliable_stream_delta_prog() {
            SYSCLOCK_ARM11 / FRAME_TIMING_FACTOR_DQ
        } else {
            SYSCLOCK_ARM11
        }
    }

    #[cfg(feature = "o3ds")]
    {
        conf.frame_timing_allowance = SYSCLOCK_ARM11
    }
}

#[cfg(not(feature = "o3ds"))]
pub struct Impl(());

#[cfg(not(feature = "o3ds"))]
impl Impl {
    #[named]
    #[cfg(not(feature = "o3ds"))]
    pub fn screen_ready_acquire(self) -> Option<ScreenReady> {
        wait_syn(
            cname!(),
            unsafe { SYN_HANDLES.screen_ready },
            c_str!("screen_ready"),
        )?;

        Some(ScreenReady(()))
    }
}

#[cfg(not(feature = "o3ds"))]
pub struct ScreenReady(());

#[cfg(not(feature = "o3ds"))]
impl ScreenReady {
    #[named]
    #[cfg(not(feature = "o3ds"))]
    pub fn work_done_acquire(self) -> Option<WorkDone> {
        unsafe {
            let w = PARAMS.work_index.get_atomic();
            let w = SYN_HANDLES.works.get_mut(&w);
            let flag = &mut w.work_done_flag;

            if !flag.load(Ordering::Acquire) {
                wait_syn(cname!(), w.work_done, c_str!("work_done"))?;
                flag.store(true, Ordering::Release);
            }
        }

        Some(WorkDone(()))
    }
}

#[cfg(not(feature = "o3ds"))]
pub unsafe fn work_done_flag_release(w: WorkIndex) {
    unsafe {
        let w = SYN_HANDLES.works.get_mut(&w);
        w.work_done_flag.store(false, Ordering::Release);
    }
}

#[cfg(not(feature = "o3ds"))]
pub struct WorkDone(());

#[cfg(not(feature = "o3ds"))]
impl WorkDone {
    pub fn do_screen<'a>(&'a self) -> Screen<'a> {
        return Screen(PhantomData);
    }
}

#[allow(unused)]
pub const EYE_LEFT: usize = 0;
#[allow(unused)]
pub const EYE_RIGHT: usize = 1;
#[allow(unused)]
pub const EYE_COUNT: usize = 2;

#[derive(ConstDefault)]
pub struct WorkReadyParams {
    pub is_top: bool,
    pub format: u32,
    #[cfg(feature = "mem3")]
    pub pitch: u32,
    #[cfg(not(feature = "o3ds"))]
    pub dma: [Handle; EYE_COUNT],
    #[cfg(not(feature = "o3ds"))]
    pub both_eyes: bool,
}

type WorkReady = RangedArray<WorkReadyParams, WORK_COUNT>;
#[cfg(not(feature = "o3ds"))]
type SkipFrames = RangedArray<bool, WORK_COUNT>;

#[named]
#[allow(unused_macros)]
fn thread_ready_release(
    is_top: bool,
    format: u32,
    #[cfg(feature = "mem3")] pitch: u32,
    #[cfg(not(feature = "o3ds"))] work_index: WorkIndex,
    #[cfg(not(feature = "o3ds"))] dma: [Handle; EYE_COUNT],
    #[cfg(not(feature = "o3ds"))] both_eyes: bool,
) {
    unsafe {
        #[cfg(feature = "o3ds")]
        let work_index = WorkIndex::init(0);

        #[cfg(not(feature = "mem3"))]
        {
            *PARAMS.work_ready.get_mut(&work_index) = WorkReadyParams {
                is_top,
                format,
                #[cfg(not(feature = "o3ds"))]
                dma,
                #[cfg(not(feature = "o3ds"))]
                both_eyes,
            };
        }
        #[cfg(feature = "mem3")]
        {
            *PARAMS.work_ready.get_mut(&work_index) = WorkReadyParams {
                is_top,
                format,
                pitch,
            };
        }

        #[cfg(not(feature = "o3ds"))]
        {
            if *PARAMS.skip_frames.get(&work_index) {
                release_sem(
                    cname!(),
                    SYN_HANDLES.threads.get(&PARAMS.thread_index).thread_ready,
                    c_str!("thread_ready"),
                );
            } else {
                for j in ThreadIndex::up_to(&thread_index_last(core_count_in_use())) {
                    release_sem(
                        cname!(),
                        SYN_HANDLES.threads.get(&j).thread_ready,
                        c_str!("thread_ready"),
                    );
                }
            }
        }
    }
}

#[named]
#[cfg(not(feature = "o3ds"))]
pub fn thread_ready_acquire(t: &ThreadIndex) -> Option<()> {
    unsafe {
        wait_syn(
            cname!(),
            SYN_HANDLES.threads.get(t).thread_ready,
            c_str!("thread_ready"),
        )?;

        Some(())
    }
}

#[cfg(feature = "o3ds")]
pub fn thread_ready_acquire() -> Option<()> {
    safe_impl::thread_screen()
}

#[named]
#[cfg(not(feature = "o3ds"))]
pub unsafe fn screen_ready_release() {
    unsafe { release_sem(cname!(), SYN_HANDLES.screen_ready, c_str!("screen_ready")) }
}

#[cfg(not(feature = "o3ds"))]
pub enum SkipFrameParams {
    Frame,
    SkipFrame(ThreadIndex),
}

#[cfg(not(feature = "o3ds"))]
pub unsafe fn skip_frame_release(work_index: WorkIndex, skip_frame: SkipFrameParams) {
    unsafe {
        match skip_frame {
            SkipFrameParams::Frame => *PARAMS.skip_frames.get_mut(&work_index) = false,
            SkipFrameParams::SkipFrame(t) => {
                *PARAMS.skip_frames.get_mut(&work_index) = true;
                PARAMS.thread_index = t;
                screen_ready_release()
            }
        }
    }
}

pub unsafe fn work_ready_params(w: &WorkIndex) -> &mut WorkReadyParams {
    unsafe { PARAMS.work_ready.get_mut(w) }
}

pub struct Screen<'a>(PhantomData<&'a ()>);

impl Screen<'_> {
    pub fn config<'a>(&'a self) -> &'a mut Config {
        unsafe { &mut CONFIG }
    }

    #[cfg(feature = "o3ds")]
    pub fn screen() -> Self {
        Self(PhantomData)
    }

    #[named]
    #[cfg(not(feature = "o3ds"))]
    pub fn screen_port_sync(&self, is_top: bool, wait: bool) -> bool {
        unsafe {
            let res = svcWaitSynchronization(
                *SYN_HANDLES.screens_port_ready.get(&is_top_index(is_top)),
                if wait { THREAD_WAIT_NS.get() } else { 0 },
            );
            if res == 0 {
                return true;
            }
            if res != RES_TIMEOUT as s32 {
                ns_dbg_print!(failed, c_str!("Wait screens_port_ready"), res);
                if wait {
                    sleep_thread(THREAD_WAIT_NS);
                }
            }
            false
        }
    }

    #[named]
    #[cfg(not(feature = "o3ds"))]
    pub fn screens_ports_sync(&self) -> Option<bool> {
        unsafe {
            let mut out = mem::MaybeUninit::uninit();
            let res = svcWaitSynchronizationN(
                out.as_mut_ptr(),
                SYN_HANDLES.screens_port_ready.as_mut_ptr(),
                SCREEN_COUNT as s32,
                false,
                THREAD_WAIT_NS.get(),
            );
            if res != 0 {
                if res != RES_TIMEOUT as s32 {
                    ns_dbg_print!(failed, c_str!("Wait any screens_port_ready"), res);
                    sleep_thread(THREAD_WAIT_NS);
                    return None;
                }
                return None;
            }
            Some(out.assume_init() == RP_SCREEN_TOP as s32)
        }
    }
}

#[cfg(not(feature = "o3ds"))]
pub fn wait_for_vblank(is_top: bool) {
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

#[derive(ConstDefault, Clone, Copy)]
pub struct ScreenInfo {
    pub fill: u32,
    pub src_0: *mut u8,
    pub src_1: *mut u8,
    pub full_width: bool,
    pub pitch: u32,
    pub format: u32,
}

impl ScreenInfo {
    #[allow(unused)]
    fn src(&self) -> *mut u8 {
        if !self.src_1.is_null()
            && entries::thread_screen::top_screen_width()
                == entries::thread_screen::TopScreenWidth::Right
        {
            self.src_1
        } else {
            self.src_0
        }
    }
}

pub fn update_gpu_regs(is_top: bool) -> ScreenInfo {
    unsafe {
        let mut screen_info: ScreenInfo = const_default();
        if is_top {
            screen_info.format = ptr::read_volatile(GPU_FB_TOP_FMT as *const u32);
            screen_info.pitch = ptr::read_volatile(GPU_FB_TOP_STRIDE as *const u32);

            let fb = ptr::read_volatile(GPU_FB_TOP_SEL as *const u32);
            screen_info.src_0 = if fb & 1 == 0 {
                ptr::read_volatile(GPU_FB_TOP_LEFT_ADDR_1 as *const u32) as *mut u8
            } else {
                ptr::read_volatile(GPU_FB_TOP_LEFT_ADDR_2 as *const u32) as *mut u8
            };
            screen_info.src_1 = if fb & 1 == 0 {
                ptr::read_volatile(GPU_FB_TOP_RIGHT_ADDR_1 as *const u32) as *mut u8
            } else {
                ptr::read_volatile(GPU_FB_TOP_RIGHT_ADDR_2 as *const u32) as *mut u8
            };

            screen_info.full_width = (screen_info.format & (7 << 4)) == 0;
            screen_info.fill = ptr::read_volatile(LCD_TOP_FILLCOLOR as *const u32);
        } else {
            screen_info.format = ptr::read_volatile(GPU_FB_BOTTOM_FMT as *const u32);
            screen_info.pitch = ptr::read_volatile(GPU_FB_BOTTOM_STRIDE as *const u32);

            let fb = ptr::read_volatile(GPU_FB_BOTTOM_SEL as *const u32);
            screen_info.src_0 = if fb & 1 == 0 {
                ptr::read_volatile(GPU_FB_BOTTOM_ADDR_1 as *const u32) as *mut u8
            } else {
                ptr::read_volatile(GPU_FB_BOTTOM_ADDR_2 as *const u32) as *mut u8
            };
            screen_info.src_1 = ptr::null_mut();
            screen_info.fill = ptr::read_volatile(LCD_BOTTOM_FILLCOLOR as *const u32);
            screen_info.full_width = false;
        }
        screen_info.format &= 0xf;
        screen_info
    }
}

#[cfg(not(feature = "mem3"))]
type DmaHandles = RangedArray<Handle, WORK_COUNT>;

#[derive(ConstDefault)]
pub struct ScreenParams {
    #[cfg(not(feature = "mem3"))]
    pub dmas: [DmaHandles; EYE_COUNT],
    pub game_handle: Handle,
    pub game_pid: u32,
    pub game_fcram_base: u32,
    #[cfg(not(feature = "mem3"))]
    pub pid: u32,
    pub overlay: OverlayParams,
}

#[derive(ConstDefault)]
pub struct OverlayParams {
    pub game_pid: u32,
    pub game_handle: Handle,
}

pub static mut SCREEN_PARAMS: ScreenParams = const_default();

#[named]
pub fn screen_params_lock() -> Option<ScreenParamsLock> {
    wait_syn(
        cname!(),
        unsafe { SCREEN_HANDLES_LOCK },
        c_str!("SCREEN_HANDLES_LOCK"),
    )?;
    Some(ScreenParamsLock(()))
}

pub struct ScreenParamsLock(());

impl ScreenParamsLock {
    pub fn params(&self) -> &mut ScreenParams {
        unsafe { &mut SCREEN_PARAMS }
    }
}

impl Drop for ScreenParamsLock {
    #[named]
    fn drop(&mut self) {
        unsafe { release_mutex(cname!(), SCREEN_HANDLES_LOCK, c_str!("SCREEN_HANDLES_LOCK")) }
    }
}

pub fn try_capture_screen(is_top: bool, screen_info: &ScreenInfo) -> bool {
    #[cfg(not(feature = "mem3"))]
    if let Some(lock) = screen_params_lock() {
        #[cfg(not(feature = "o3ds"))]
        let w = unsafe { PARAMS.work_index };

        #[cfg(not(feature = "o3ds"))]
        let img = unsafe { img_info(is_top) } as u32;

        #[cfg(feature = "o3ds")]
        let img = unsafe { img_info() } as u32;

        capture_screen(
            lock.params(),
            is_top,
            screen_info,
            img,
            #[cfg(not(feature = "o3ds"))]
            w,
        )
    } else {
        false
    }

    #[cfg(feature = "mem3")]
    unsafe {
        IMG_INFO = (screen_info.src() as u32 | (1 << 31)) as *mut u8;
        thread_ready_release(is_top, screen_info.format, screen_info.pitch);
        true
    }
}

#[cfg(feature = "o3ds")]
pub unsafe fn img_info() -> *mut u8 {
    unsafe { IMG_INFO }
}

#[cfg(not(feature = "o3ds"))]
pub unsafe fn img_info_next(is_top: bool) {
    let iinfo = unsafe { IMG_INFOS.get_mut(&is_top_index(is_top)) };
    iinfo.index.next_wrapped();
}

#[cfg(not(feature = "o3ds"))]
pub unsafe fn img_info(is_top: bool) -> *mut u8 {
    let iinfo = unsafe { IMG_INFOS.get_mut(&is_top_index(is_top)) };
    *iinfo.bufs.get(&iinfo.index)
}

#[cfg(not(feature = "o3ds"))]
pub fn img_info_prev(is_top: bool) -> *const u8 {
    let iinfo = unsafe { IMG_INFOS.get_mut(&is_top_index(is_top)) };
    let mut index = iinfo.index;
    index.prev_wrapped();
    *iinfo.bufs.get(&index)
}

#[named]
#[cfg(not(feature = "mem3"))]
fn capture_screen(
    params: &mut ScreenParams,
    is_top: bool,
    screen_info: &ScreenInfo,
    dst: u32,
    #[cfg(not(feature = "o3ds"))] w: WorkIndex,
) -> bool {
    unsafe {
        #[cfg(feature = "o3ds")]
        let w = WorkIndex::init(0);

        let top_screen_width = entries::thread_screen::top_screen_width();
        let top_screen_right = top_screen_width == entries::thread_screen::TopScreenWidth::Right;
        let top_screen_both = top_screen_width == entries::thread_screen::TopScreenWidth::Both;
        let both_eyes = top_screen_both && !screen_info.full_width;

        let phys_0 = if top_screen_right && !screen_info.full_width {
            screen_info.src_1
        } else {
            screen_info.src_0
        } as u32;
        let phys_1 = if both_eyes {
            screen_info.src_1 as u32
        } else {
            0
        };

        let format = screen_info.format & 0xf;

        // Skip if handling of format unimplemented
        if format > 4 {
            ns_dbg_print!(failed, c_str!("format"), format as s32);
            sleep_thread(THREAD_WAIT_NS);
            return false;
        }

        let bpp: u32;
        let mut burst_size: u32 = 16;

        if format == 0 {
            bpp = 4;
            burst_size *= 4;
        } else if format == 1 {
            bpp = 3;
        } else {
            bpp = 2;
            burst_size *= 2;
        }

        let mut transfer_size = GSP_SCREEN_WIDTH * bpp;

        let mut pitch = screen_info.pitch;

        if !top_screen_both && screen_info.full_width {
            pitch *= 2;
        }

        let height = if is_top {
            if top_screen_both && screen_info.full_width {
                GSP_SCREEN_HEIGHT_TOP_2X
            } else {
                GSP_SCREEN_HEIGHT_TOP
            }
        } else {
            GSP_SCREEN_HEIGHT_BOTTOM
        };

        let buf_size = transfer_size * height;

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

        let mut do_dma = |phys, dst, eye: usize| {
            if phys == 0 {
                return 0;
            }

            let dma_conf = DmaConfig {
                channelId: -1,
                flags: (DMACFG_WAIT_AVAILABLE | DMACFG_DST_MEMORY_CONFIG | DMACFG_SRC_MEMORY_CONFIG)
                    as u8,
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

            {
                let dma = params.dmas.get_unchecked_mut(eye).get_mut(&w);
                if *dma != 0 {
                    let _ = svcCloseHandle(*dma);
                    *dma = 0;
                }
            }

            let (process, addr) = if is_in_vram(phys) {
                close_game_handle(params);
                (
                    entries::start_up::HOME_PROCESS_HANDLE,
                    0x1f000000 + (phys - 0x18000000),
                )
            } else if is_in_fcram(phys) {
                let process = get_game_handle(params);
                if process == 0 {
                    sleep_thread(THREAD_WAIT_NS);
                    return 0;
                }
                (process, SCREEN_PARAMS.game_fcram_base + (phys - 0x20000000))
            } else {
                sleep_thread(THREAD_WAIT_NS);
                return 0;
            };

            let dma = {
                let mut dma = mem::MaybeUninit::uninit();
                let res = svcStartInterProcessDma(
                    dma.as_mut_ptr(),
                    CUR_PROCESS_HANDLE,
                    dst,
                    process,
                    addr,
                    buf_size,
                    &dma_conf,
                );
                if res != 0 {
                    close_game_handle(params);
                    ns_dbg_print!(failed, c_str!("dma"), res);
                    sleep_thread(THREAD_WAIT_NS);
                    return 0;
                }

                #[cfg(not(feature = "o3ds"))]
                send_overlay_stats(&mut params.overlay);

                let dma = dma.assume_init();
                *params.dmas.get_unchecked_mut(eye).get_mut(&w) = dma;
                dma
            };

            dma
        };

        let dma = [
            do_dma(phys_0, dst, EYE_LEFT),
            do_dma(phys_1, dst + buf_size, EYE_RIGHT),
        ];
        if dma[0] == 0 {
            return false;
        }
        #[cfg(feature = "o3ds")]
        {
            let _ = dma;
        }

        thread_ready_release(
            is_top,
            screen_info.format,
            #[cfg(not(feature = "o3ds"))]
            w,
            #[cfg(not(feature = "o3ds"))]
            dma,
            #[cfg(not(feature = "o3ds"))]
            both_eyes,
        );

        true
    }
}

fn close_game_handle(params: &mut ScreenParams) {
    if params.game_handle != 0 {
        unsafe {
            let _ = svcCloseHandle(params.game_handle);
        }
        params.game_handle = 0;
        params.game_fcram_base = 0;
        params.game_pid = 0;

        #[cfg(not(feature = "o3ds"))]
        set_no_skip_frames();
    }
    close_overlay_handle(&mut params.overlay);
}

fn close_overlay_handle(params: &mut OverlayParams) {
    if params.game_handle != 0 {
        unsafe {
            let _ = svcCloseHandle(params.game_handle);
        }
        params.game_handle = 0;
    }
    params.game_pid = 0;
}

#[cfg(not(feature = "mem3"))]
fn get_game_handle(params: &mut ScreenParams) -> Handle {
    let game_pid = RP_CONFIG.game_pid().load(Ordering::Acquire);
    if game_pid != params.game_pid {
        close_game_handle(params);
        params.game_pid = game_pid;
    }

    let mut process = mem::MaybeUninit::uninit();

    if params.game_handle == 0 {
        if game_pid != 0 {
            let res = unsafe { svcOpenProcess(process.as_mut_ptr(), game_pid) };
            if res == 0 {
                params.game_handle = unsafe { process.assume_init() };
            }
        }
        if params.game_handle == 0 {
            let mut process_count = mem::MaybeUninit::uninit();
            let mut pids = [const { mem::MaybeUninit::uninit() }; LOCAL_PID_BUF_COUNT as usize];
            let res = unsafe {
                svcGetProcessList(
                    process_count.as_mut_ptr(),
                    pids.as_mut_ptr() as *mut u32_,
                    LOCAL_PID_BUF_COUNT as s32,
                )
            };
            if res == 0 {
                for i in 0..unsafe { process_count.assume_init() } {
                    let pid = unsafe { pids.get_unchecked(i as usize).assume_init() };
                    if pid < 0x28 {
                        continue;
                    }

                    let res = unsafe { svcOpenProcess(process.as_mut_ptr(), pid) };
                    if res == 0 {
                        let process = unsafe { process.assume_init() };
                        let mut tid = mem::MaybeUninit::<[u32_; 2]>::uninit();
                        let res =
                            unsafe { getProcessTIDByHandle(process, tid.as_mut_ptr() as *mut _) }
                                as s32;
                        if res == 0 {
                            if unsafe { tid.assume_init().get_unchecked(1) & 0xffff == 0 } {
                                if params.pid == pid {
                                    sleep_thread(THREAD_WAIT_NS);
                                    params.pid = 0;
                                } else {
                                    params.game_handle = process;
                                    params.pid = pid;
                                    break;
                                }
                            }
                        }
                        let _ = unsafe { svcCloseHandle(process) };
                    }
                }
            }
        }
        if params.game_handle == 0 {
            return 0;
        }
    }
    if params.game_fcram_base == 0 {
        if unsafe { svcFlushProcessDataCache(params.game_handle, 0x14000000, 0x1000) } == 0 {
            params.game_fcram_base = 0x14000000;
        } else if unsafe { svcFlushProcessDataCache(params.game_handle, 0x30000000, 0x1000) } == 0 {
            params.game_fcram_base = 0x30000000;
        } else {
            close_game_handle(params);
            return 0;
        }
    }

    params.game_handle
}

#[cfg(not(feature = "o3ds"))]
fn send_overlay_stats(params: &mut OverlayParams) {
    if unsafe { (*config_consts::NTR_CONFIG).ex.plg.overlayStats == 0 } {
        close_overlay_handle(params);
        return;
    }

    let game_pid = port_game_pid();

    let game_pid = if game_pid == 0 {
        RP_CONFIG.game_pid().load(Ordering::Acquire)
    } else if game_pid == unsafe { (*config_consts::NTR_CONFIG).HomeMenuPid } {
        0
    } else {
        game_pid
    };
    if params.game_pid != game_pid {
        close_overlay_handle(params);

        if game_pid > 0 {
            let res = unsafe { svcOpenProcess(&mut params.game_handle, game_pid) };
            if res >= 0 {
                params.game_pid = game_pid;
            }
        }
    }

    let process = if params.game_pid == 0 {
        unsafe { entries::start_up::HOME_PROCESS_HANDLE }
    } else {
        params.game_handle
    };

    let addr = config_consts::OV_STATS as *mut _;
    let len = unsafe { mem::size_of_val(&*config_consts::OV_STATS) } as u32;

    unsafe {
        let _ = copyRemoteMemory(process, addr, CUR_PROCESS_HANDLE, addr, len);
    }
}

#[cfg(not(feature = "mem3"))]
fn is_in_vram(phys: u32) -> bool {
    if phys >= 0x18000000 {
        if phys < 0x18000000 + 0x00600000 {
            return true;
        }
    }
    false
}

#[cfg(not(feature = "mem3"))]
fn is_in_fcram(phys: u32) -> bool {
    if phys >= 0x20000000 {
        if phys < 0x20000000 + 0x10000000 {
            return true;
        }
    }
    false
}
