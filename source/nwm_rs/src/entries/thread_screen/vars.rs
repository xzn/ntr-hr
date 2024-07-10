use super::*;

static mut priority_is_top: bool = false;
static mut priority_factor: u32_ = 0;
static mut priority_factor_scaled: u32_ = 0;
static mut screens_synced: RangedArray<bool, WORK_COUNT> = const_default();
static mut frame_counts: RangedArray<u32_, SCREEN_COUNT> = const_default();
static mut frame_queues: RangedArray<u32_, SCREEN_COUNT> = const_default();
static mut screen_work_index: WorkIndex = WorkIndex::init();
static mut screen_thread_id: ThreadId = ThreadId::init();
static mut skip_frames: RangedArray<bool, WORK_COUNT> = const_default();
static mut no_skip_frames: RangedArray<bool, SCREEN_COUNT> = const_default();
static mut last_frame_timings: RangedArray<u32_, SCREEN_COUNT> = const_default();
static mut port_game_pid: AtomicU32 = const_default();

type DmaHandles = RangedArray<Handle, WORK_COUNT>;

#[derive(ConstDefault)]
pub struct CapInfo {
    pub src: *mut u8_,
    pub pitch: u32_,
    pub format: u32_,
}

#[derive(ConstDefault)]
pub struct CapParams {
    pub dmas: DmaHandles,
    pub game: Handle,
    pub game_pid: u32_,
    pub game_fcram_base: u32_,
}

pub static mut cap_params: CapParams = const_default();

pub const IMG_WORK_COUNT: u32_ = 2;
pub type ImgWorkIndex = Ranged<IMG_WORK_COUNT>;
pub type ImgBufs = RangedArray<*mut u8_, IMG_WORK_COUNT>;

#[derive(ConstDefault)]
pub struct ImgInfo {
    pub bufs: ImgBufs,
    pub index: ImgWorkIndex,
}

pub type ImgInfos = RangedArray<ImgInfo, SCREEN_COUNT>;
pub static mut img_infos: ImgInfos = const_default();

pub unsafe fn reset_no_skip_frame(is_top: bool) -> bool {
    let b = *no_skip_frames.get_b(is_top);
    *no_skip_frames.get_b_mut(is_top) = false;
    b
}

pub unsafe fn set_no_skip_frame(is_top: bool) {
    *no_skip_frames.get_b_mut(is_top) = true;
}

pub unsafe fn get_last_frame_timing(is_top: bool) -> u32_ {
    *last_frame_timings.get_b(is_top)
}

pub unsafe fn set_last_frame_timing(is_top: bool, timing: u32_) {
    *last_frame_timings.get_b_mut(is_top) = timing;
}

pub unsafe fn set_port_game_pid(v: u32_) {
    port_game_pid.store(v, Ordering::Relaxed);
}

pub unsafe fn get_port_game_pid() -> u32_ {
    *port_game_pid.as_ptr()
}

const fn log_scale(v: u8) -> c_double {
    v as c_double
}

