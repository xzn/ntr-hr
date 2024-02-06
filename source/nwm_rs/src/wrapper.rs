pub use c_str_macro::c_str;
use core::mem::{self, offset_of, size_of, MaybeUninit};
use core::ptr::{self, addr_of_mut};
use core::slice;
use core::sync::atomic::AtomicU32;
use core::sync::atomic::{AtomicI32, Ordering};
pub use function_name::named;

macro_rules! nsDbgPrint {
    ($fn:ident $(, $es:expr)*) => {
        nsDbgPrint_t::$fn(
            c_str!(module_path!()).as_ptr() as *const ::libc::c_char,
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

pub fn create_thread<const T: usize>(
    h: &mut Handle,
    f: ThreadFunc,
    a: u32_,
    t: &mut [u32_; T / mem::size_of::<u32_>()],
    prio: s32,
    core: s32,
) -> ctru_sys::Result
where
    [u32_; T / mem::size_of::<u32_>() - 10]: Sized,
{
    unsafe {
        svcCreateThread(
            h as *mut Handle,
            f,
            a,
            t.as_mut_ptr().add(t.len() - 10),
            prio,
            core,
        )
    }
}

pub fn create_thread_from_pool<const T: usize>(
    h: &mut Handle,
    f: ThreadFunc,
    a: u32_,
    prio: s32,
    core: s32,
) -> ctru_sys::Result
where
    [u32_; T / mem::size_of::<u32_>()]: Sized,
    [u32_; T / mem::size_of::<u32_>() - 10]: Sized,
{
    let s = unsafe { plgRequestMemory(T as u32_) };
    if s > 0 {
        let t = unsafe {
            mem::transmute::<*mut u32_, &mut [u32_; T / mem::size_of::<u32_>()]>(s as *mut u32_)
        };
        create_thread(h, f, a, t, prio, core)
    } else {
        -1
    }
}

#[allow(unused)]
impl svcThread_t {
    pub fn create<const T: usize>(
        f: ThreadFunc,
        a: u32_,
        t: &mut [u32_; T / mem::size_of::<u32_>()],
        prio: s32,
        core: s32,
    ) -> Option<Self>
    where
        [u32_; T / mem::size_of::<u32_>() - 10]: Sized,
    {
        let mut h = MaybeUninit::uninit();
        let res = unsafe { create_thread(&mut *h.as_mut_ptr(), f, a, t, prio, core) };

        if R_SUCCEEDED(res) {
            let h = unsafe { h.assume_init() };
            Some(Self { h })
        } else {
            None
        }
    }

    pub fn create_from_pool<const T: usize>(
        f: ThreadFunc,
        a: u32_,
        prio: s32,
        core: s32,
    ) -> Option<Self>
    where
        [u32_; T / mem::size_of::<u32_>()]: Sized,
        [u32_; T / mem::size_of::<u32_>() - 10]: Sized,
    {
        let mut h = MaybeUninit::uninit();
        let res = unsafe { create_thread_from_pool::<T>(&mut *h.as_mut_ptr(), f, a, prio, core) };

        if R_SUCCEEDED(res) {
            let h = unsafe { h.assume_init() };
            Some(Self { h })
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

mod handlePortCmd;
pub use handlePortCmd::handlePortCmd_threadVars_t;

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

mod startUp;
pub use startUp::startUp_threadVars_t;

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

pub struct nsDbgPrint_t {
    _z: (),
}

impl nsDbgPrint_t {
    nsDbgPrint_fn!(singalPortFailed, "Signal port event failed: %08x\n", ret: s32);

    nsDbgPrint_fn!(openProcessFailed, "openProcess failed: %08x\n", ret: s32);

    nsDbgPrint_fn!(copyRemoteMemoryFailed, "copyRemoteMemory failed: %08x\n", ret: s32);
}
