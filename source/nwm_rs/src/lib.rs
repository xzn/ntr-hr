#![no_std]
#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(internal_features)]
#![feature(atomic_from_mut)]
#![feature(core_intrinsics)]
#![feature(const_mut_refs)]

use c_str_macro::c_str;
use core::panic::PanicInfo;

#[macro_use]
extern crate function_name;

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    core::intrinsics::abort();
}

mod wrapper;

use crate::wrapper::{
    nsDbgPrint_t, rpConfig_t, rpPortGamePid_t, rpResetThreads_t, rpSyn_t, u32_,
    SVC_NWM_CMD_GAME_PID_UPDATE, SVC_NWM_CMD_OVERLAY_CALLBACK, SVC_NWM_CMD_PARAMS_UPDATE,
};

macro_rules! nsDbgPrint {
    ($fn:ident $(, $es:expr)*) => {
        nsDbgPrint_t::$fn(
            c_str!("nwm_rs|lib.rs").as_ptr() as *const ::libc::c_char,
            line!() as ::libc::c_int,
            c_str!(function_name!()).as_ptr() as *const ::libc::c_char
            $(, $es)*
        );
    };
}

#[named]
fn handlePortCmd(cmd_id: u32_, norm_params: &[u32_], trans_params: &[u32_]) {
    match cmd_id {
        SVC_NWM_CMD_OVERLAY_CALLBACK => {
            let isTop = *norm_params.get(0).unwrap_or(&u32_::MAX);

            let gamePid = *trans_params.get(1).unwrap_or(&0);
            if isTop > 1 {
                rpPortGamePid_t::set_ar(0);
            } else {
                if rpPortGamePid_t::get() != gamePid {
                    rpPortGamePid_t::set_ar(gamePid);
                }
                let ret = rpSyn_t::signalPortEvent(isTop > 0);
                if ret != 0 {
                    nsDbgPrint!(singalPortFailed, ret);
                }
            }
        }
        SVC_NWM_CMD_PARAMS_UPDATE => {
            if rpConfig_t::set_mar(norm_params) {
                rpResetThreads_t::set_ar(true);
            }
        }
        SVC_NWM_CMD_GAME_PID_UPDATE => {
            let gamePid = *norm_params.get(0).unwrap_or(&0);
            rpConfig_t::gamePid_set_ar(gamePid);
        }
        _ => (),
    }
}
