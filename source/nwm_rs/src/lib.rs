#![no_std]
#![allow(internal_features)]
#![allow(incomplete_features)]
#![feature(core_intrinsics)]
#![feature(const_trait_impl)]
#![feature(generic_const_exprs)]
#![feature(generic_const_items)]
#![feature(adt_const_params)]
#![feature(trivial_bounds)]
#![cfg_attr(not(feature = "o3ds"), feature(iter_array_chunks))]
#![feature(array_ptr_get)]
#![allow(static_mut_refs)]

use ::libc::*;
use const_default::{ConstDefault, const_default};
use core::ops::*;
use core::panic::PanicInfo;
use core::sync::atomic::*;
use core::{
    cmp,
    marker::{ConstParamTy, PhantomData},
    mem, ptr, slice,
};
use ctru::*;
use dbg::*;
use fix::*;
use function_name::named;
#[cfg(not(feature = "o3ds"))]
use oorandom::Rand32;
use utils::*;
use vars::*;

#[allow(unused)]
#[allow(clippy::all)]
#[allow(non_upper_case_globals)]
#[allow(non_camel_case_types)]
#[allow(non_snake_case)]
mod ctru {
    use crate::ConstDefault;
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

#[macro_use]
mod dbg;
#[macro_use]
mod vars;
mod encoder;
mod entries;
mod fix;
mod utils;

#[panic_handler]
fn panic(panic_info: &PanicInfo) -> ! {
    unsafe {
        if let Some(location) = panic_info.location() {
            panicHandle(
                location.file().as_ptr() as *mut _,
                location.file().len() as i32,
                location.line() as i32,
                location.column() as i32,
            )
        } else {
            showMsgRaw(c_str!("Panic!"));
        }

        loop {
            sleep_thread(THREAD_WAIT_NS);
        }
    }
}
