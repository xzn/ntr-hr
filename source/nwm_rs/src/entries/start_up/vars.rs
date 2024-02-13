use super::*;
pub struct NwmHdr<'a>(&'a [u8_; Self::N]);

impl NwmHdr<'_> {
    const N: usize = NWM_HDR_SIZE as usize;
}

static mut rp_inited: bool = false;
static mut rp_src_addr: u32_ = 0;

pub struct ThreadVars(());

pub struct Config(());

impl Config {
    fn dst_addr_ar_wrap(&self) -> &AtomicU32 {
        unsafe { AtomicU32::from_mut(&mut (*rp_config).dstAddr) }
    }

    pub fn dst_addr_ar(&self) -> u32_ {
        self.dst_addr_ar_wrap().load(Ordering::Relaxed)
    }

    fn set_dst_addr_ar(&self, v: u32_) {
        self.dst_addr_ar_wrap().store(v, Ordering::Relaxed)
    }

    #[named]
    pub fn set_dst_addr_ar_and_update(&self, mut v: u32_) {
        self.set_dst_addr_ar(v);

        let res = unsafe {
            copyRemoteMemory(
                home_process_handle,
                ptr::addr_of_mut!((*rp_config).dstAddr) as *mut c_void,
                CUR_PROCESS_HANDLE,
                ptr::addr_of_mut!(v) as *mut c_void,
                mem::size_of::<u32_>() as u32_,
            ) as i32
        };
        if res != 0 {
            nsDbgPrint!(copyRemoteMemoryFailed, res);
        }
    }
}

impl ThreadVars {
    pub fn config(&self) -> Config {
        Config(())
    }

    pub fn thread_main_handle(&self) -> *mut Handle {
        unsafe { ptr::addr_of_mut!(thread_main_handle) }
    }

    #[named]
    pub fn open_home_process(&self) -> bool {
        unsafe {
            let res = svcOpenProcess(&mut home_process_handle, (*ntr_config).HomeMenuPid);
            if res != 0 {
                nsDbgPrint!(openProcessFailed, res);
                false
            } else {
                true
            }
        }
    }

    pub fn inited(&self) -> bool {
        unsafe { rp_inited }
    }

    pub fn set_inited(&self) {
        unsafe { rp_inited = true }
    }

    pub fn src_addr(&self) -> u32_ {
        unsafe { rp_src_addr }
    }

    pub fn set_src_addr(&self, v: u32_) {
        unsafe { rp_src_addr = v }
    }

    pub fn set_nwm_hdr(&self, v: &NwmHdr) {
        unsafe {
            ptr::copy_nonoverlapping(
                v.0.as_ptr(),
                crate::entries::thread_nwm::get_current_nwm_hdr().as_mut_ptr(),
                NwmHdr::N,
            )
        }
    }
}

impl NwmHdr<'_> {
    pub fn protocol(&self) -> u8_ {
        self.0[0x17 + 0x8]
    }

    pub fn src_port(&self) -> u16_ {
        unsafe { *(&self.0[0x22 + 0x8] as *const u8_ as *const u16_) }
    }

    pub fn dst_port(&self) -> u16_ {
        unsafe { *(&self.0[0x22 + 0xa] as *const u8_ as *const u16_) }
    }

    pub fn srcAddr(&self) -> u32_ {
        unsafe { (&self.0[0x1a + 0x8] as *const u8_ as *const u32_).read_unaligned() }
    }

    pub fn dstAddr(&self) -> u32_ {
        unsafe { (&self.0[0x1e + 0x8] as *const u8_ as *const u32_).read_unaligned() }
    }
}

#[no_mangle]
extern "C" fn rpStartup(buf: *const u8_) {
    let buf = unsafe { mem::transmute(buf) };
    let buf = NwmHdr(buf);
    safe_impl::start_up(ThreadVars(()), buf)
}
