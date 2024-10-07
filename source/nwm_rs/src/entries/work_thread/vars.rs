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
static mut blit_formats: RangedArray<u32_, SCREEN_COUNT> = const_default();

pub unsafe fn get_blit_format_changed(is_top: bool, format: u32_) -> bool {
    let blit_format = blit_formats.get_b_mut(if is_top { false } else { true });
    if *blit_format == format {
        false
    } else {
        *blit_format = format;
        true
    }
}

static mut term_dsts: RangedArray<RangedArray<*mut u8, RP_CORE_COUNT_MAX>, WORK_COUNT> =
    const_default();

#[derive(ConstDefault, Clone, Copy)]
pub struct TermInfo {
    pub is_top: bool,
    pub core_count: CoreCount,
    pub v_adjusted: u32,
    pub v_last_adjusted: u32,
}

static mut term_infos: RangedArray<TermInfo, WORK_COUNT> = const_default();
static mut jpeg_quality: u32 = const_default();
static mut jpeg_chroma_ss: u32 = const_default();

pub unsafe fn set_term_dst(dst: *mut u8, w: WorkIndex, t: ThreadId) -> bool {
    let d = term_dsts.get_mut(&w).get_mut(&t);
    if *d == ptr::null_mut() {
        *d = dst;
        return true;
    }
    return false;
}

pub unsafe fn set_term_info(info: &TermInfo, w: WorkIndex) {
    *term_infos.get_mut(&w) = *info;
}

const _arq_rp_size_size_assert: () = {
    assert!(PACKET_SIZE <= ((1 << RP_KCP_HDR_SIZE_NBITS) - 1));
};

const _arq_rp_quality_size_assert: () = {
    assert!(RP_QUALITY_MAX <= ((1 << RP_KCP_HDR_QUALITY_NBITS) - 1));
};

// FIXME endianness
#[named]
unsafe fn send_term_dsts(w: WorkIndex) -> bool {
    if *term_dsts.get(&w).get(&ThreadId::init_unchecked(0)) == ptr::null_mut() {
        return true;
    }

    if entries::thread_nwm::thread_wait_sync(term_seg_mem_sem) == None {
        return false;
    }

    let mut terms: [*mut u8; RP_CORE_COUNT_MAX as usize + 1] = const_default();
    let mut term_cur = 0;
    let mut term_size = 0;
    terms[term_cur] = if let Some(d) = rp_term_data_buf_malloc() {
        d.add((NWM_HDR_SIZE + ARQ_OVERHEAD_SIZE + ARQ_DATA_HDR_SIZE) as usize)
    } else {
        return false;
    };

    let rp_packet_data_size = entries::thread_nwm::get_packet_data_size();
    let mut copy_to_terms = |mut data: *const u8, mut len: usize| {
        while len > 0 {
            let len_0 = rp_packet_data_size as usize - term_size;
            if len_0 >= len {
                ptr::copy_nonoverlapping(
                    data,
                    terms.get_unchecked_mut(term_cur).add(term_size),
                    len,
                );
                term_size += len;
                break;
            } else {
                if len_0 > 0 {
                    ptr::copy_nonoverlapping(
                        data,
                        terms.get_unchecked_mut(term_cur).add(term_size),
                        len_0,
                    );
                    data = data.add(len_0);
                    len -= len_0;
                }
                term_cur += 1;
                term_size = 0;
                terms[term_cur] = if let Some(d) = rp_term_data_buf_malloc() {
                    d.add((NWM_HDR_SIZE + ARQ_OVERHEAD_SIZE + ARQ_DATA_HDR_SIZE) as usize)
                } else {
                    return false;
                };
            }
        }
        true
    };

    let info = term_infos.get(&w);
    let hdr = (jpeg_chroma_ss as u16) << (RP_KCP_HDR_QUALITY_NBITS + RP_KCP_HDR_T_NBITS + 1)
        | (if info.is_top { 0 } else { 1 } as u16)
            << (RP_KCP_HDR_QUALITY_NBITS + RP_KCP_HDR_T_NBITS)
        | (info.core_count.get() as u16) << RP_KCP_HDR_QUALITY_NBITS
        | jpeg_quality as u16;
    if !copy_to_terms(&hdr as *const u16 as *const _, mem::size_of_val(&hdr)) {
        return false;
    }

    let core_count = get_core_count_in_use();
    let mut sizes: RangedArray<u32, RP_CORE_COUNT_MAX> = const_default();
    for i in ThreadId::up_to(&core_count) {
        let dst = *term_dsts.get_mut(&w).get_mut(&i);
        if dst == ptr::null_mut() {
            return false;
        }

        ptr::copy_nonoverlapping(
            dst.sub(mem::size_of::<u32>()) as *const _,
            sizes.get_mut(&i),
            1,
        );
        let size = *sizes.get(&i) as u16;

        let hdr = size
            | if i.get() == core_count.get() - 1 {
                (info.v_last_adjusted as u16) << RP_KCP_HDR_SIZE_NBITS
            } else {
                (info.v_adjusted as u16) << RP_KCP_HDR_SIZE_NBITS
            };
        if !copy_to_terms(&hdr as *const u16 as *const _, mem::size_of_val(&hdr)) {
            return false;
        }
    }

    for i in ThreadId::up_to(&core_count) {
        let dst_ref = term_dsts.get_mut(&w).get_mut(&i);
        let dst = *dst_ref;

        if !copy_to_terms(dst, *sizes.get(&i) as usize) {
            return false;
        }

        rp_seg_data_buf_free(dst.sub(ARQ_DATA_HDR_SIZE as usize));
        *dst_ref = ptr::null_mut();
    }

    for i in 0..=term_cur {
        let mut dst = *terms.get_unchecked_mut(i);

        if i == term_cur {
            ptr::write_bytes(dst.add(term_size), 0, rp_packet_data_size - term_size);
        }

        let mut size = rp_packet_data_size as u32;

        dst = dst.sub(ARQ_DATA_HDR_SIZE as usize);
        size += ARQ_DATA_HDR_SIZE;

        let hdr = (w.get() as u16) << (PID_NBITS + CID_NBITS)
            | (RP_CORE_COUNT_MAX as u16) << (PID_NBITS + CID_NBITS + RP_KCP_HDR_W_NBITS);
        ptr::copy_nonoverlapping(&hdr, dst as *mut _, 1);

        size |= 1 << 31;
        if i == term_cur {
            size |= 1 << 30;
        }
        ptr::copy_nonoverlapping(&size, dst.sub(mem::size_of::<u32>()) as *mut _, 1);

        let cb = &mut *reliable_stream_cb;
        while !entries::work_thread::reset_threads() {
            let res = rp_syn_rel1(&mut cb.nwm_syn, dst as *mut _);
            if res == 0 {
                break;
            }
            if res != RES_TIMEOUT as s32 {
                nsDbgPrint!(waitForSyncFailed, c_str!("nwm_syn.rp_syn_rel1"), res);
                entries::work_thread::set_reset_threads_ar();
                return false;
            }
        }
    }

    true
}

