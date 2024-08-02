use super::*;

#[derive(ConstDefault)]
pub struct BlitCtx {
    pub format: u32_,
    pub src: *const u8_,

    pub frame_id: u8_,
    pub is_top: bool,

    pub i_start: RowIndexes,
    pub i_count: RowIndexes,

    pub should_capture: AtomicBool,
}

impl BlitCtx {
    pub fn screen(&self) -> ScreenIndex {
        unsafe {
            if self.is_top {
                ScreenIndex::init_unchecked(0)
            } else {
                ScreenIndex::init_unchecked(1)
            }
        }
    }

    pub fn pitch(&self) -> u32_ {
        self.bpp() * self.width()
    }

    pub fn src_len(&self) -> u32_ {
        self.height() * self.pitch()
    }

    pub fn width(&self) -> u32_ {
        GSP_SCREEN_WIDTH
    }

    pub fn height(&self) -> u32_ {
        if self.is_top {
            GSP_SCREEN_HEIGHT_TOP
        } else {
            GSP_SCREEN_HEIGHT_BOTTOM
        }
    }

    pub fn bpp(&self) -> u32_ {
        let format = self.format & 0xf;
        if format == 0 {
            4
        } else if format == 1 {
            3
        } else {
            2
        }
    }
}

pub type BlitCtxes = RangedArray<BlitCtx, WORK_COUNT>;
static mut blit_ctxes: BlitCtxes = const_default();

#[derive(ConstDefault)]
pub struct JpegGlobal {
    jpeg: *mut crate::jpeg::Jpeg,
}

unsafe impl core::marker::Sync for JpegGlobal {}
unsafe impl core::marker::Send for JpegGlobal {}

static mut jpeg_mem: JpegGlobal = const_default();

pub unsafe fn set_jpeg(buf: &'static mut [u8; mem::size_of::<crate::jpeg::Jpeg>()]) {
    jpeg_mem.jpeg = buf.as_mut_ptr() as *mut crate::jpeg::Jpeg;
}
pub unsafe fn get_jpeg() -> &'static mut crate::jpeg::Jpeg {
    &mut *jpeg_mem.jpeg
}

pub type RowIndexes = RangedArray<u32_, RP_CORE_COUNT_MAX>;
static mut last_row_last_n: u32_ = const_default();

static mut reset_threads_flag: AtomicBool = const_default();
static mut core_count_in_use: CoreCount = CoreCount::init();

static mut current_frame_ids: RangedArray<u8_, SCREEN_COUNT> = const_default();
static mut last_frame_timings: RangedArray<u32_, SCREEN_COUNT> = const_default();

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
    unsafe { core_count_in_use }
}

pub unsafe fn set_core_count_in_use(v: u32_) {
    core_count_in_use.set(v)
}

pub unsafe fn reset_vars() {
    last_row_last_n = 0;

    for i in WorkIndex::all() {
        for j in ThreadId::up_to(&get_core_count_in_use()) {
            let info = crate::entries::thread_nwm::get_nwm_infos()
                .get_mut(&i)
                .get_mut(&j);
            let buf = info.buf;
            let info = &mut info.info;
            info.send_pos = buf;
            *info.pos.as_ptr() = buf;
            *info.flag.as_ptr() = 0;
        }
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

    pub fn data_buf_hdr(&self) -> &mut crate::entries::thread_nwm::DataHdr {
        unsafe { crate::entries::thread_nwm::get_data_buf_hdrs().get_mut(&self.v().work_index()) }
    }

    pub fn blit_ctx(&self) -> &mut BlitCtx {
        unsafe { blit_ctxes.get_mut(&self.0.work_index()) }
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
            if f == core_count.get() - 1 {
                syn.work_done_count.store(0, Ordering::Relaxed);
                syn.work_begin_flag.store(false, Ordering::Relaxed);

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

    pub fn get_last_frame_timing(&self) -> u32_ {
        unsafe { *last_frame_timings.get_b(self.v().is_top()) }
    }

    pub fn set_last_frame_timing(&self, timing: u32_) {
        unsafe { *last_frame_timings.get_b_mut(self.v().is_top()) = timing }
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

            let ctx = self.ctx();
            let src_len = ctx.src_len();
            let _ = svcInvalidateProcessDataCache(CUR_PROCESS_HANDLE, ctx.src as u32_, src_len);
        }
    }

    pub fn frame_changed(&self) -> bool {
        unsafe {
            let ctx = self.ctx();
            let src_len = ctx.src_len();
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

    pub fn last_row_last_n(&self) -> &mut u32_ {
        unsafe { &mut last_row_last_n }
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
    crate::entries::thread_screen::set_no_skip_frame(false);
    crate::entries::thread_screen::set_no_skip_frame(true);

    let _ = svcSignalEvent(*(*syn_handles).port_screen_ready.get_b(false));
    let _ = svcSignalEvent(*(*syn_handles).port_screen_ready.get_b(true));
}
