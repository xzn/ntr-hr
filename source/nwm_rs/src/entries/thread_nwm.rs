use crate::*;

pub extern "C" fn thread_nwm(_: *mut c_void) {
    unsafe { svcExitThread() }
}
