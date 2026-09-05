use super::*;

pub struct Impl(());

impl Impl {
    pub fn signal_port_event(&self, is_top: bool) -> Result {
        unsafe { svcSignalEvent(*SYN_HANDLES.screens_port_ready.get(&is_top_index(is_top))) }
    }

    pub fn set_game_pid(&self, v: u32) {
        RP_CONFIG.game_pid().store(v, Ordering::Release);

        if unsafe { entries::thread_screen::SCREEN_HANDLES_INITED.load(Ordering::Acquire) } {
            entries::thread_screen::close_handles();
        }
    }

    pub fn set_config(&self, a: &[u32]) -> bool {
        if a.len() >= config_consts::RP_CONFIG_U32_COUNT {
            let mut ret = false;
            let mut quality = 0;
            let quality_check = !entries::thread_nwm::get_lossless_compression()
                && !entries::thread_nwm::get_reliable_stream_delta_prog();
            for i in 0..config_consts::RP_CONFIG_U32_COUNT {
                let f =
                    unsafe { AtomicU32::from_ptr((config_consts::RP_CONFIG as *mut u32).add(i)) };
                let p = unsafe { *a.as_ptr().add(i) };
                let t = f.load(Ordering::Acquire);
                if t != p {
                    f.store(p, Ordering::Release);
                    if quality_check && i == config_consts::RP_CONFIG_QUALITY_OFFSET_COUNT {
                        quality = p;
                    } else if i != config_consts::RP_CONFIG_DSTADDR_OFFSET_COUNT {
                        ret = true;
                    }
                }
            }

            if !ret && quality > 0 {
                unsafe {
                    encoder::jpeg_update_quality(quality);
                }
            }

            ret
        } else {
            false
        }
    }
}

#[unsafe(no_mangle)]
extern "C" fn handlePortCmd(
    cmd_id: u32,
    norm_params_count: u32,
    trans_params_size: u32,
    cmd_buf1: *const u32,
) {
    unsafe {
        safe_impl::handle_port(
            Impl(()),
            cmd_id,
            slice::from_raw_parts(cmd_buf1, norm_params_count as usize),
            slice::from_raw_parts(
                cmd_buf1.add(norm_params_count as usize),
                trans_params_size as usize,
            ),
        )
    }
}
