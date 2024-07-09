use super::*;

#[derive(ConstDefault)]
pub struct DataBufInfo {
    pub send_pos: *mut u8_,
    pub pos: AtomicPtr<u8_>,
    pub flag: AtomicU32,
}

#[derive(ConstDefault)]
pub struct NwmInfo {
    pub buf: *mut u8_,
    pub buf_packet_last: *mut u8_,
    pub info: DataBufInfo,
}

pub type NwmThreadInfos = RangedArray<NwmInfo, RP_CORE_COUNT_MAX>;
pub type NwmInfos = RangedArray<NwmThreadInfos, WORK_COUNT>;
static mut nwm_infos: NwmInfos = const_default();

pub unsafe fn get_nwm_infos() -> &'static mut NwmInfos {
    &mut nwm_infos
}

static mut nwm_work_index: WorkIndex = WorkIndex::init();
static mut nwm_thread_id: ThreadId = ThreadId::init();

static mut nwm_need_syn: RangedArray<bool, WORK_COUNT> = const_default();

static mut last_send_tick: u32_ = 0;
pub type NwmHdr = [u8_; NWM_HDR_SIZE as usize];
static mut current_nwm_hdr: NwmHdr = const_default();

pub unsafe fn get_current_nwm_hdr() -> &'static mut NwmHdr {
    &mut current_nwm_hdr
}

pub type DataHdr = [u8_; DATA_HDR_SIZE as usize];
static mut data_buf_hdrs: RangedArray<DataHdr, WORK_COUNT> = const_default();

pub unsafe fn get_data_buf_hdrs() -> &'static mut RangedArray<DataHdr, WORK_COUNT> {
    &mut data_buf_hdrs
}

static mut min_send_interval_tick: u32_ = 0;
static mut min_send_interval_ns: u32_ = 0;

pub fn init_min_send_interval(qos: u32_) {
    unsafe {
        min_send_interval_tick = core::intrinsics::unchecked_div(
            SYSCLOCK_ARM11 as u64_ * PACKET_SIZE as u64_,
            qos as u64_,
        ) as u32_;
        min_send_interval_ns =
            (min_send_interval_tick as u64_ * 1000_000_000 / SYSCLOCK_ARM11 as u64_) as u32_;
    }
}

pub unsafe fn reset_vars() {
    for i in WorkIndex::all() {
        *nwm_need_syn.get_mut(&i) = true;
    }
    nwm_work_index = WorkIndex::init();
    nwm_thread_id = ThreadId::init();
    last_send_tick = svcGetSystemTick() as u32_;
}

pub struct ThreadVars(());

impl ThreadVars {
    pub fn work_index(&self) -> &mut WorkIndex {
        unsafe { &mut nwm_work_index }
    }

    pub fn thread_id(&self) -> &mut ThreadId {
        unsafe { &mut nwm_thread_id }
    }

    #[named]
    pub fn sync(&self, work_flush: bool) -> bool {
        unsafe {
            let w = self.work_index();
            let syn = nwm_need_syn.get_mut(&w);
            if *syn {
                let res = svcWaitSynchronization(
                    (*syn_handles).works.get(&w).nwm_ready,
                    if work_flush { THREAD_WAIT_NS } else { 0 },
                );
                if res != 0 {
                    if res != RES_TIMEOUT as s32 {
                        nsDbgPrint!(waitForSyncFailed, c_str!("nwm_ready"), res);
                    }
                    return false;
                }
                *syn = false;
            }
            true
        }
    }

    pub fn nwm_infos(&self) -> &mut NwmThreadInfos {
        unsafe { nwm_infos.get_mut(&self.work_index()) }
    }

    pub fn nwm_info(&self) -> &mut NwmInfo {
        self.nwm_infos().get_mut(&self.thread_id())
    }

    pub fn last_send_tick(&self) -> &mut u32_ {
        unsafe { &mut last_send_tick }
    }

    pub fn min_send_interval_tick(&self) -> u32_ {
        unsafe { min_send_interval_tick }
    }

    pub fn data_buf_hdr(&self) -> &mut DataHdr {
        unsafe { data_buf_hdrs.get_mut(&self.work_index()) }
    }

    #[named]
    pub fn release(&self) {
        unsafe {
            let w = self.work_index();
            let mut count = mem::MaybeUninit::uninit();
            let res =
                svcReleaseSemaphore(count.as_mut_ptr(), (*syn_handles).works.get(w).nwm_done, 1);
            if res != 0 {
                nsDbgPrint!(releaseSemaphoreFailed, c_str!("nwm_done"), w.get(), res);
            }

            *nwm_need_syn.get_mut(w) = true;
            w.next_wrapped();
        }
    }
}

pub extern "C" fn thread_nwm(_: *mut c_void) {
    unsafe {
        while !crate::entries::work_thread::reset_threads() {
            if safe_impl::try_send_next_buffer(ThreadVars(()), true) {
                svcSleepThread(min_send_interval_ns as s64);
                continue;
            }

            let res = svcWaitSynchronization((*syn_handles).nwm_ready, THREAD_WAIT_NS);
            if res != 0 {
                if res != RES_TIMEOUT as s32 {
                    svcSleepThread(THREAD_WAIT_NS);
                }
            }
        }
        svcExitThread()
    }
}

#[named]
pub unsafe fn wait_nwm_done(w: &WorkIndex) -> bool {
    loop {
        if crate::entries::work_thread::reset_threads() {
            return false;
        }

        let res = svcWaitSynchronization((*syn_handles).works.get(w).nwm_done, THREAD_WAIT_NS);
        if res != 0 {
            if res != RES_TIMEOUT as s32 {
                nsDbgPrint!(waitForSyncFailed, c_str!("nwm_done"), res);
                svcSleepThread(THREAD_WAIT_NS);
            }
            continue;
        }
        return true;
    }
}

#[named]
pub unsafe fn release_nwm_ready(w: &WorkIndex) {
    let mut count = mem::MaybeUninit::uninit();
    let res = svcReleaseSemaphore(count.as_mut_ptr(), (*syn_handles).works.get(w).nwm_ready, 1);
    if res != 0 {
        nsDbgPrint!(releaseSemaphoreFailed, c_str!("nwm_ready"), w.get(), res);
    }
}

#[named]
pub unsafe fn rp_send_buffer(dst: &mut crate::jpeg::WorkerDst, term: bool) {
    let ninfo = &*dst.info;
    let dinfo = &ninfo.info;

    let mut size = crate::jpeg::vars::OUTPUT_BUF_SIZE as usize;
    if term {
        size -= dst.free_in_bytes as usize;
    }

    let mut pos_next = (*dinfo.pos.as_ptr()).add(size);

    if pos_next > ninfo.buf_packet_last {
        pos_next = ninfo.buf_packet_last;
        nsDbgPrint!(sendBufferOverflow);
    }

    dinfo.pos.store(pos_next, Ordering::Relaxed);
    if term {
        dinfo.flag.store(0x10, Ordering::Release);
    }

    dst.dst = pos_next as *mut _;
    dst.free_in_bytes = crate::jpeg::vars::OUTPUT_BUF_SIZE as u16;

    let res = svcSignalEvent((*syn_handles).nwm_ready);
    if res != 0 {
        nsDbgPrint!(nwmEventSignalFailed, res);
    }
}
