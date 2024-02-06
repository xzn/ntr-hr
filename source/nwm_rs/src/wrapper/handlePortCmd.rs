use super::*;

mod handlePortCmd_threadVars_m {
    use super::*;

    pub struct rpConfig_t;

    impl rpConfig_t {
        pub fn new() -> Self {
            rpConfig_t {}
        }

        fn set_mar_ref(&self, v: &RP_CONFIG) {
            for i in 0..rpConfig_u32_count {
                let f = unsafe { AtomicU32::from_ptr((rpConfig as *mut u32_).add(i)) };
                let p = unsafe { *(v as *const RP_CONFIG as *const u32_).add(i) };
                f.store(p, Ordering::Relaxed);
            }
        }

        fn set_mar_array(&self, v: &[u32_; rpConfig_u32_count]) {
            let v = v.as_ptr() as *const RP_CONFIG;
            self.set_mar_ref(&unsafe { *v } as &RP_CONFIG);
        }

        pub fn set_mar(&self, v: &[u32_]) -> bool {
            let ret = v.len() >= rpConfig_u32_count;
            if ret {
                self.set_mar_array(unsafe {
                    mem::transmute::<*const u32_, &[u32_; rpConfig_u32_count]>(v.as_ptr())
                });
            }
            ret
        }

        pub fn gamePid_set_ar(&self, v: u32_) {
            let pid_a = unsafe { AtomicU32::from_ptr(addr_of_mut!((*rpConfig).gamePid)) };
            pid_a.store(v, Ordering::Relaxed);
        }
    }

    pub struct rpPortGamePid_t;

    impl rpPortGamePid_t {
        pub fn get(&self) -> u32_ {
            unsafe { rpPortGamePid }
        }

        pub fn set_ar(&self, v: u32_) {
            let pid_a = unsafe { AtomicU32::from_ptr(addr_of_mut!(rpPortGamePid)) };
            pid_a.store(v, Ordering::Relaxed);
        }
    }

    pub struct rpResetThreads_t;

    impl rpResetThreads_t {
        pub fn set_ar(&self, v: bool) {
            let reset_a = unsafe { AtomicI32::from_ptr(addr_of_mut!(rpResetThreads)) };
            reset_a.store(v as s32, Ordering::Relaxed);
        }
    }

    pub struct rpSyn_t;

    impl rpSyn_t {
        pub fn signalPortEvent(&self, isTop: bool) -> Result {
            unsafe { svcSignalEvent(*(*rp_syn).portEvent.get_unchecked(isTop as usize)) }
        }
    }
}

pub struct handlePortCmd_threadVars_t {
    _z: (),
}

use handlePortCmd_threadVars_m::*;

impl handlePortCmd_threadVars_t {
    pub fn portGamePid(&self) -> rpPortGamePid_t {
        rpPortGamePid_t {}
    }

    pub fn config(&self) -> rpConfig_t {
        rpConfig_t::new()
    }

    pub fn resetThreads(&self) -> rpResetThreads_t {
        rpResetThreads_t {}
    }

    pub fn syn(&self) -> rpSyn_t {
        rpSyn_t {}
    }
}

#[no_mangle]
extern "C" fn handlePortCmd(
    cmd_id: u32_,
    norm_param_count: u32_,
    trans_param_size: u32_,
    cmd_buf1: *const u32_,
) {
    crate::handlePortCmd(
        handlePortCmd_threadVars_t { _z: () },
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