#[named]
pub unsafe fn rp_term_data_buf_malloc() -> Option<*mut c_char> {
    // entries::thread_nwm::thread_wait_sync(term_seg_mem_sem)?;
    entries::thread_nwm::thread_wait_sync(seg_mem_lock)?;

    let cb = &mut *reliable_stream_cb;
    let dst = mp_malloc(&mut cb.send_pool) as *mut u8;
    if dst == ptr::null_mut() {
        nsDbgPrint!(mpAllocFailed, c_str!("send_pool"));
        crate::entries::work_thread::set_reset_threads_ar();
        let res = svcReleaseMutex(seg_mem_lock);
        if res != 0 {
            nsDbgPrint!(releaseMutexFailed, c_str!("seg_mem_lock"), res);
        }
        return None;
    }
    let res = svcReleaseMutex(seg_mem_lock);
    if res != 0 {
        nsDbgPrint!(releaseMutexFailed, c_str!("seg_mem_lock"), res);
    }
    Some(dst)
}

#[named]
#[no_mangle]
unsafe fn rp_term_data_buf_free(dst: *const ::libc::c_char) {
    if entries::thread_nwm::thread_wait_sync(seg_mem_lock) == None {
        return;
    }

    let cb = &mut *reliable_stream_cb;
    if mp_free(
        &mut cb.send_pool,
        dst.sub((NWM_HDR_SIZE + ARQ_OVERHEAD_SIZE) as usize) as *mut _,
    ) < 0
    {
        nsDbgPrint!(mpFreeFailed, c_str!("send_pool"));
        let res = svcReleaseMutex(seg_mem_lock);
        if res != 0 {
            nsDbgPrint!(releaseMutexFailed, c_str!("seg_mem_lock"), res);
        }
        return;
    }

    // let mut count = mem::MaybeUninit::uninit();
    // let res = svcReleaseSemaphore(count.as_mut_ptr(), term_seg_mem_sem, 1);
    // if res != 0 {
    //     nsDbgPrint!(releaseSemaphoreFailed, c_str!("term_seg_mem_sem"), 0, res);
    // }

    let res = svcReleaseMutex(seg_mem_lock);
    if res != 0 {
        nsDbgPrint!(releaseMutexFailed, c_str!("seg_mem_lock"), res);
    }
}

#[named]
#[no_mangle]
unsafe fn rp_term_notify() {
    let mut count = mem::MaybeUninit::uninit();
    let res = svcReleaseSemaphore(count.as_mut_ptr(), term_seg_mem_sem, 1);
    if res != 0 {
        nsDbgPrint!(releaseSemaphoreFailed, c_str!("term_seg_mem_sem"), 0, res);
    }
}

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

pub unsafe fn reset_vars(quality: u32, chroma_ss: u32) {
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

    term_dsts = const_default();
    jpeg_quality = quality;
    jpeg_chroma_ss = chroma_ss;
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

    pub fn release(self) -> Option<()> {
        unsafe {
            let w = self.v().work_index();
            let syn = (*syn_handles).works.get(&w);

            let f = syn.work_done_count.fetch_add(1, Ordering::AcqRel);
            let core_count = get_core_count_in_use();
            if f == core_count.get() - 1 {
                if !send_term_dsts(w) {
                    return None;
                }

                syn.work_done_count.store(0, Ordering::Release);
                syn.work_begin_flag.store(false, Ordering::Release);

                self.v().release_work_done();
            }
            Some(())
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
                .swap(true, Ordering::AcqRel)
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
            self.v().set_skip_frame(false);
            self.v().clear_screen_synced();

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
