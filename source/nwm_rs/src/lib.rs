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
#![feature(const_fn_floating_point_arithmetic)]
#![feature(maybe_uninit_uninit_array)]
#![feature(maybe_uninit_array_assume_init)]
#![feature(stmt_expr_attributes)]
#![feature(array_chunks)]
#![feature(iter_array_chunks)]
#![feature(ptr_sub_ptr)]
#![allow(static_mut_refs)]

use crate::dbg::*;
use crate::fix::*;
use crate::utils::*;
use crate::vars::*;
use ::libc::*;
use const_default::{const_default, ConstDefault};
use core::ops::*;
use core::panic::PanicInfo;
use core::sync::atomic::*;
use core::{
    cmp,
    marker::{ConstParamTy, PhantomData},
    mem, ptr, slice,
};
use ctru::*;
use function_name::named;

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    core::intrinsics::abort();
}

#[allow(unused)]
mod ctru {
    use crate::ConstDefault;
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

#[macro_use]
mod dbg;
mod entries;
mod fix;
mod jpeg;
mod utils;
mod vars;