macro_rules! LOG {
    ($v:expr) => {
        FIX(log_scale($v))
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

const log_scaled_tab_nested: [[[u32_; 8]; 8]; 4] =
    [LOG64!(0), LOG64!(64), LOG64!(128), LOG64!(192)];

pub unsafe fn reset_thread_vars(mode: u32_) {
    let log_scaled_tab: &[u32_; 256] = mem::transmute(&log_scaled_tab_nested);

    let is_top = (mode & 0xff00) > 0;
    let factor = mode & 0xff;
    priority_is_top = is_top;
    priority_factor = factor;
    priority_factor_scaled = *log_scaled_tab.get_unchecked(factor as usize);
    crate::entries::work_thread::no_skip_next_frames();

    for i in ScreenIndex::all() {
        *frame_counts.get_mut(&i) = 1;
        *frame_queues.get_mut(&i) = priority_factor_scaled;

        *skip_frames.get_mut(&i) = false;
    }
    screen_work_index = WorkIndex::init();
    screen_thread_id = ThreadId::init();

    for i in WorkIndex::all() {
        *screens_synced.get_mut(&i) = false;
    }
}

pub unsafe fn init_img_info<const T: usize>(
    i: &ScreenIndex,
    j: &ImgWorkIndex,
    m: &mut MemRegion8<T>,
) {
    *img_infos.get_mut(&j).bufs.get_mut(&i) = m.to_ptr();
}

#[allow(dead_code)]
pub unsafe fn get_img_info(is_top: bool, j: &ImgWorkIndex) -> &mut *mut u8 {
    img_infos.get_b_mut(is_top).bufs.get_mut(&j)
}

pub struct ScreenThreadVars(());

impl ScreenThreadVars {
    pub fn priority_is_top(&self) -> bool {
        unsafe { priority_is_top }
    }

    pub fn priority_factor(&self) -> u32_ {
        unsafe { priority_factor }
    }

    pub fn priority_factor_scaled(&self) -> u32_ {
        unsafe { priority_factor_scaled }
    }

    pub fn frame_count(&self, is_top: bool) -> &'static mut u32_ {
        unsafe { frame_counts.get_b_mut(is_top) }
    }

    pub fn frame_queue(&self, is_top: bool) -> &'static mut u32_ {
        unsafe { frame_queues.get_b_mut(is_top) }
    }

    pub fn port_game_pid(&self) -> u32_ {
        unsafe { port_game_pid.load(Ordering::Relaxed) }
    }

    pub fn img_dst(&self, is_top: bool) -> u32_ {
        unsafe {
            let iinfo = img_infos.get_b_mut(is_top);
            *iinfo.bufs.get(&ptr::read_volatile(&iinfo.index)) as u32_
        }
    }

    pub fn screen_work_index(&self) -> WorkIndex {
        unsafe { ptr::read_volatile(&screen_work_index) }
    }

    #[named]
    pub fn port_screen_sync(&self, is_top: bool, wait: bool) -> bool {
        unsafe {
            let res = svcWaitSynchronization(
                *(*syn_handles).port_screen_ready.get_b(is_top),
                if wait { THREAD_WAIT_NS } else { 0 },
            );
            if res == 0 {
                return true;
            }
            if res != RES_TIMEOUT as s32 {
                nsDbgPrint!(waitForSyncFailed, c_str!("port_screen_ready"), res);
                if wait {
                    svcSleepThread(THREAD_WAIT_NS);
                }
            }
            false
        }
    }

    #[named]
    pub fn port_screens_sync(&self) -> Option<bool> {
        unsafe {
            let mut out = mem::MaybeUninit::uninit();
            let res = svcWaitSynchronizationN(
                out.as_mut_ptr(),
                (*syn_handles).port_screen_ready.as_mut_ptr(),
                SCREEN_COUNT as s32,
                false,
                THREAD_WAIT_NS,
            );
            if res != 0 {
                if res != RES_TIMEOUT as s32 {
                    nsDbgPrint!(waitForSyncFailed, c_str!("port_screen_ready"), res);
                    svcSleepThread(THREAD_WAIT_NS);
                    return None;
                }
                return None;
            }
            Some(out.assume_init() > 0)
        }
    }

    #[named]
    pub fn release(self, is_top: bool, format: u32_, work_index: WorkIndex) {
        unsafe {
            ptr::write_volatile(
                screen_encode_vars.get_mut(&work_index),
                ScreenEncodeVars::init(is_top, format, work_index),
            );

            let mut count = mem::MaybeUninit::<s32>::uninit();
            if ptr::read_volatile(skip_frames.get(&ptr::read_volatile(&screen_work_index))) {
                let res = svcReleaseSemaphore(
                    count.as_mut_ptr(),
                    (*syn_handles)
                        .threads
                        .get(&ptr::read_volatile(&screen_thread_id))
                        .work_ready,
                    1,
                );
                if res != 0 {
                    nsDbgPrint!(
                        releaseSemaphoreFailed,
                        c_str!("work_ready"),
                        work_index.get(),
                        res
                    );
                }
            } else {
                for j in ThreadId::up_to(&crate::entries::work_thread::get_core_count_in_use()) {
                    let res = svcReleaseSemaphore(
                        count.as_mut_ptr(),
                        (*syn_handles).threads.get(&j).work_ready,
                        1,
                    );
                    if res != 0 {
                        nsDbgPrint!(
                            releaseSemaphoreFailed,
                            c_str!("work_ready"),
                            work_index.get(),
                            res
                        );
                    }
                }
            }
        }
    }
}

#[derive(Copy, Clone, ConstDefault)]
pub struct ScreenEncodeVars {
    is_top: bool,
    format: u32_,
    dma: Handle,
}

#[derive(Copy, Clone, ConstDefault)]
pub struct ScreenWorkVars {
    is_top: bool,
    work_index: WorkIndex,
}

impl ScreenEncodeVars {
    pub fn init(is_top: bool, format: u32_, work_index: WorkIndex) -> Self {
        unsafe {
            let dma = *cap_params.dmas.get(&work_index);
            ScreenEncodeVars {
                is_top,
                format,
                dma,
            }
        }
    }
}

impl ScreenWorkVars {
    pub fn init(work_index: WorkIndex) -> Self {
        unsafe {
            Self {
                is_top: ptr::read_volatile(&screen_encode_vars.get(&work_index).is_top),
                work_index,
            }
        }
    }

    pub fn is_top(&self) -> bool {
        self.is_top
    }

    pub fn read_is_top(&mut self) -> bool {
        unsafe {
            self.is_top = ptr::read_volatile(&screen_encode_vars.get(&self.work_index).is_top);
            self.is_top
        }
    }

    pub fn format(&self) -> u32_ {
        unsafe { ptr::read_volatile(&screen_encode_vars.get(&self.work_index).format) }
    }

