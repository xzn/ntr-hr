use super::*;

extern "C" {
    #[ffi_returns_twice]
    pub fn setjmp(env: *mut jmp_buf) -> c_int;
    #[allow(unused)]
    pub fn longjmp(env: *mut jmp_buf, val: c_int) -> !;
}
