use crate::*;

pub unsafe fn reset_threads() -> bool {
    AtomicBool::from_ptr(ptr::addr_of_mut!(crate::reset_threads)).load(Ordering::Relaxed)
}

pub unsafe fn no_skip_next_frames() {
    todo!()
}

pub unsafe fn work_thread_loop(t: ThreadId) {
    let w = WorkIndex::init();

    while !reset_threads() {
        todo!()
    }
}

#[named]
#[no_mangle]
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
            return ptr::null_mut();
        }

        info.alloc.stats.offset += total_size;
        info.alloc.stats.remaining -= total_size;

        return ret as *mut _;
    }
}

#[no_mangle]
extern "C" fn rpFree(_: j_common_ptr, _: *mut c_void) {}
