use crate::*;

pub extern "C" fn thread_screen(_: *mut c_void) {
    unsafe { svcExitThread() }
}
