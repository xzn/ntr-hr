use super::*;

#[derive(ConstDefault)]
pub struct BlitCtx {
    pub width: u32_,
    pub height: u32_,
    pub format: u32_,
    pub src: *mut u8_,
    pub src_pitch: u32_,
    pub bpp: u32_,

    pub frame_id: u8_,
    pub is_top: bool,

    pub cinfo: *mut CInfosThreads,

    pub i_start: RowIndexes,
    pub i_count: RowIndexes,

    pub should_capture: AtomicBool,
}

pub type BlitCtxes = RangedArray<BlitCtx, WORK_COUNT>;
pub static mut blit_ctxes: BlitCtxes = const_default();

#[derive(ConstDefault)]
pub struct AllocStats {
    pub qual: rp_alloc_stats,
    pub comp: rp_alloc_stats,
}

#[derive(ConstDefault)]
pub struct CInfo {
    pub info: jpeg_compress_struct,
    pub alloc_stats: AllocStats,
}

pub type CInfosThreads = RangedArray<CInfo, RP_CORE_COUNT_MAX>;

pub type CInfos = RangedArray<CInfosThreads, WORK_COUNT>;

pub type CInfosAll = RangedArray<CInfo, CINFOS_COUNT>;

static mut cinfos: CInfos = const_default();
static mut cinfos_all: *mut CInfosAll = ptr::null_mut();

pub unsafe fn get_jerr() -> &'static mut jpeg_error_mgr {
    unsafe { &mut jerr }
}

pub unsafe fn get_cinfos() -> &'static mut CInfos {
    unsafe { &mut cinfos }
}

pub unsafe fn get_cinfos_all() -> &'static mut *mut CInfosAll {
    unsafe { &mut cinfos_all }
}

static mut jerr: jpeg_error_mgr = const_default();

pub type RowIndexes = RangedArray<u32_, RP_CORE_COUNT_MAX>;
pub type RowProgresses = RangedArray<AtomicU32, RP_CORE_COUNT_MAX>;

#[derive(ConstDefault)]
pub struct LoadAndProgress {
    pub n: Fix32,
    pub n_last: Fix32,
    pub n_adjusted: Fix32,
    pub n_last_adjusted: Fix32,
    pub v_adjusted: u32_,
    pub v_last_adjusted: u32_,
    pub p: RowProgresses,
    pub p_snapshot: RowIndexes,
}

pub type LoadAndProgresses = RangedArray<LoadAndProgress, WORK_COUNT>;
static mut load_and_progresses: LoadAndProgresses = const_default();

static mut reset_threads_flag: AtomicBool = const_default();
static mut core_count_in_use: CoreCount = CoreCount::init();

pub static mut current_frame_ids: RangedArray<u8_, SCREEN_COUNT> = const_default();

#[derive(ConstDefault)]
pub struct Buffers {
    pub prep: [JSAMPARRAY; MAX_COMPONENTS as usize],
    pub color: [JSAMPARRAY; MAX_COMPONENTS as usize],
    pub mcu: [JBLOCKROW; C_MAX_BLOCKS_IN_MCU as usize],
}

pub type WorkBuffers = RangedArray<RangedArray<Buffers, RP_CORE_COUNT_MAX>, WORK_COUNT>;
pub static mut work_buffers: WorkBuffers = const_default();

pub unsafe fn get_work_buffers() -> &'static mut WorkBuffers {
    &mut work_buffers
}

pub fn reset_threads() -> bool {
    unsafe { reset_threads_flag.load(Ordering::Relaxed) }
}

pub fn set_reset_threads_ar() {
    unsafe { reset_threads_flag.store(true, Ordering::Relaxed) }
}

pub fn clear_reset_threads_ar() {
    unsafe { reset_threads_flag.store(false, Ordering::Relaxed) }
}

pub fn get_core_count_in_use() -> CoreCount {
    unsafe { ptr::read_volatile(&core_count_in_use) }
}

pub unsafe fn set_core_count_in_use(v: u32_) {
    core_count_in_use.set(v)
}

pub unsafe fn reset_vars() {
    for i in WorkIndex::all() {
        let load = load_and_progresses.get_mut(&i);

        for j in ThreadId::up_to(&get_core_count_in_use()) {
            let info = crate::entries::thread_nwm::get_nwm_infos()
                .get_mut(&i)
                .get_mut(&j);
            let buf = info.buf;
            let info = &mut info.info;
            info.send_pos = buf;
            *info.pos.as_ptr() = buf;
            *info.flag.as_ptr() = 0;

            *(*load.p.get_mut(&j)).as_ptr() = 0;
            *load.p_snapshot.get_mut(&j) = 0;
        }

        load.n.0 = 0;
        load.n_last.0 = 0;
        load.n_adjusted.0 = 0;
        load.n_last_adjusted.0 = 0;
        load.v_adjusted = 0;
        load.v_last_adjusted = 0;
    }
}

