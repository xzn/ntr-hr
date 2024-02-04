#![no_std]
#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![feature(core_intrinsics)]
#![allow(internal_features)]

use core::panic::PanicInfo;

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    core::intrinsics::abort();
}

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

pub fn add(left: usize, right: usize) -> usize {
    left + right
}
