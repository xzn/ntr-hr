use crate::*;

pub extern "C" fn thread_aux(p: *mut c_void) {
    unsafe {
        let t = crate::ThreadId::init_unchecked(p as u32_);
        crate::entries::work_thread::work_thread_loop(t);
        svcExitThread()
    }
}
