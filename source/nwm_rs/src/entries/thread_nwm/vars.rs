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

static mut next_send_tick: u32_ = 0;
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

static mut reliable_stream: bool = const_default();
static mut reliable_stream_method: u8 = const_default();

const reliable_stream_kcp: u8 = 0;

static mut packet_data_size: usize = 0;

#[derive(PartialEq, Eq)]
pub enum ReliableStreamMethod {
    None,
    KCP,
}

pub unsafe fn get_reliable_stream_method() -> ReliableStreamMethod {
    if reliable_stream {
        if reliable_stream_method == reliable_stream_kcp {
            ReliableStreamMethod::KCP
        } else {
            ReliableStreamMethod::None
        }
    } else {
        ReliableStreamMethod::None
    }
}

static mut kcp_conv_count: u8 = 0;

unsafe fn init_reliable_stream(flags: u32_, qos: u32_) -> Option<()> {
    let nwm_lock = if let Some(l) = NwmCbLock::lock() {
        l
    } else {
        return None;
    };

    reliable_stream = flags & RP_CONFIG_RELIABLE_STREAM_FLAG > 0;
    reliable_stream_method = reliable_stream_kcp;

    set_packet_data_size();

    match get_reliable_stream_method() {
        ReliableStreamMethod::None => {}
        ReliableStreamMethod::KCP => {
            let kcp = &mut (*reliable_stream_cb).ikcp;

            if ikcp_create(kcp, kcp_conv_count as u16) < 0 {
                return None;
            }
            let sndwnd = ((ARQ_BUFS_COUNT * qos + RP_QOS_MAX / 2) / RP_QOS_MAX) as i32;
            let curwnd = ((ARQ_CUR_BUFS_COUNT * qos + RP_QOS_MAX / 2) / RP_QOS_MAX) as i32;
            if ikcp_wndsize(kcp, sndwnd, curwnd) != 0 {
                return None;
            }

            drop(nwm_lock);

            kcp_conv_count += 1;
        }
    }

    Some(())
}

pub unsafe fn get_packet_data_size() -> usize {
    packet_data_size
}

const packet_data_size_method_none: usize = {
    let size = (PACKET_SIZE - DATA_HDR_SIZE) as usize;
    assert!(size % mem::size_of::<usize>() == 0);
    size
};

unsafe fn set_packet_data_size() {
    packet_data_size = match get_reliable_stream_method() {
        ReliableStreamMethod::None => packet_data_size_method_none,
        ReliableStreamMethod::KCP => (PACKET_SIZE - ARQ_OVERHEAD_SIZE - ARQ_DATA_HDR_SIZE) as usize,
    }
}

static mut min_send_interval_tick: u32_ = 0;
static mut min_send_interval_ns: u32_ = 0;

unsafe fn init_min_send_interval(qos: u32_) {
    min_send_interval_tick =
        core::intrinsics::unchecked_div(SYSCLOCK_ARM11 as u64_ * PACKET_SIZE as u64_, qos as u64_)
            as u32_;
    min_send_interval_ns =
        (min_send_interval_tick as u64_ * 1000_000_000 / SYSCLOCK_ARM11 as u64_) as u32_;
}