pub struct ThreadDoVars(crate::entries::thread_screen::ScreenWorkVars);

impl ThreadDoVars {
    pub fn v(&self) -> &crate::entries::thread_screen::ScreenWorkVars {
        &self.0
    }

    pub fn nwm_infos(&self) -> &mut crate::entries::thread_nwm::NwmThreadInfos {
        unsafe { crate::entries::thread_nwm::get_nwm_infos().get_mut(&self.v().work_index()) }
    }

    pub fn blit_ctx(&self) -> &mut BlitCtx {
        unsafe { blit_ctxes.get_mut(&self.0.work_index()) }
    }

    pub fn load_and_progresses(&self) -> &mut LoadAndProgresses {
        unsafe { &mut load_and_progresses }
    }

    pub fn capture_screen(&self) {
        unsafe {
            let mut w = self.v().work_index();
            w.next_wrapped();
            self.v().set_screen_work_index(&w);

            self.v().release_screen_ready();
        }
    }

    pub fn release(self) {
        unsafe {
            let w = self.v().work_index();
            let syn = (*syn_handles).works.get(&w);

            let f = syn.work_done_count.fetch_add(1, Ordering::Relaxed);
            let core_count = get_core_count_in_use();
            if f == 0 {
                let p = load_and_progresses.get_mut(&w);
                for j in ThreadId::up_to(&core_count) {
                    *p.p_snapshot.get_mut(&j) = p.p.get_mut(&j).load(Ordering::Relaxed);
                }
            }
            if f == core_count.get() - 1 {
                ptr::write_volatile(syn.work_done_count.as_ptr(), 0);
                ptr::write_volatile(syn.work_begin_flag.as_ptr(), false);

                self.v().release_work_done();
            }
        }
    }
}

pub struct ThreadVars(crate::entries::thread_screen::ScreenWorkVars);

impl ThreadVars {
    pub fn work_begin_acquire(self) -> core::result::Result<ThreadBeginVars, ThreadBeginRestVars> {
        unsafe {
            if (*syn_handles)
                .works
                .get_mut(&self.0.work_index())
                .work_begin_flag
                .swap(true, Ordering::Relaxed)
                == false
            {
                Ok(ThreadBeginVars(self))
            } else {
                Err(ThreadBeginRestVars(self))
            }
        }
    }

    pub fn blit_ctx(&self) -> &mut BlitCtx {
        unsafe { blit_ctxes.get_mut(&self.0.work_index()) }
    }

    pub fn v(&self) -> &crate::entries::thread_screen::ScreenWorkVars {
        &self.0
    }

    pub fn v_mut(&mut self) -> &mut crate::entries::thread_screen::ScreenWorkVars {
        &mut self.0
    }
}

pub struct ThreadBeginRestVars(ThreadVars);

impl ThreadBeginRestVars {
    #[named]
    pub fn acquire(self, t: &ThreadId) -> Option<ThreadDoVars> {
        unsafe {
            loop {
                if reset_threads() {
                    return None;
                }
                let res = svcWaitSynchronization(
                    (*syn_handles).threads.get(&t).work_begin_ready,
                    THREAD_WAIT_NS,
                );
                if res != 0 {
                    if res != RES_TIMEOUT as s32 {
                        nsDbgPrint!(waitForSyncFailed, c_str!("work_begin_ready"), res);
                        svcSleepThread(THREAD_WAIT_NS);
                    }
                    continue;
                }
                return Some(ThreadDoVars(self.0 .0));
            }
        }
    }
}

pub struct ThreadBeginVars(ThreadVars);

impl ThreadBeginVars {
    pub fn ctx(&self) -> &mut BlitCtx {
        self.0.blit_ctx()
    }

    pub fn cinfos(&self) -> &mut CInfosThreads {
        unsafe { cinfos.get_mut(&self.v().work_index()) }
    }

    pub fn nwm_infos(&self) -> &mut crate::entries::thread_nwm::NwmThreadInfos {
        unsafe { crate::entries::thread_nwm::get_nwm_infos().get_mut(&self.v().work_index()) }
    }

    pub fn data_buf_hdr(&self) -> &mut crate::entries::thread_nwm::DataHdr {
        unsafe { crate::entries::thread_nwm::get_data_buf_hdrs().get_mut(&self.v().work_index()) }
    }

