mod safe_impl;
mod vars;

use crate::*;
pub use vars::*;

pub fn reset_threads() -> bool {
    unsafe { crate::reset_threads.load(Ordering::Relaxed) }
}

pub fn set_reset_threads_ar() {
    unsafe { crate::reset_threads.store(true, Ordering::Relaxed) }
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
