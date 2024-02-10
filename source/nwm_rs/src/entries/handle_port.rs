mod safe_impl {
    use super::*;

    #[named]
    pub fn handlePort(t: ThreadVars, cmd_id: u32_, norm_params: &[u32_], trans_params: &[u32_]) {
        match cmd_id {
            SVC_NWM_CMD_OVERLAY_CALLBACK => {
                let is_top = *norm_params.get(0).unwrap_or(&u32_::MAX);

                let game_pid = *trans_params.get(1).unwrap_or(&0);
                if is_top > 1 {
                    t.set_port_game_pid_ar(0);
                } else {
                    if t.port_game_pid() != game_pid {
                        t.set_port_game_pid_ar(game_pid);
                    }
                    let ret = t.signal_port_event(is_top > 0);
                    if ret != 0 {
                        nsDbgPrint!(singalPortFailed, ret);
                    }
                }
            }
            SVC_NWM_CMD_PARAMS_UPDATE => {
                if t.config().set_ar(norm_params) {
                    t.set_reset_threads_ar();
                }
            }
            SVC_NWM_CMD_GAME_PID_UPDATE => {
                let game_pid = *norm_params.get(0).unwrap_or(&0);
                t.config().set_game_pid_ar(game_pid);
            }
            _ => (),
        }
    }
}

use crate::*;

pub struct Config(());

impl Config {
    pub fn set_game_pid_ar(&self, v: u32_) {
        unsafe { AtomicU32::from_ptr(ptr::addr_of_mut!((*rp_config).gamePid)) }
            .store(v, Ordering::Relaxed);
    }

    pub fn set_ar(&self, a: &[u32_]) -> bool {
        if a.len() >= rp_config_u32_count {
            for i in 0..rp_config_u32_count {
                let f = unsafe { AtomicU32::from_ptr((rp_config as *mut u32_).add(i)) };
                let p = unsafe { *a.as_ptr().add(i) };
                f.store(p, Ordering::Relaxed);
            }
            true
        } else {
            false
        }
    }
}

pub struct ThreadVars(());

impl ThreadVars {
    pub fn config(&self) -> Config {
        Config(())
    }

    pub fn set_port_game_pid_ar(&self, v: u32_) {
        unsafe {
            AtomicU32::from_ptr(ptr::addr_of_mut!(port_game_pid)).store(v, Ordering::Relaxed);
        }
    }

    pub fn port_game_pid(&self) -> u32_ {
        unsafe { port_game_pid }
    }

    pub fn set_reset_threads_ar(&self) {
        unsafe { crate::entries::work_thread::set_reset_threads_ar() }
    }

    pub fn signal_port_event(&self, is_top: bool) -> Result {
        unsafe {
            svcSignalEvent(
                *(*syn_handles)
                    .port_screen_ready
                    .get(&Ranged::<SCREEN_COUNT>::from_bool(is_top)),
            )
        }
    }
}

#[no_mangle]
extern "C" fn handlePortCmd(
    cmd_id: u32_,
    norm_params_count: u32_,
    trans_params_size: u32_,
    cmd_buf1: *const u32_,
) {
    unsafe {
        safe_impl::handlePort(
            ThreadVars(()),
            cmd_id,
            slice::from_raw_parts(cmd_buf1, norm_params_count as usize),
            slice::from_raw_parts(
                cmd_buf1.add(norm_params_count as usize),
                trans_params_size as usize,
            ),
        )
    }
}
