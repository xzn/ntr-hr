#![no_std]
#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(internal_features)]
#![allow(incomplete_features)]
#![feature(atomic_from_mut)]
#![feature(core_intrinsics)]
#![feature(const_mut_refs)]
#![feature(const_trait_impl)]
#![feature(generic_const_exprs)]
#![feature(generic_const_items)]
#![feature(adt_const_params)]
#![feature(inherent_associated_types)]
#![feature(trivial_bounds)]

use crate::dbg::*;
use crate::result::*;
use crate::utils::*;
use crate::vars::*;
use ::libc::*;
use const_default::ConstDefault;
use const_default_union_derive::ConstDefaultUnion;
use core::panic::PanicInfo;
use core::sync::atomic::*;
use core::{mem, ptr};
use ctru::*;
use function_name::named;

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    core::intrinsics::abort();
}

#[allow(unused)]
mod ctru {
    use crate::ConstDefault;
    use crate::ConstDefaultUnion;
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

#[macro_use]
mod dbg;
mod entries;
mod result;
mod utils;
mod vars;
