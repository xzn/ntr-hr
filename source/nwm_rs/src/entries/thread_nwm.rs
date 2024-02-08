use crate::*;

pub extern "C" fn thread_nwm(_: *mut c_void) {
    unsafe { svcExitThread() }
}

#[no_mangle]
#[named]
extern "C" fn rpSendBuffer(cinfo: j_compress_ptr, _: *mut u8_, size: u32_, flag: u32_) {
    unsafe {
        let info = &mut *cinfo;

        let work_index = WorkIndex::init_unchecked(info.user_work_index);
        let thread_id = ThreadId::init_unchecked(info.user_thread_id);

        let ninfo = nwm_infos.get_mut(&work_index).get_mut(&thread_id);
        let dinfo = &mut ninfo.info;

        let mut pos_next = dinfo.pos.add(size as usize);
        if pos_next > ninfo.buf_packet_last {
            pos_next = ninfo.buf_packet_last;
            nsDbgPrint!(sendBufferOverflow);
        }

        info.client_data = pos_next as *mut _;

        AtomicPtr::from_ptr(ptr::addr_of_mut!(dinfo.pos)).store(pos_next, Ordering::Relaxed);
        if flag > 0 {
            AtomicU32::from_ptr(ptr::addr_of_mut!(dinfo.flag)).store(flag, Ordering::Release);
        }

        let res = svcSignalEvent((*syn_handles).nwm_ready);
        if R_FAILED(res) {
            nsDbgPrint!(nwmEventSignalFailed, res);
        }
    }
}
