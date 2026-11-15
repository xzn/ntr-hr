use super::*;

#[named]
pub fn handle_port(impl_: Impl, cmd_id: u32, norm_params: &[u32], trans_params: &[u32]) -> u32 {
    match cmd_id as u8 {
        SVC_NWM_CMD_OVERLAY_CALLBACK => {
            let is_top = *norm_params.get(0).unwrap_or(&u32::MAX);

            let game_pid = *trans_params.get(1).unwrap_or(&0);
            if is_top > 1 {
                entries::thread_screen::set_port_game_pid(0);
            } else {
                if entries::thread_screen::port_game_pid() != game_pid {
                    entries::thread_screen::set_port_game_pid(game_pid);
                }
                let ret = impl_.signal_port_event(is_top > 0);
                if ret != 0 {
                    ns_dbg_print!(failed, c_str!("Signal port event"), ret);
                }
            }
        }
        SVC_NWM_CMD_PARAMS_UPDATE => {
            if impl_.set_config(norm_params) {
                set_reset_threads();
            }
        }
        SVC_NWM_CMD_GAME_PID_UPDATE => {
            let game_pid = *norm_params.get(0).unwrap_or(&0);
            impl_.set_game_pid(game_pid);
        }
        SVC_NWM_CMD_SET_CSND_CAP => {
            let _csnd_cap = *norm_params.get(0).unwrap_or(&0) > 0;
        }
        _ => (),
    };

    0
}
