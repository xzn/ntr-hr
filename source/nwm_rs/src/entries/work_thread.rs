use crate::*;

unsafe fn reset_threads() -> bool {
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
