#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(internal_features)]

use c_str_macro::c_str;
use core::mem::{self, offset_of, size_of};
use core::ptr::addr_of_mut;
use core::slice;
use core::sync::atomic::AtomicU32;
use core::sync::atomic::{AtomicI32, Ordering};

#[allow(unused)]
mod ctru_sys {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub use ctru_sys::*;

const rpConfig: *mut RP_CONFIG =
    (NS_CONFIG_ADDR as usize + offset_of!(NS_CONFIG, rpConfig)) as *mut RP_CONFIG;

const rpConfig_u32_count: usize = size_of::<RP_CONFIG>() / size_of::<u32_>();

pub struct rpConfig_t;

impl rpConfig_t {
    fn set_mar_ref(v: &RP_CONFIG) {
        for i in 0..rpConfig_u32_count {
            let f = unsafe { AtomicU32::from_ptr((rpConfig as *mut u32_).add(i)) };
            let p = unsafe { *(v as *const RP_CONFIG as *const u32_).add(i) };
            f.store(p, Ordering::Relaxed);
        }
    }

    fn set_mar_a(v: &[u32_; rpConfig_u32_count]) {
        let v = v.as_ptr() as *const RP_CONFIG;
        Self::set_mar_ref(&unsafe { *v } as &RP_CONFIG);
    }

    pub fn set_mar(v: &[u32_]) -> bool {
        let ret = v.len() >= rpConfig_u32_count;
        if ret {
            Self::set_mar_a(unsafe {
                mem::transmute::<*const u32_, &[u32_; rpConfig_u32_count]>(v.as_ptr())
            });
        }
        ret
    }

    pub fn gamePid_set_ar(v: u32_) {
        let pid_a = unsafe { AtomicU32::from_ptr(addr_of_mut!((*rpConfig).gamePid)) };
        pid_a.store(v, Ordering::Relaxed);
    }
}

pub struct rpPortGamePid_t;

impl rpPortGamePid_t {
    pub fn get() -> u32_ {
        unsafe { rpPortGamePid }
    }

    pub fn set_ar(v: u32_) {
        let pid_a = unsafe { AtomicU32::from_ptr(addr_of_mut!(rpPortGamePid)) };
        pid_a.store(v, Ordering::Relaxed);
    }
}

pub struct rpResetThreads_t;

impl rpResetThreads_t {
    pub fn set_ar(v: bool) {
        let reset_a = unsafe { AtomicI32::from_ptr(addr_of_mut!(rpResetThreads)) };
        reset_a.store(v as s32, Ordering::Relaxed);
    }
}

pub struct rpSyn_t;

impl rpSyn_t {
    pub fn signalPortEvent(isTop: bool) -> Result {
        unsafe { svcSignalEvent(*(*rp_syn).portEvent.get_unchecked(isTop as usize)) }
    }
}

#[no_mangle]
pub extern "C" fn handlePortCmd(
    cmd_id: u32_,
    norm_param_count: u32_,
    trans_param_size: u32_,
    cmd_buf1: *const u32_,
) {
    super::handlePortCmd(
        cmd_id,
        unsafe { slice::from_raw_parts(cmd_buf1, norm_param_count as usize) },
        unsafe {
            slice::from_raw_parts(
                cmd_buf1.add(norm_param_count as usize),
                trans_param_size as usize,
            )
        },
    )
}

macro_rules! nsDbgPrint_fn {
    ($fn:ident, $fmt:expr $(, $vn:ident: $ty:ty)*) => {
        pub fn $fn(
            file_name: *const ::libc::c_char,
            line_number: ::libc::c_int,
            func_name: *const ::libc::c_char
            $(, $vn: $ty)*
        ) {
            unsafe {
                nsDbgPrintVerbose(
                    file_name,
                    line_number,
                    func_name,
                    c_str!($fmt).as_ptr() as *const ::libc::c_char
                    $(, $vn)*
                );
            }
        }
    };
}

pub struct nsDbgPrint_t;

impl nsDbgPrint_t {
    nsDbgPrint_fn!(singalPortFailed, "Signal port event failed: %08x\n", ret: s32);
}