pub unsafe fn reset_vars(dst_flags: u32, qos: u32) -> Option<()> {
    init_reliable_stream(dst_flags, qos)?;
    init_min_send_interval(qos);

    for i in WorkIndex::all() {
        *nwm_need_syn.get_mut(&i) = true;
    }
    nwm_work_index = WorkIndex::init();
    nwm_thread_id = ThreadId::init();
    rp_output_next_tick = svcGetSystemTick() as s64 + min_send_interval_tick as s64;
    next_send_tick = rp_output_next_tick as u32_;
    cur_seg_mem_count = 0;
    Some(())
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

    pub fn next_send_tick(&self) -> &mut u32_ {
        unsafe { &mut next_send_tick }
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
pub unsafe fn rp_send_buffer(dst: &mut crate::jpeg::WorkerDst, term: bool) -> bool {
    let rp_packet_data_size = get_packet_data_size();
    let mut size = rp_packet_data_size;
    const term_flag: u8 = 0x10;
    if term {
        size -= dst.free_in_bytes as usize;
    }

    dst.dst = match get_reliable_stream_method() {
        ReliableStreamMethod::None => {
            let ninfo = &*dst.user.info;
            let dinfo = &ninfo.info;

            let mut pos_next = (*dinfo.pos.as_ptr()).add(size);

            dinfo.pos.store(pos_next, Ordering::Release);
            if term {
                dinfo.flag.store(term_flag as u32, Ordering::Release);
            }

            if !term && pos_next > ninfo.buf_packet_last {
                pos_next = ninfo.buf_packet_last;
                nsDbgPrint!(sendBufferOverflow);
            }

            let res = svcSignalEvent((*syn_handles).nwm_ready);
            if res != 0 {
                nsDbgPrint!(nwmEventSignalFailed, res);
            }

            pos_next
        }
        ReliableStreamMethod::KCP => {
            let hdr = &dst.user.hdr;
            let mut dst = if term {
                dst.dst
                    .sub(rp_packet_data_size - dst.free_in_bytes as usize)
            } else {
                // assert!(dst.free_in_bytes == 0);
                dst.dst.sub(rp_packet_data_size)
            };

            let mut size = size as u32;
            if !term {
                dst = dst.sub(ARQ_DATA_HDR_SIZE as usize);
                size += ARQ_DATA_HDR_SIZE;
                hdr.write_hdr(dst);
            }

            ptr::copy_nonoverlapping(&size, dst.sub(mem::size_of::<u32>()) as *mut _, 1);

            if term {
                return entries::work_thread::set_term_dst(dst, hdr.w, hdr.t);
            } else {
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

                if let Some(dst) = rp_data_buf_malloc() {
                    dst.add((NWM_HDR_SIZE + ARQ_OVERHEAD_SIZE + ARQ_DATA_HDR_SIZE) as usize)
                } else {
                    return false;
                }
            }
        }
    };
    dst.free_in_bytes = rp_packet_data_size as u16;
    true
}

unsafe fn init_udp_packet(nwm_buf: *mut u8_, mut len: u32_) -> u32_ {
    len += 8;
    *(nwm_buf.add(0x22 + 8) as *mut u16_) = htons(RP_SRC_PORT as u16_); // src port
    *(nwm_buf.add(0x24 + 8) as *mut u16_) =
        htons(AtomicU32::from_mut(&mut (*rp_config).dstPort).load(Ordering::Relaxed) as u16_); // dest port
    *(nwm_buf.add(0x26 + 8) as *mut u16_) = htons(len as u16_);
    *(nwm_buf.add(0x28 + 8) as *mut u16_) = 0; // no checksum
    len += 20;

    *(nwm_buf.add(0x10 + 8) as *mut u16_) = htons(len as u16_);
    *(nwm_buf.add(0x12 + 8) as *mut u16_) = 0xaf01; // packet id is a random value since we won't use the fragment
    *(nwm_buf.add(0x14 + 8) as *mut u16_) = 0x0040; // no fragment
    *(nwm_buf.add(0x16 + 8) as *mut u16_) = 0x1140; // ttl 64, udp

    *(nwm_buf.add(0x18 + 8) as *mut u16_) = 0;
    *(nwm_buf.add(0x18 + 8) as *mut u16_) = ip_checksum(nwm_buf.add(0xE + 8), 0x14);

    len += 22;
    *(nwm_buf.add(12) as *mut u16_) = htons(len as u16_);

    len
}

unsafe fn ip_checksum(data: *mut u8_, mut length: usize) -> u16_ {
    // Cast the data pointer to one that can be indexed.
    // Initialise the accumulator.
    let mut acc: u32_ = 0;

    if length % 2 != 0 {
        *data.add(length) = 0;
        length += 1;
    }

    length /= 2;
    let data = data as *mut u16_;

    // Handle complete 16-bit blocks.
    for i in 0..length {
        acc += ntohs(*data.add(i)) as u32_;
    }
    acc = (acc & 0xffff) + (acc >> 16);
    acc += acc >> 16;

    // Return the checksum in network byte order.
    htons(!acc as u16_)
}

static mut rp_output_next_tick: s64 = 0;

#[no_mangle]
#[named]
unsafe extern "C" fn rp_udp_output(buf: *mut u8, len: s32, kcp: *mut ikcpcb) -> s32 {
    if len > PACKET_SIZE as s32 {
        nsDbgPrint!(nwmOutputOverflow, len);
        return -3;
    }

    let curr_tick = svcGetSystemTick() as s64;
    let tick_diff = rp_output_next_tick - curr_tick;
    let duration = if tick_diff > 0 {
        tick_diff as s64 * 1_000_000_000 / SYSCLOCK_ARM11 as s64
    } else {
        0
    };
    let next_interval = if NWM_PROPORTIONAL_MIN_INTERVAL > 0 {
        min_send_interval_tick as s64 * len as s64 / PACKET_SIZE as s64
    } else {
        min_send_interval_tick as s64
    };
    if duration > 0 {
        nwm_cb_unlock();
        svcSleepThread(duration);
        if nwm_cb_lock() == None {
            crate::entries::work_thread::set_reset_threads_ar();
            return -5;
        }
        if AtomicBool::from_ptr(&mut (*kcp).rp_output_retry).load(Ordering::Acquire) {
            return 0;
        }
        rp_output_next_tick = svcGetSystemTick() as s64 + next_interval;
    } else {
        rp_output_next_tick = curr_tick + next_interval;
    }

    nwm_output(buf.sub(NWM_HDR_SIZE as usize), len as usize);

    return len;
}

unsafe fn nwm_output(nwm_buf: *mut u8, packet_size: usize) {
    ptr::copy_nonoverlapping(
        get_current_nwm_hdr().as_mut_ptr(),
        nwm_buf,
        NWM_HDR_SIZE as usize,
    );
    let nwm_size = init_udp_packet(nwm_buf, packet_size as u32_);
    nwmSendPacket.unwrap_unchecked()(nwm_buf, nwm_size);
}

pub unsafe fn rp_output(packet_buf: *mut u8, packet_size: usize) -> Option<()> {
    let nwm_buf = packet_buf.sub(NWM_HDR_SIZE as usize);
    nwm_output(nwm_buf, packet_size);
    Some(())
}

#[no_mangle]
#[named]
unsafe extern "C" fn nsControlRecv(fd: c_int) -> c_int {
    let recv_bufs = if let Some(dst) = rp_recv_data_buf() {
        dst
    } else {
        return -1;
    };

    let mut recv_ret = 0;
    let mut recv_idx = 0;
    let mut recv_buf = ptr::null_mut::<u8>();
    loop {
        let buf = recv_bufs.get_unchecked_mut(recv_idx);
        let ret = recv(
            fd,
            buf.as_mut_ptr() as *mut _,
            RP_RECV_PACKET_SIZE as usize,
            0,
        );
        if ret == 0 {
            break;
        } else if ret < 0 {
            let err = *__errno();
            if err != ctru::EWOULDBLOCK as i32 || err != ctru::EAGAIN as i32 {
                nsDbgPrint!(nwmInputFailed, ret as i32, err);
                return 0;
            }
            break;
        }
        recv_ret = ret;
        recv_buf = buf.as_mut_ptr();
        recv_idx = (recv_idx + 1) % RP_RECV_BUF_N as usize;
    }

    if recv_ret == 0 {
        nsDbgPrint!(nwmInputNothing);
        return 0;
    }

    let nwm_lock = if let Some(l) = NwmCbLock::lock() {
        l
    } else {
        return -1;
    };

    match get_reliable_stream_method() {
        ReliableStreamMethod::None => {
            return 0;
        }
        ReliableStreamMethod::KCP => {
            let cb = &mut (*reliable_stream_cb);
            let kcp = &mut cb.ikcp;

            let ret = ikcp_input(kcp, recv_buf, recv_ret as i32);
            if ret < 0 {
                // Reset KCP
                if ret < -0x10 {
                    nsDbgPrint!(kcpInputFailed, ret);
                }
                drop(nwm_lock);
                crate::entries::work_thread::set_reset_threads_ar();
                return -1;
            }
            drop(nwm_lock);
            let _ = svcSignalEvent(reliable_stream_cb_evt);
        }
    }

    0
}

#[derive(PartialEq, Eq)]
pub struct NwmCbLock();

impl NwmCbLock {
    pub unsafe fn lock() -> Option<Self> {
        let _ = nwm_cb_lock()?;
        Some(Self())
    }
}

impl Drop for NwmCbLock {
    fn drop(&mut self) {
        unsafe { nwm_cb_unlock() };
    }
}

#[named]
unsafe fn nwm_cb_lock() -> Option<()> {
    while !entries::work_thread::reset_threads() {
        let res = svcWaitSynchronization(reliable_stream_cb_lock, THREAD_WAIT_NS);

        if res == 0 {
            return Some(());
        }
        if res != RES_TIMEOUT as s32 {
            nsDbgPrint!(waitForSyncFailed, c_str!("reliable_stream_cb_sync"), res);
            entries::work_thread::set_reset_threads_ar();
            return None;
        }
    }
    None
}

#[named]
unsafe fn nwm_cb_unlock() {
    let res = svcReleaseMutex(reliable_stream_cb_lock);
    if res != 0 {
        nsDbgPrint!(releaseMutexFailed, c_str!("reliable_stream_cb_lock"), res);
    }
}

#[named]
pub unsafe fn thread_wait_sync(h: Handle) -> Option<()> {
    while !entries::work_thread::reset_threads() {
        let res = svcWaitSynchronization(h, THREAD_WAIT_NS);
        if res == 0 {
            return Some(());
        }
        if res != RES_TIMEOUT as s32 {
            nsDbgPrint!(waitForSyncFailed, c_str!("..."), res);
            entries::work_thread::set_reset_threads_ar();
            return None;
        }
    }
    None
}

#[named]
pub unsafe fn rp_data_buf_malloc() -> Option<*mut c_char> {
    thread_wait_sync(seg_mem_sem)?;
    thread_wait_sync(seg_mem_lock)?;

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

static mut cur_seg_mem_count: u32 = 0;

#[no_mangle]
#[named]
unsafe extern "C" fn ikcp_seg_data_buf_malloc() -> *mut c_char {
    if let Some(dst) = (|| {
        // thread_wait_sync(cur_seg_mem_sem)?;
        // thread_wait_sync(cur_seg_mem_lock)?;

        if cur_seg_mem_count == SEND_CUR_BUFS_COUNT {
            return None;
        }

        let cb = &mut *reliable_stream_cb;
        let dst = mp_malloc(&mut cb.cur_send_pool) as *mut u8;
        if dst == ptr::null_mut() {
            nsDbgPrint!(mpAllocFailed, c_str!("cur_send_pool"));
            crate::entries::work_thread::set_reset_threads_ar();
            // let res = svcReleaseMutex(cur_seg_mem_lock);
            // if res != 0 {
            //     nsDbgPrint!(releaseMutexFailed, c_str!("cur_seg_mem_lock"), res);
            // }
            return None;
        }

        cur_seg_mem_count += 1;
        // let res = svcReleaseMutex(cur_seg_mem_lock);
        // if res != 0 {
        //     nsDbgPrint!(releaseMutexFailed, c_str!("cur_seg_mem_lock"), res);
        // }
        Some(dst)
    })() {
        return dst.add((NWM_HDR_SIZE + ARQ_OVERHEAD_SIZE) as usize);
    } else {
        return ptr::null_mut();
    }
}

#[no_mangle]
#[named]
unsafe extern "C" fn ikcp_seg_data_buf_free(dst: *const ::libc::c_char) {
    // if thread_wait_sync(cur_seg_mem_lock) == None {
    //     return;
    // }

    let cb = &mut *reliable_stream_cb;
    if mp_free(
        &mut cb.cur_send_pool,
        dst.sub((NWM_HDR_SIZE + ARQ_OVERHEAD_SIZE) as usize) as *mut _,
    ) < 0
    {
        nsDbgPrint!(mpFreeFailed, c_str!("cur_send_pool"));
        // let _ = svcReleaseMutex(cur_seg_mem_lock);
        return;
    }
    // let mut count = mem::MaybeUninit::uninit();
    // let _ = svcReleaseSemaphore(count.as_mut_ptr(), cur_seg_mem_sem, 1);
    // if res != 0 {
    //     nsDbgPrint!(releaseSemaphoreFailed, c_str!("cur_seg_mem_sem"), 0, res);
    // }
    cur_seg_mem_count -= 1;

    // let _ = svcReleaseMutex(cur_seg_mem_lock);
}

#[named]
unsafe fn rp_data_buf_free(dst: *const ::libc::c_char) {
    if thread_wait_sync(seg_mem_lock) == None {
        return;
    }

    let cb = &mut *reliable_stream_cb;
    if mp_free(&mut cb.send_pool, dst as *mut _) < 0 {
        nsDbgPrint!(mpFreeFailed, c_str!("send_pool"));
        let res = svcReleaseMutex(seg_mem_lock);
        if res != 0 {
            nsDbgPrint!(releaseMutexFailed, c_str!("seg_mem_lock"), res);
        }
        return;
    }
    let mut count = mem::MaybeUninit::uninit();
    let res = svcReleaseSemaphore(count.as_mut_ptr(), seg_mem_sem, 1);
    if res != 0 {
        nsDbgPrint!(releaseSemaphoreFailed, c_str!("seg_mem_sem"), 0, res);
    }

    let res = svcReleaseMutex(seg_mem_lock);
    if res != 0 {
        nsDbgPrint!(releaseMutexFailed, c_str!("seg_mem_lock"), res);
    }
}

unsafe fn rp_recv_data_buf(
) -> Option<&'static mut [[c_char; RP_RECV_PACKET_SIZE as usize]; RP_RECV_BUF_N as usize]> {
    if !recv_seg_mem_inited.load(Ordering::Acquire) {
        return None;
    }

    let cb = &mut *reliable_stream_cb;
    Some(&mut cb.recv_buf)
}

#[no_mangle]
unsafe extern "C" fn rp_seg_data_buf_free(data_buf: *const ::libc::c_char) {
    rp_data_buf_free(data_buf.sub((NWM_HDR_SIZE + ARQ_OVERHEAD_SIZE) as usize) as *mut _)
}

#[named]
unsafe fn kcp_thread_nwm_loop() -> bool {
    let cb = &mut *reliable_stream_cb;

    if let Some(_) = nwm_cb_lock() {
        let kcp = &mut cb.ikcp;

        let mut dst = mem::MaybeUninit::uninit();
        let mut has_dst = false;

        while !entries::work_thread::reset_threads() {
            if (svcGetSystemTick() as s64 - rp_output_next_tick) / SYSCLOCK_ARM11 as s64
                >= RP_KCP_TIMEOUT_SEC
            {
                // Reset KCP
                nsDbgPrint!(kcpTimeout);
                crate::entries::work_thread::set_reset_threads_ar();
                nwm_cb_unlock();
                return false;
            }

            let can_queue = ikcp_queue_get_free(kcp) > 0;
            let send_delay = ikcp_send_ready_and_get_delay(kcp);

            let timeout = if send_delay >= 0 {
                if send_delay == 0 {
                    0
                } else {
                    let delay = rp_output_next_tick - svcGetSystemTick() as s64
                        + ((send_delay - 1) as u32 * min_send_interval_tick) as s64;
                    if delay > 0 {
                        delay * 1_000_000_000 / SYSCLOCK_ARM11 as s64
                    } else {
                        0
                    }
                }
            } else {
                if send_delay < -0x10 {
                    // Reset KCP
                    nsDbgPrint!(kcpSendFailed, send_delay);
                    crate::entries::work_thread::set_reset_threads_ar();
                    nwm_cb_unlock();
                    return false;
                }
                THREAD_WAIT_NS
            };

            let mut retry = false;

            let relock_nwm = timeout != 0;

            if relock_nwm {
                nwm_cb_unlock();
            }

            if can_queue && !has_dst {
                while !entries::work_thread::reset_threads() {
                    let res = rp_syn_acq(&mut cb.nwm_syn, timeout, dst.as_mut_ptr());

                    if res == 0 {
                        has_dst = true;
                        break;
                    }
                    if res != RES_TIMEOUT as s32 {
                        nsDbgPrint!(waitForSyncFailed, c_str!("nwm_syn.rp_syn_acq"), res);
                        entries::work_thread::set_reset_threads_ar();
                        if !relock_nwm {
                            nwm_cb_unlock();
                        }
                        return false;
                    } else if send_delay >= 0 || (*kcp).session_new_data_received {
                        break;
                    }
                }
            } else if timeout > 0 {
                if send_delay < 0 {
                    let res = svcWaitSynchronization(reliable_stream_cb_evt, timeout);
                    if res != 0 && res != RES_TIMEOUT as s32 {
                        nsDbgPrint!(waitForSyncFailed, c_str!("reliable_stream_cb_evt"), res);
                        entries::work_thread::set_reset_threads_ar();
                        if !relock_nwm {
                            nwm_cb_unlock();
                        }
                        return false;
                    }
                    if res == 0 && !has_dst {
                        retry = true;
                    }
                } else {
                    svcSleepThread(timeout);
                }
            }

            if relock_nwm {
                if nwm_cb_lock() == None {
                    crate::entries::work_thread::set_reset_threads_ar();
                    return false;
                }
            }

            if retry {
                continue;
            }

            let mut dst_queued = false;
            if has_dst {
                let dst = dst.assume_init() as *mut u8;

                let mut size: u32 = 0;
                ptr::copy_nonoverlapping(dst.sub(mem::size_of::<u32>()) as *const _, &mut size, 1);

                let ret = ikcp_queue(kcp, dst, size as i32);
                if ret < 0 {
                    // Reset KCP
                    nsDbgPrint!(kcpSendFailed, ret);
                    crate::entries::work_thread::set_reset_threads_ar();
                    nwm_cb_unlock();
                    return false;
                } else if ret == 0 {
                    has_dst = false;
                    dst_queued = true;
                }
            }

            // Ready send again
            let ret = ikcp_send_ready_and_get_delay(kcp);
            if ret < -0x10 && dst_queued {
                // Reset KCP
                nsDbgPrint!(kcpSendFailed, ret);
                crate::entries::work_thread::set_reset_threads_ar();
                nwm_cb_unlock();
                return false;
            }
            if ret < 0 {
                if (svcGetSystemTick() as s64 - rp_output_next_tick) / SYSCLOCK_ARM11 as s64
                    >= RP_KCP_TIMEOUT_SEC
                {
                    // Reset KCP
                    nsDbgPrint!(kcpTimeout);
                    crate::entries::work_thread::set_reset_threads_ar();
                    nwm_cb_unlock();
                    return false;
                }
            } else {
                // Send next
                let ret = ikcp_send_next(kcp);
                if ret < 0 {
                    // Reset KCP
                    if !entries::work_thread::reset_threads() {
                        nsDbgPrint!(kcpFlushFailed, ret);
                    }
                    crate::entries::work_thread::set_reset_threads_ar();
                    nwm_cb_unlock();
                    return false;
                }
            }
        }

        nwm_cb_unlock();
        true
    } else {
        crate::entries::work_thread::set_reset_threads_ar();
        false
    }
}

pub unsafe extern "C" fn kcp_thread_nwm(_: *mut c_void) {
    while !crate::entries::work_thread::reset_threads() && kcp_thread_nwm_loop() {}
    svcExitThread()
}
