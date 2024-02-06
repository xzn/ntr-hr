#![no_std]
#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(internal_features)]
#![feature(atomic_from_mut)]
#![feature(core_intrinsics)]
#![feature(const_mut_refs)]

use core::panic::PanicInfo;
use wrapper::*;

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    core::intrinsics::abort();
}

#[macro_use]
mod wrapper;
mod result;

#[named]
fn handlePortCmd(
    t: handlePortCmd_threadVars_t,
    cmd_id: u32_,
    norm_params: &[u32_],
    trans_params: &[u32_],
) {
    match cmd_id {
        SVC_NWM_CMD_OVERLAY_CALLBACK => {
            let isTop = *norm_params.get(0).unwrap_or(&u32_::MAX);

            let gamePid = *trans_params.get(1).unwrap_or(&0);
            if isTop > 1 {
                t.portGamePid().set_ar(0);
            } else {
                if t.portGamePid().get() != gamePid {
                    t.portGamePid().set_ar(gamePid);
                }
                let ret = t.syn().signalPortEvent(isTop > 0);
                if ret != 0 {
                    nsDbgPrint!(singalPortFailed, ret);
                }
            }
        }
        SVC_NWM_CMD_PARAMS_UPDATE => {
            if t.config().set_mar(norm_params) {
                t.resetThreads().set_ar(true);
            }
        }
        SVC_NWM_CMD_GAME_PID_UPDATE => {
            let gamePid = *norm_params.get(0).unwrap_or(&0);
            t.config().gamePid_set_ar(gamePid);
        }
        _ => (),
    }
}

fn htons(v: u16_) -> u16_ {
    v.to_be()
}

fn startUp(t: startUp_threadVars_t, nwmHdr: nwmHdr_t) {
    let protocol = nwmHdr.protocol();
    let srcPort = nwmHdr.srcPort();
    let dstPort = nwmHdr.dstPort();

    let tcpHit = protocol == 0x6 && srcPort == htons(NS_MENU_LISTEN_PORT as u16);
    let udpHit = protocol == 0x11
        && srcPort == htons(NWM_INIT_SRC_PORT as u16)
        && dstPort == htons(NWM_INIT_DST_PORT as u16);

    if tcpHit || udpHit {
        let saddr = nwmHdr.srcAddr();
        let daddr = nwmHdr.dstAddr();

        if t.inited().get() {
            let mut needUpdate = false;
            let rpDaddr = t.config().dstAddr_get_ar();
            if (tcpHit && rpDaddr == 0) || udpHit {
                if rpDaddr != daddr {
                    t.config().dstAddr_set_ar_update(daddr);

                    needUpdate = true;
                }
            }
            if t.srcAddr().get() != saddr {
                t.srcAddr().set(saddr);

                needUpdate = true;
            }

            if needUpdate {
                t.nwmHdr().set(&nwmHdr);
            }
            return;
        }

        t.inited().set();

        t.nwmHdr().set(&nwmHdr);
        t.config().dstAddr_set_ar_update(daddr);
        t.srcAddr().set(saddr);

        create_thread_from_pool(
            t.hThreadMain().get_mut_ref(),
            Some(rpThreadMain),
            0,
            RP_THREAD_STACK_SIZE,
            RP_THREAD_PRIO_DEFAULT as s32,
            2,
        );
    }
}
