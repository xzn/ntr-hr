#![no_std]
#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(internal_features)]
#![feature(atomic_from_mut)]
#![feature(core_intrinsics)]
#![feature(const_mut_refs)]

use core::mem::{offset_of, size_of};
use core::ptr::addr_of_mut;
use core::sync::atomic::{AtomicI32, Ordering};
use core::{panic::PanicInfo, sync::atomic::AtomicU32};

#[macro_use]
extern crate function_name;
use c_str_macro::c_str;

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    core::intrinsics::abort();
}

include!(concat!(env!("OUT_DIR"), "/bindings.rs"));

const rpConfig: *mut RP_CONFIG =
    (NS_CONFIG_ADDR as usize + offset_of!(NS_CONFIG, rpConfig)) as *mut RP_CONFIG;

macro_rules! nsDbgPrint {
    ($e:expr $(, $es:expr)*) => {
        nsDbgPrintVerbose(
            c_str!(file!()).as_ptr() as *const ::libc::c_char,
            line!() as ::libc::c_int,
            c_str!(function_name!()).as_ptr() as *const ::libc::c_char,
            c_str!($e).as_ptr() as *const ::libc::c_char
            $(, $es)*
        );
    };
}

#[no_mangle]
#[named]
pub extern "C" fn handlePortCmd(
    cmd_id: u32_,
    norm_param_count: u32_,
    trans_param_size: u32_,
    cmd_buf1: *mut u32_,
) {
    let rpGamePid = unsafe { AtomicU32::from_ptr(addr_of_mut!(rpPortGamePid)) };
    let rpConfigGamePid = unsafe { AtomicU32::from_ptr(addr_of_mut!((*rpConfig).gamePid)) };

    match cmd_id {
        SVC_NWM_CMD_OVERLAY_CALLBACK => {
            let isTop = if norm_param_count >= 1 {
                unsafe { *cmd_buf1 }
            } else {
                u32_::MAX
            };
            let gamePid = if trans_param_size >= 2 {
                unsafe { *cmd_buf1.add(norm_param_count as usize + 1) }
            } else {
                0
            };
            if isTop > 1 {
                rpGamePid.store(0, Ordering::Relaxed)
            } else {
                if unsafe { rpPortGamePid } != gamePid {
                    rpGamePid.store(gamePid, Ordering::Relaxed);
                }
            }

            let ret = unsafe { svcSignalEvent(*(*rp_syn).portEvent.get_unchecked(isTop as usize)) };
            if ret != 0 {
                unsafe {
                    nsDbgPrint!("Signal port event failed: %08x\n", ret);
                }
            }
        }
        SVC_NWM_CMD_PARAMS_UPDATE => {
            if norm_param_count as usize * size_of::<u32_>() >= size_of::<RP_CONFIG>() {
                for i in 0..(size_of::<RP_CONFIG>() / size_of::<u32_>()) {
                    let f = unsafe { AtomicU32::from_ptr((rpConfig as *mut u32_).add(i)) };
                    let p = unsafe { *cmd_buf1.add(i) };
                    f.store(p, Ordering::Relaxed);
                }

                let resetThreads = unsafe { AtomicI32::from_ptr(addr_of_mut!(rpResetThreads)) };
                resetThreads.store(1, Ordering::Relaxed);
            }
        }
        SVC_NWM_CMD_GAME_PID_UPDATE => {
            let gamePid = if norm_param_count >= 1 {
                unsafe { *cmd_buf1 }
            } else {
                0
            };
            rpConfigGamePid.store(gamePid, Ordering::Relaxed);
        }
        _ => (),
    }
}
