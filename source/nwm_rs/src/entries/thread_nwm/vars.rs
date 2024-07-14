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

static mut reliable_stream_cb: *mut rp_cb = const_default();
static mut reliable_stream: bool = const_default();
static mut reliable_stream_method: u8 = const_default();

const reliable_stream_kcp: u8 = 0;

static mut packet_data_size: usize = 0;

#[derive(PartialEq, Eq)]
enum ReliableStreamMethod {
    None,
    KCP,
}

unsafe fn get_reliable_stream_method() -> ReliableStreamMethod {
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

unsafe fn init_reliable_stream(flags: u32_, qos: u32_) -> Option<()> {
    let _ = NwmCbLock::lock()?;

    reliable_stream_cb_inited = false;
    reliable_stream = flags & (1 << 30) > 0;
    reliable_stream_method = ((flags >> 31) & 1) as u8;

    if get_reliable_stream_method() != ReliableStreamMethod::None {
        if reliable_stream_cb == ptr::null_mut() {
            reliable_stream_cb =
                request_mem_from_pool::<{ mem::size_of::<rp_cb>() }>()?.to_ptr() as *mut _;
        }
    }

    set_packet_data_size();

    match get_reliable_stream_method() {
        ReliableStreamMethod::None => {}
        ReliableStreamMethod::KCP => {
            let kcp = &mut (*reliable_stream_cb).cb.ikcp;
            if ikcp_create(kcp, RP_HDR_RELIABLE_STREAM_FLAG, ptr::null_mut()) < 0 {
                return None;
            }
            kcp.output = Some(rp_udp_output);
            ikcp_nodelay(kcp, 2, 0, 2, 0);
            ikcp_wndsize(kcp, (qos / PACKET_SIZE / 16) as i32);
            kcp.rx_minrto = 16;
        }
    }

    reliable_stream_cb_inited = true;
    Some(())
}

pub unsafe fn get_packet_data_size() -> usize {
    packet_data_size
}

unsafe fn set_packet_data_size() {
    packet_data_size = match get_reliable_stream_method() {
        ReliableStreamMethod::None => (PACKET_SIZE - DATA_HDR_SIZE) as usize,
        ReliableStreamMethod::KCP => (PACKET_SIZE - IKCP_OVERHEAD_CONST - DATA_HDR_SIZE) as usize,
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
    last_send_tick = svcGetSystemTick() as u32_;
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

    let rp_packet_data_size = get_packet_data_size();
    let mut size = rp_packet_data_size;
    if term {
        size -= dst.free_in_bytes as usize;
    }

    let mut pos_next = (*dinfo.pos.as_ptr()).add(size);

    if !term && pos_next > ninfo.buf_packet_last {
        pos_next = ninfo.buf_packet_last;
        nsDbgPrint!(sendBufferOverflow);
    }

    dinfo.pos.store(pos_next, Ordering::Relaxed);
    if term {
        dinfo.flag.store(0x10, Ordering::Release);
    }

    dst.dst = pos_next as *mut _;
    dst.free_in_bytes = rp_packet_data_size as u16;

    let res = svcSignalEvent((*syn_handles).nwm_ready);
    if res != 0 {
        nsDbgPrint!(nwmEventSignalFailed, res);
    }
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

static mut rp_output_last_tick: u64 = 0;

#[named]
unsafe extern "C" fn rp_udp_output(
    buf: *const u8,
    len: s32,
    _kcp: *mut ikcpcb,
    _user: *mut c_void,
) -> s32 {
    let nwm_buf = (*reliable_stream_cb).nwm_buf.as_mut_ptr();
    let packet_buf = nwm_buf.add(NWM_HDR_SIZE as usize);

    if len > PACKET_SIZE as s32 {
        nsDbgPrint!(nwmOutputOverflow, len);
        return 0;
    }

    ptr::copy_nonoverlapping(buf, packet_buf, len as usize);

    let curr_tick = svcGetSystemTick();
    let tick_diff = curr_tick - rp_output_last_tick;
    let duration = if tick_diff < min_send_interval_tick as u64 {
        (min_send_interval_tick as u64 - tick_diff) * 1_000_000_000 / SYSCLOCK_ARM11 as u64
    } else {
        0
    };
    rp_output_last_tick = curr_tick;
    if duration > 0 {
        svcSleepThread(duration as s64);
    }

    nwm_output(nwm_buf, len as usize);

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

#[named]
pub unsafe fn rp_output(packet_buf: *mut u8, packet_size: usize) -> Option<()> {
    let _ = NwmCbLock::lock()?;

    match get_reliable_stream_method() {
        ReliableStreamMethod::None => {
            let nwm_buf = packet_buf.sub(NWM_HDR_SIZE as usize);
            nwm_output(nwm_buf, packet_size);
        }
        ReliableStreamMethod::KCP => {
            let kcp = &mut (*reliable_stream_cb).cb.ikcp;

            while !entries::work_thread::reset_threads() {
                let waitsnd = ikcp_waitsnd(kcp);
                if waitsnd < kcp.snd_wnd as i32 {
                    let ret = ikcp_send(kcp, packet_buf, packet_size as i32);
                    if ret < 0 {
                        // Reset KCP
                        nsDbgPrint!(kcpInputFailed, ret);
                        todo!();
                    }

                    ikcp_update(kcp, iclock());
                    break;
                } else {
                    // Wait
                    let current = iclock();
                    let next = ikcp_check(kcp, current);
                    let diff = (next - current) as u64 * SYSCLOCK_ARM11 as u64 / 1000;

                    let res = svcWaitSynchronization(reliable_stream_cb_evt, diff as s64);
                    if res != 0 && res != RES_TIMEOUT as s32 {
                        nsDbgPrint!(waitForSyncFailed, c_str!("reliable_stream_cb_evt"), res);
                        return None;
                    }
                    ikcp_update(kcp, iclock());
                }
            }
        }
    }

    Some(())
}

unsafe fn iclock64() -> IUINT64 {
    let v = svcGetSystemTick();
    v * 1000 / SYSCLOCK_ARM11 as u64
}

unsafe fn iclock() -> IUINT32 {
    iclock64() as IUINT32
}

#[no_mangle]
#[named]
unsafe extern "C" fn nsControlRecv(fd: c_int) -> c_int {
    let ret = recv(
        fd,
        (*reliable_stream_cb).recv_buf.as_mut_ptr() as *mut _,
        NWM_RECV_SIZE as usize,
        0,
    );

    if ret == 0 {
        nsDbgPrint!(nwmInputNothing);
        return 0;
    } else if ret < 0 {
        nsDbgPrint!(nwmInputFailed, ret as i32, *__errno());
        return -1;
    }

    if !reliable_stream_cb_inited {
        return 0;
    }

    let nwm_lock = NwmCbLock::lock();
    if nwm_lock == None || !reliable_stream_cb_inited {
        return 0;
    }

    match get_reliable_stream_method() {
        ReliableStreamMethod::None => return 0,
        ReliableStreamMethod::KCP => {
            let kcp = &mut (*reliable_stream_cb).cb.ikcp;
            let recv_buf = &mut (*reliable_stream_cb).recv_buf;
            let ret = ikcp_input(kcp, recv_buf.as_ptr(), ret as i32);
            if ret < 0 {
                // Reset KCP
                todo!()
            }
            loop {
                let ret = ikcp_recv(kcp, recv_buf.as_mut_ptr(), recv_buf.len() as i32);
                if ret < 0 {
                    break;
                }
            }
            let _ = svcSignalEvent(reliable_stream_cb_evt);
        }
    }

    0
}

#[derive(PartialEq, Eq)]
struct NwmCbLock();

impl NwmCbLock {
    #[named]
    unsafe fn lock() -> Option<Self> {
        while !entries::work_thread::reset_threads() {
            let res = svcWaitSynchronization(reliable_stream_cb_lock, THREAD_WAIT_NS);

            if res == 0 {
                return Some(Self());
            }
            if res != RES_TIMEOUT as s32 {
                nsDbgPrint!(waitForSyncFailed, c_str!("reliable_stream_cb_sync"), res);
                return None;
            }
        }
        None
    }
}

impl Drop for NwmCbLock {
    fn drop(&mut self) {
        let _ = unsafe { svcReleaseMutex(reliable_stream_cb_lock) };
    }
}