    pub fn v(&self) -> &crate::entries::thread_screen::ScreenWorkVars {
        &self.0.v()
    }

    pub fn v_mut(&mut self) -> &mut crate::entries::thread_screen::ScreenWorkVars {
        self.0.v_mut()
    }

    pub fn frame_id(&self) -> u8_ {
        unsafe { *current_frame_ids.get_b_mut(self.v().is_top()) }
    }

    #[named]
    pub fn dma_sync(&self) {
        unsafe {
            let res = svcWaitSynchronization(self.v().dma(), THREAD_WAIT_NS);
            if res != 0 {
                if res != RES_TIMEOUT as s32 {
                    nsDbgPrint!(waitForSyncFailed, c_str!("dmas"), res);
                    svcSleepThread(THREAD_WAIT_NS);
                }
            }
        }
    }

    pub fn frame_changed(&self) -> bool {
        unsafe {
            let ctx = self.ctx();
            let src_len = ctx.width * ctx.src_pitch;
            *slice::from_raw_parts(ctx.src, src_len as usize)
                != *slice::from_raw_parts(self.v().img_src_prev(), src_len as usize)
        }
    }

    pub fn ready_next(&self) {
        unsafe {
            self.v().img_index_next();
            *current_frame_ids.get_b_mut(self.v().is_top()) += 1;
        }
    }

    #[named]
    pub fn release_and_capture_screen(self, t: &ThreadId) -> ThreadDoVars {
        unsafe {
            let mut count = mem::MaybeUninit::uninit();
            for j in ThreadId::up_to(&get_core_count_in_use()) {
                if j != *t {
                    let res = svcReleaseSemaphore(
                        count.as_mut_ptr(),
                        (*syn_handles).threads.get(&j).work_begin_ready,
                        1,
                    );
                    if res != 0 {
                        nsDbgPrint!(
                            releaseSemaphoreFailed,
                            c_str!("work_begin_ready"),
                            self.v().work_index().get(),
                            res
                        );
                    }
                }
            }

            self.v().set_skip_frame(false);
            self.v().clear_screen_synced();

            ThreadDoVars(self.0 .0)
        }
    }

    pub fn release_skip_frame(&self, t: &ThreadId) -> Option<()> {
        unsafe {
            self.v().set_skip_frame(true);
            self.v().set_screen_thread_id(&t);
            self.v().release_screen_ready();

            crate::entries::thread_screen::screen_encode_acquire(&t)?;
            Some(())
        }
    }

    pub fn load_and_progresses(&self) -> &mut LoadAndProgresses {
        unsafe { &mut load_and_progresses }
    }
}

pub unsafe fn work_thread_loop(t: ThreadId) -> Option<()> {
    let mut work_index = WorkIndex::init();
    loop {
        crate::entries::thread_screen::screen_encode_acquire(&t)?;
        safe_impl::send_frame(
            &t,
            ThreadVars(crate::entries::thread_screen::ScreenWorkVars::init(
                work_index,
            )),
        )?;
        work_index.next_wrapped();
    }
}

pub unsafe fn no_skip_next_frames() {
    let _ = svcSignalEvent(*(*syn_handles).port_screen_ready.get_b(false));
    let _ = svcSignalEvent(*(*syn_handles).port_screen_ready.get_b(true));

    for i in crate::entries::thread_screen::ImgWorkIndex::all() {
        **crate::entries::thread_screen::get_img_info(false, &i) += 1;
        **crate::entries::thread_screen::get_img_info(false, &i) += 1;
    }
}

#[named]
#[no_mangle]
#[allow(unreachable_code)]
extern "C" fn rpMalloc(cinfo: j_common_ptr, size: u32_) -> *mut c_void {
    unsafe {
        let info = &mut *cinfo;
        let ret = info.alloc.buf.add(info.alloc.stats.offset as usize);
        let mut total_size = size;

        if total_size % 32 != 0 {
            total_size += 32 - (total_size % 32);
        }

        if info.alloc.stats.remaining < total_size {
            let alloc_size = info.alloc.stats.offset + info.alloc.stats.remaining;
            nsDbgPrint!(allocFailed, total_size, alloc_size);

            (*info.err).msg_code = JERR_OUT_OF_MEMORY as s32;
            (*info.err).msg_parm.i[0] = 0;
            (*info.err).error_exit.unwrap_unchecked()(cinfo);
            return ptr::null_mut();
        }

        info.alloc.stats.offset += total_size;
        info.alloc.stats.remaining -= total_size;

        return ret as *mut _;
    }
}

#[no_mangle]
extern "C" fn rpFree(_: j_common_ptr, _: *mut c_void) {}
