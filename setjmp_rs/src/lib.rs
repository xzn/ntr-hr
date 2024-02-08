//!
//! Expose C standard library functions in ``<setjmp.h>`` to rust.
//!
//! **WARNING: this crate is experimental and even careful use is
//! likely undefined behavior.**
//!
//! See
//! [setjmp(3)](http://man7.org/linux/man-pages/man3/setjmp.3.html)
//! and [RFC #2625](https://github.com/rust-lang/rfcs/issues/2625).

#![no_std]
#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![feature(ffi_returns_twice)]

mod sys {
    use libc::*;

    // definitions for jmp_buf and sigjmp_buf types
    include!(concat!(env!("OUT_DIR"), "/jmpbuf.rs"));

    // declarations for setjmp, sigsetjmp, longjmp, and siglongjmp
    // including proper link name attributes for this platform's libc
    include!(concat!(env!("OUT_DIR"), "/decls.rs"));
}

#[cfg(test)]
mod tests;

pub use crate::sys::{jmp_buf, setjmp};
