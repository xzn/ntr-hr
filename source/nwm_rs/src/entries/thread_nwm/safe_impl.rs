use super::*;

pub fn try_send_next_buffer(v: ThreadVars, work_flush: bool) -> bool {
    try_send_next_buffer_may_skip(v, work_flush, false)
}

fn try_send_next_buffer_may_skip(v: ThreadVars, work_flush: bool, mut may_skip: bool) -> bool {
    let work_index = *v.work_index();
    let mut thread_id = *v.thread_id();

    loop {
        if !v.sync(work_flush) {
            return false;
        }

        let ninfo = v.nwm_info();
        let dinfo = &ninfo.info;

        let (filled, pos, flag) = data_buf_filled(dinfo);
        if !filled {
            return may_skip;
        }

        if !send_next_buffer_delay(&v, work_flush, pos, flag) {
            return false;
        }

        if work_flush {
            if work_index != *v.work_index() {
                return true;
            }

            if thread_id != *v.thread_id() {
                thread_id = *v.thread_id();
            }

            may_skip = false;
            continue;
        }

        return true;
    }
}

fn send_next_buffer_delay(v: &ThreadVars, work_flush: bool, pos: *mut u8, flag: u32) -> bool {
    unsafe {
        let next_tick = svcGetSystemTick() as u32_;
        let tick_diff = next_tick as s32 - *v.last_send_tick() as s32;

        if tick_diff < v.min_send_interval_tick() as s32 {
            if work_flush {
                let sleep_value = (v.min_send_interval_tick() as s32 - tick_diff) as u64_
                    * 1000_000_000
                    / SYSCLOCK_ARM11 as u64_;
                svcSleepThread(sleep_value as s64);
                if !send_next_buffer(&v, svcGetSystemTick() as u32_, pos, flag) {
                    return false;
                }
            }
        } else {
            if !send_next_buffer(&v, next_tick, pos, flag) {
                return false;
            }
        }
        true
    }
}

unsafe fn send_next_buffer(v: &ThreadVars, tick: u32_, pos: *mut u8_, flag: u32_) -> bool {
    let thread_id = *v.thread_id();
    let core_count = crate::entries::work_thread::get_core_count_in_use();

    let winfo = v.nwm_infos();
    let ninfo = winfo.get_mut(&thread_id);
    let dinfo = &ninfo.info;

    let send_pos = dinfo.send_pos;
    let data_buf = send_pos;
    let packet_buf = data_buf.sub(DATA_HDR_SIZE as usize);

    let size = pos.offset_from(send_pos) as u32_;
    let packet_data_size = get_packet_data_size() as u32_;
    let size = cmp::min(size, packet_data_size);

    let thread_emptied = send_pos.add(size as usize) == pos;
    let thread_done = thread_emptied && flag > 0;

    if size < packet_data_size && !thread_done {
        return false;
    }

    let mut thread_end_id = thread_id;
    let mut end_size = size;

    let mut thread_end_done = thread_done;

    if thread_done {
        thread_end_id.next_wrapped_n(&core_count);
    }

    let mut total_size = size;

    if thread_done && thread_end_id.get() != 0 {
        loop {
            let ninfo = winfo.get_mut(&thread_end_id);
            let dinfo = &ninfo.info;

            let (filled, pos, flag) = data_buf_filled(dinfo);
            if !filled {
                return false;
            }

            let data_buf_end = data_buf.add(total_size as usize);

            let send_pos = dinfo.send_pos;

            let remaining_size = packet_data_size - total_size;

            let size = pos.offset_from(send_pos) as u32_;
            let size = cmp::min(size, remaining_size);

            let thread_emptied = send_pos.add(size as usize) == pos;
            let thread_done = thread_emptied && flag > 0;

            if size < remaining_size && !thread_done {
                return false;
            }

            ptr::copy_nonoverlapping(send_pos, data_buf_end, size as usize);
            total_size += size;

            end_size = size;

            thread_end_done = thread_done;
            if thread_done {
                thread_end_id.next_wrapped_n(&core_count);
                if thread_end_id.get() == 0 {
                    break;
                }
                continue;
            }
            break;
        }
    }

    let data_buf_hdr = v.data_buf_hdr();
    ptr::copy(
        data_buf_hdr.as_ptr(),
        packet_buf as *mut u8_,
        DATA_HDR_SIZE as usize,
    );
    if thread_end_done && thread_end_id.get() == 0 {
        *packet_buf.add(1) |= flag as u8_;
    }
    *data_buf_hdr.get_unchecked_mut(3) += 1;

    if rp_output(packet_buf, (total_size + DATA_HDR_SIZE) as usize) == None {
        return false;
    }
    *v.last_send_tick() = tick;

    if !thread_end_done {
        let send_pos = &mut winfo.get_mut(&thread_end_id).info.send_pos;
        *send_pos = (*send_pos).add(end_size as usize);
    }

    if thread_done {
        *v.thread_id() = thread_end_id;

        if v.thread_id().get() == 0 {
            v.release();
        }
    }

    true
}

fn data_buf_filled(dinfo: &DataBufInfo) -> (bool, *mut u8_, u32_) {
    let flag = dinfo.flag.load(Ordering::Acquire);
    let pos = dinfo.pos.load(Ordering::Relaxed);
    (dinfo.send_pos < pos || flag > 0, pos, flag)
}
