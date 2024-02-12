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