    pub fn work_index(&self) -> WorkIndex {
        self.work_index
    }

    pub fn dma(&self) -> Handle {
        unsafe { ptr::read_volatile(&screen_encode_vars.get(&self.work_index).dma) }
    }

    pub fn img_src(&self) -> *const u8_ {
        ScreenThreadVars(()).img_dst(self.is_top()) as *const u8_
    }

    pub fn img_src_prev(&self) -> *const u8_ {
        unsafe {
            let iinfo = img_infos.get_b_mut(self.is_top());
            let mut index = ptr::read_volatile(&iinfo.index);
            index.prev_wrapped();
            *iinfo.bufs.get(&index)
        }
    }

    pub unsafe fn img_index_next(&self) {
        let iinfo = img_infos.get_b_mut(self.is_top());
        iinfo.index.next_wrapped();
    }

    pub unsafe fn set_skip_frame(&self, skip_frame: bool) {
        ptr::write_volatile(skip_frames.get_mut(&self.work_index), skip_frame);
    }

    pub unsafe fn clear_screen_synced(&self) {
        ptr::write_volatile(screens_synced.get_mut(&self.work_index), false);
    }

    pub unsafe fn set_screen_thread_id(&self, t: &ThreadId) {
        ptr::write_volatile(&mut screen_thread_id, *t);
    }

    pub unsafe fn set_screen_work_index(&self, w: &WorkIndex) {
        ptr::write_volatile(&mut screen_work_index, *w);
    }

    #[named]
    pub unsafe fn release_screen_ready(&self) {
        let mut count = mem::MaybeUninit::uninit();
        let res = svcReleaseSemaphore(count.as_mut_ptr(), (*syn_handles).screen_ready, 1);
        if res != 0 {
            nsDbgPrint!(
                releaseSemaphoreFailed,
                c_str!("screen_ready"),
                self.work_index.get(),
                res
            );
        }
    }

    #[named]
    pub unsafe fn release_work_done(&self) {
        let mut count = mem::MaybeUninit::uninit();
        let res = svcReleaseSemaphore(
            count.as_mut_ptr(),
            (*syn_handles).works.get(&self.work_index).work_done,
            1,
        );
        if res != 0 {
            nsDbgPrint!(
                releaseSemaphoreFailed,
                c_str!("work_done"),
                self.work_index.get(),
                res
            );
        }
    }
}

static mut screen_encode_vars: RangedArray<ScreenEncodeVars, WORK_COUNT> = const_default();

#[named]
pub unsafe fn screen_encode_acquire(t: &ThreadId) -> Option<()> {
    loop {
        if crate::entries::work_thread::reset_threads() {
            return None;
        }

        let res = svcWaitSynchronization((*syn_handles).threads.get(&t).work_ready, THREAD_WAIT_NS);

        if res != 0 {
            if res != RES_TIMEOUT as s32 {
                nsDbgPrint!(waitForSyncFailed, c_str!("work_ready"), res);
                svcSleepThread(THREAD_WAIT_NS);
            }
            continue;
        }

        return Some(());
    }
}

pub struct ScreenEncodeSync(());

impl ScreenEncodeSync {
    #[named]
    pub fn acquire(&self) -> Option<ScreenThreadVarsSync> {
        unsafe {
            loop {
                if crate::entries::work_thread::reset_threads() {
                    return None;
                }
                let res = svcWaitSynchronization((*syn_handles).screen_ready, THREAD_WAIT_NS);
                if res != 0 {
                    if res != RES_TIMEOUT as s32 {
                        nsDbgPrint!(waitForSyncFailed, c_str!("screen_ready"), res);
                        svcSleepThread(THREAD_WAIT_NS);
                    }
                    continue;
                }
                return Some(ScreenThreadVarsSync(()));
            }
        }
    }
}

pub struct ScreenThreadVarsSync(());

impl ScreenThreadVarsSync {
    pub fn sync(self, wait_sync: bool) -> Option<ScreenThreadVars> {
        unsafe {
            loop {
                if crate::entries::work_thread::reset_threads() {
                    return None;
                }
                let w = ptr::read_volatile(&screen_work_index);
                let synced = screens_synced.get_mut(&w);

                if !ptr::read_volatile(synced) {
                    let res = svcWaitSynchronization(
                        (*syn_handles).works.get_mut(&w).work_done,
                        if wait_sync { THREAD_WAIT_NS } else { 0 },
                    );
                    if res != 0 {
                        if wait_sync && res != RES_TIMEOUT as s32 {
                            svcSleepThread(THREAD_WAIT_NS)
                        }
                        continue;
                    }
                    *synced = true;
                }
                return Some(ScreenThreadVars(()));
            }
        }
    }
}

pub extern "C" fn thread_screen(_: *mut c_void) {
    unsafe {
        safe_impl::thread_screen_loop(ScreenEncodeSync(()));
        svcExitThread()
    }
}
