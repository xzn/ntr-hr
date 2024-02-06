use super::*;

mod startUp_threadVars_m {
    use super::*;

    pub struct rpInited_t;

    impl rpInited_t {
        pub fn get(&self) -> bool {
            unsafe { rpInited > 0 }
        }

        pub fn set(&self) {
            unsafe { rpInited = 1 }
        }
    }

    pub struct rpNwmHdr_t;

    impl rpNwmHdr_t {
        pub fn set(&self, hdr: &nwmHdr_t) {
            unsafe {
                ptr::copy_nonoverlapping(hdr.buf.as_ptr(), rpNwmHdr.as_mut_ptr(), hdr.buf.len())
            }
        }
    }

    pub struct rpSrcAddr_t;

    impl rpSrcAddr_t {
        pub fn get(&self) -> u32_ {
            unsafe { rpSrcAddr }
        }

        pub fn set(&self, v: u32_) {
            unsafe { rpSrcAddr = v }
        }
    }

    pub struct hThreadMain_t;

    impl hThreadMain_t {
        pub fn get_mut_ref(&self) -> &'static mut Handle {
            unsafe { &mut *ptr::addr_of_mut!(hThreadMain) }
        }
    }

    pub struct rpConfig_t {
        ntr: ntrConfig_t,
    }

    impl rpConfig_t {
        pub fn new() -> Self {
            rpConfig_t {
                ntr: ntrConfig_t {},
            }
        }

        #[named]
        pub fn dstAddr_set_ar_update(&self, mut v: u32_) {
            let daddr_a = unsafe { AtomicU32::from_ptr(addr_of_mut!((*rpConfig).dstAddr)) };
            daddr_a.store(v, Ordering::Relaxed);

            if let Some(p) = svcProcess_t::open(self.ntr.homeMenuPid_get()) {
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

        pub fn dstAddr_get_ar(&self) -> u32_ {
            let daddr_a = unsafe { AtomicU32::from_ptr(addr_of_mut!((*rpConfig).dstAddr)) };
            daddr_a.load(Ordering::Relaxed)
        }
    }

    const ntrConfig: *mut NTR_CONFIG =
        (NS_CONFIG_ADDR as usize + offset_of!(NS_CONFIG, ntrConfig)) as *mut NTR_CONFIG;

    pub struct ntrConfig_t;

    impl ntrConfig_t {
        pub fn homeMenuPid_get(&self) -> u32_ {
            unsafe { (*ntrConfig).HomeMenuPid }
        }
    }
}

use startUp_threadVars_m::*;

pub struct startUp_threadVars_t {
    _z: (),
}

impl startUp_threadVars_t {
    pub fn inited(&self) -> rpInited_t {
        rpInited_t {}
    }

    pub fn srcAddr(&self) -> rpSrcAddr_t {
        rpSrcAddr_t {}
    }

    pub fn hThreadMain(&self) -> hThreadMain_t {
        hThreadMain_t {}
    }

    pub fn nwmHdr(&self) -> rpNwmHdr_t {
        rpNwmHdr_t {}
    }

    pub fn config(&self) -> rpConfig_t {
        rpConfig_t::new()
    }
}

#[no_mangle]
pub extern "C" fn rpStartup(buf: *const u8_) {
    let buf = unsafe { mem::transmute::<*const u8_, &[u8_; RP_NWM_HDR_SIZE as usize]>(buf) };
    let buf = nwmHdr_t { buf };
    crate::startUp(startUp_threadVars_t { _z: () }, buf)
}
