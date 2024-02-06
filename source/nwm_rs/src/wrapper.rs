#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(internal_features)]

use c_str_macro::c_str;
use core::mem::{self, offset_of, size_of, MaybeUninit};
use core::ptr::{self, addr_of_mut};
use core::slice;
use core::sync::atomic::AtomicU32;
use core::sync::atomic::{AtomicI32, Ordering};
use function_name::named;

macro_rules! nsDbgPrint {
    ($fn:ident $(, $es:expr)*) => {
        nsDbgPrint_t::$fn(
            c_str!("nwm_rs|wrapper.rs").as_ptr() as *const ::libc::c_char,
            line!() as ::libc::c_int,
            c_str!(function_name!()).as_ptr() as *const ::libc::c_char
            $(, $es)*
        );
    };
}

#[allow(unused)]
mod ctru_sys {
    include!(concat!(env!("OUT_DIR"), "/bindings.rs"));
}

pub use crate::result::*;
pub use ctru_sys::*;

pub struct svcProcess_t {
    h: Handle,
}

impl svcProcess_t {
    #[named]
    pub fn open(pid: u32_) -> Option<Self> {
        let mut h = MaybeUninit::uninit();
        let res = unsafe { svcOpenProcess(h.as_mut_ptr(), pid) };

        if R_SUCCEEDED(res) {
            let h = unsafe { h.assume_init() };
            Some(Self { h })
        } else {
            nsDbgPrint!(openProcessFailed, res);
            None
        }
    }
}

impl Drop for svcProcess_t {
    fn drop(&mut self) {
        unsafe {
            let _ = svcCloseHandle(self.h);
        }
    }
}

pub struct svcThread_t {
    h: Handle,
}

impl svcThread_t {
    pub fn create(f: ThreadFunc, a: u32_, t: &mut [u32_], prio: s32, core: s32) -> Option<Self> {
        let mut h = MaybeUninit::uninit();
        let res = unsafe {
            svcCreateThread(
                h.as_mut_ptr(),
                f,
                a,
                t.as_mut_ptr().add(t.len() - 10),
                prio,
                core,
            )
        };

        if R_SUCCEEDED(res) {
            let h = unsafe { h.assume_init() };
            Some(Self { h })
        } else {
            None
        }
    }

    pub fn create_from_pool(f: ThreadFunc, a: u32_, t: u32_, prio: s32, core: s32) -> Option<Self> {
        let s = unsafe { plgRequestMemory(t) };
        if s > 0 {
            let t = unsafe {
                slice::from_raw_parts_mut(s as *mut u32_, t as usize / mem::size_of::<u32_>())
            };
            Self::create(f, a, t, prio, core)
        } else {
            None
        }
    }
}

impl Drop for svcThread_t {
    fn drop(&mut self) {
        unsafe {
            let _ = svcCloseHandle(self.h);
        }
    }
}

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

    fn set_mar_array(v: &[u32_; rpConfig_u32_count]) {
        let v = v.as_ptr() as *const RP_CONFIG;
        Self::set_mar_ref(&unsafe { *v } as &RP_CONFIG);
    }

    pub fn set_mar(v: &[u32_]) -> bool {
        let ret = v.len() >= rpConfig_u32_count;
        if ret {
            Self::set_mar_array(unsafe {
                mem::transmute::<*const u32_, &[u32_; rpConfig_u32_count]>(v.as_ptr())
            });
        }
        ret
    }

    pub fn gamePid_set_ar(v: u32_) {
        let pid_a = unsafe { AtomicU32::from_ptr(addr_of_mut!((*rpConfig).gamePid)) };
        pid_a.store(v, Ordering::Relaxed);
    }

    #[named]
    pub fn dstAddr_set_ar_update(mut v: u32_) {
        let daddr_a = unsafe { AtomicU32::from_ptr(addr_of_mut!((*rpConfig).dstAddr)) };
        daddr_a.store(v, Ordering::Relaxed);

        if let Some(p) = svcProcess_t::open(ntrConfig_t::homeMenuPid_get()) {
            let res = unsafe {
                copyRemoteMemory(
                    p.h,
                    ptr::addr_of_mut!((*rpConfig).dstAddr) as *mut ::libc::c_void,
                    CUR_PROCESS_HANDLE,
                    ptr::addr_of_mut!(v) as *mut ::libc::c_void,
                    mem::size_of::<u32_>() as u32_,
                ) as i32
            };
            if R_FAILED(res) {
                nsDbgPrint!(copyRemoteMemoryFailed, res);
            }
        }
    }

    pub fn dstAddr_get_ar() -> u32_ {
        let daddr_a = unsafe { AtomicU32::from_ptr(addr_of_mut!((*rpConfig).dstAddr)) };
        daddr_a.load(Ordering::Relaxed)
    }
}

const ntrConfig: *mut NTR_CONFIG =
    (NS_CONFIG_ADDR as usize + offset_of!(NS_CONFIG, ntrConfig)) as *mut NTR_CONFIG;

pub struct ntrConfig_t;

impl ntrConfig_t {
    pub fn homeMenuPid_get() -> u32_ {
        unsafe { (*ntrConfig).HomeMenuPid }
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

pub struct nwmHdr_t<'a> {
    buf: &'a [u8_; RP_NWM_HDR_SIZE as usize],
}

impl nwmHdr_t<'_> {
    pub fn protocol(&self) -> u8_ {
        self.buf[0x17 + 0x8]
    }

    pub fn srcPort(&self) -> u16_ {
        unsafe { *(&self.buf[0x22 + 0x8] as *const u8_ as *const u16_) }
    }

    pub fn dstPort(&self) -> u16_ {
        unsafe { *(&self.buf[0x22 + 0xa] as *const u8_ as *const u16_) }
    }

    pub fn srcAddr(&self) -> u32_ {
        unsafe { *(&self.buf[0x1a + 0x8] as *const u8_ as *const u32_) }
    }

    pub fn dstAddr(&self) -> u32_ {
        unsafe { *(&self.buf[0x1e + 0x8] as *const u8_ as *const u32_) }
    }
}

#[no_mangle]
pub extern "C" fn rpStartup(buf: *const u8_) {
    let buf = unsafe { mem::transmute::<*const u8_, &[u8_; RP_NWM_HDR_SIZE as usize]>(buf) };
    let buf = nwmHdr_t { buf };
    super::startUp(buf)
}

pub struct rpInited_t;

impl rpInited_t {
    pub fn get() -> bool {
        unsafe { rpInited > 0 }
    }

    pub fn set() {
        unsafe { rpInited = 1 }
    }
}

pub struct rpNwmHdr_t;

impl rpNwmHdr_t {
    pub fn set(hdr: &nwmHdr_t) {
        unsafe { ptr::copy_nonoverlapping(hdr.buf.as_ptr(), rpNwmHdr.as_mut_ptr(), hdr.buf.len()) }
    }
}

pub struct rpSrcAddr_t;

impl rpSrcAddr_t {
    pub fn get() -> u32_ {
        unsafe { rpSrcAddr }
    }

    pub fn set(v: u32_) {
        unsafe { rpSrcAddr = v }
    }
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

    nsDbgPrint_fn!(openProcessFailed, "openProcess failed: %08x\n", ret: s32);

    nsDbgPrint_fn!(copyRemoteMemoryFailed, "copyRemoteMemory failed: %08x\n", ret: s32);
}
