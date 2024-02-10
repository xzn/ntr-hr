use crate::*;

pub extern "C" fn thread_nwm(_: *mut c_void) {
    unsafe {
        while !crate::entries::work_thread::reset_threads() {
            if try_send_next_buffer(true) {
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

unsafe fn try_send_next_buffer(work_flush: bool) -> bool {
    try_send_next_buffer_may_skip(work_flush, false)
}

#[named]
unsafe fn try_send_next_buffer_may_skip(work_flush: bool, mut may_skip: bool) -> bool {
    let work_index = nwm_work_index;
    let mut thread_id = nwm_thread_id;

    loop {
        if *nwm_need_syn.get(&work_index) {
            let res = svcWaitSynchronization(
                (*syn_handles).works.get(&work_index).nwm_ready,
                if work_flush { THREAD_WAIT_NS } else { 0 },
            );
            if res != 0 {
                if res != RES_TIMEOUT as s32 {
                    nsDbgPrint!(waitForSyncFailed, c_str!("nwm_ready"), res);
                }
                return false;
            }
            *nwm_need_syn.get_mut(&work_index) = false;
        }

        let ninfo = nwm_infos.get_mut(&work_index).get_mut(&thread_id);
        let dinfo = &mut ninfo.info;

        let (filled, pos, flag) = data_buf_filled(dinfo);
        if !filled {
            return may_skip;
        }

        let next_tick = svcGetSystemTick() as u32_;
        let tick_diff = next_tick as s32 - last_send_tick as s32;

        if tick_diff < min_send_interval_tick as s32 {
            if work_flush {
                let sleep_value = (min_send_interval_tick as s32 - tick_diff) as u64_
                    * 1000_000_000
                    / SYSCLOCK_ARM11 as u64_;
                svcSleepThread(sleep_value as s64);
                if !send_next_buffer(svcGetSystemTick() as u32_, pos, flag) {
                    return false;
                }
            }
        } else {
            if !send_next_buffer(next_tick, pos, flag) {
                return false;
            }
        }

        if work_flush {
            if work_index != nwm_work_index {
                return true;
            }

            if thread_id != nwm_thread_id {
                thread_id = nwm_thread_id;
            }

            may_skip = false;
            continue;
        }

        return true;
    }
}

#[named]
unsafe fn send_next_buffer(tick: u32_, pos: *mut u8_, flag: u32_) -> bool {
    let work_index = nwm_work_index;
    let thread_id = nwm_thread_id;

    let mut nwm_buf_tmp: [mem::MaybeUninit<u8_>; PACKET_SIZE + NWM_HDR_SIZE] =
        mem::MaybeUninit::<u8_>::uninit_array();

    let winfo = nwm_infos.get_mut(&work_index);
    let ninfo = winfo.get_mut(&thread_id);
    let dinfo = &mut ninfo.info;

    let send_pos = dinfo.send_pos;
    let mut data_buf = send_pos;
    let mut packet_buf = data_buf.sub(DATA_HDR_SIZE);
    let mut nwm_buf = packet_buf.sub(NWM_HDR_SIZE);

    let size = pos.offset_from(send_pos) as u32_;
    let size = cmp::min(size, PACKET_DATA_SIZE as u32_);

    let thread_emptied = send_pos.add(size as usize) == pos;
    let thread_done = thread_emptied && flag > 0;

    if size < PACKET_DATA_SIZE as u32_ && !thread_done {
        return false;
    }

    let mut thread_end_id = thread_id;
    let mut sizes: [mem::MaybeUninit<u32_>; RP_CORE_COUNT_MAX as usize] =
        mem::MaybeUninit::<u32_>::uninit_array();

    sizes
        .get_unchecked_mut(thread_id.get() as usize)
        .write(size);

    let mut thread_end_done = thread_done;

    if thread_done {
        thread_end_id.next_wrapped_n(&core_count_in_use);
    }

    let mut total_size = size;

    if thread_done && thread_end_id.get() != 0 {
        nwm_buf = nwm_buf_tmp.as_mut_ptr() as *mut _;
        packet_buf = nwm_buf.add(NWM_HDR_SIZE);
        data_buf = packet_buf.add(DATA_HDR_SIZE);

        ptr::copy_nonoverlapping(send_pos, data_buf, size as usize);
        let mut remaining_size = (PACKET_DATA_SIZE - size as usize) as u32_;

        loop {
            let ninfo = winfo.get_mut(&thread_end_id);
            let dinfo = &mut ninfo.info;

            let (filled, pos, flag) = data_buf_filled(dinfo);
            if !filled {
                return false;
            }

            let send_pos = dinfo.send_pos;
            let size = pos.offset_from(send_pos) as u32_;
            let size = cmp::min(size, remaining_size);

            let thread_emptied = send_pos.add(size as usize) == pos;
            let thread_done = thread_emptied && flag > 0;

            ptr::copy_nonoverlapping(send_pos, data_buf.add(total_size as usize), size as usize);
            total_size += size;
            remaining_size -= size;

            sizes
                .get_unchecked_mut(thread_end_id.get() as usize)
                .write(size);

            thread_end_done = thread_done;
            if thread_done {
                thread_end_id.next_wrapped_n(&core_count_in_use);
                if thread_end_id.get() == 0 {
                    break;
                }
            }
            if remaining_size == 0 {
                break;
            }
        }
    }

    ptr::copy_nonoverlapping(current_nwm_hdr.as_mut_ptr(), nwm_buf, NWM_HDR_SIZE);
    let packet_len = init_udp_packet(nwm_buf, total_size + DATA_HDR_SIZE as u32_);
    let data_buf_hdr = data_buf_hdrs.get_mut(&work_index);
    ptr::copy(data_buf_hdr.as_ptr(), packet_buf as *mut u8_, DATA_HDR_SIZE);
    if thread_end_done && thread_end_id.get() == 0 {
        *packet_buf.add(1) |= flag as u8_;
    }
    *data_buf_hdr.get_unchecked_mut(3) += 1;

    nwmSendPacket.unwrap_unchecked()(nwm_buf, packet_len);
    last_send_tick = tick;

    let mut update_send_pos = |j| {
        let send_pos = &mut winfo.get_mut(&j).info.send_pos;
        *send_pos = (*send_pos).add(sizes.get_unchecked(j.get() as usize).assume_init() as usize);
    };

    for j in thread_id.from_wrapped_to(&thread_end_id, &core_count_in_use) {
        update_send_pos(j);
    }
    if !thread_end_done {
        update_send_pos(thread_end_id);
    }

    if thread_done {
        for j in thread_id.from_wrapped_to(&thread_end_id, &core_count_in_use) {
            AtomicU32::from_mut(&mut winfo.get_mut(&j).info.flag).store(0, Ordering::Relaxed);
        }
        nwm_thread_id = thread_end_id;

        if nwm_thread_id.get() == 0 {
            let mut count = mem::MaybeUninit::uninit();
            let res = svcReleaseSemaphore(
                count.as_mut_ptr(),
                (*syn_handles).works.get(&work_index).nwm_done,
                1,
            );
            if res != 0 {
                nsDbgPrint!(releaseSemaphoreFailed, c_str!("nwm_done"), res);
            }

            *nwm_need_syn.get_mut(&work_index) = true;
            nwm_work_index.next_wrapped();
        }
    }

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

unsafe fn data_buf_filled(dinfo: &mut DataBufInfo) -> (bool, *mut u8_, u32_) {
    let flag = AtomicU32::from_mut(&mut dinfo.flag).load(Ordering::Acquire);
    let pos = AtomicPtr::from_mut(&mut dinfo.pos).load(Ordering::Relaxed);
    (dinfo.send_pos < pos || flag > 0, pos, flag)
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
        if res != 0 {
            nsDbgPrint!(nwmEventSignalFailed, res);
        }
    }
}
