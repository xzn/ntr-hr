#![no_std]
#![allow(non_upper_case_globals)]
#![allow(non_camel_case_types)]
#![allow(non_snake_case)]
#![allow(internal_features)]
#![feature(atomic_from_mut)]
#![feature(core_intrinsics)]
#![feature(const_mut_refs)]

use c_str_macro::c_str;
use core::panic::PanicInfo;
use function_name::named;
use wrapper::*;

#[panic_handler]
fn panic(_: &PanicInfo) -> ! {
    core::intrinsics::abort();
}

#[macro_use]
mod wrapper;
mod result;

macro_rules! nsDbgPrint {
    ($fn:ident $(, $es:expr)*) => {
        nsDbgPrint_t::$fn(
            c_str!("nwm_rs|lib.rs").as_ptr() as *const ::libc::c_char,
            line!() as ::libc::c_int,
            c_str!(function_name!()).as_ptr() as *const ::libc::c_char
            $(, $es)*
        );
    };
}

#[named]
fn handlePortCmd(cmd_id: u32_, norm_params: &[u32_], trans_params: &[u32_]) {
    match cmd_id {
        SVC_NWM_CMD_OVERLAY_CALLBACK => {
            let isTop = *norm_params.get(0).unwrap_or(&u32_::MAX);

            let gamePid = *trans_params.get(1).unwrap_or(&0);
            if isTop > 1 {
                rpPortGamePid_t::set_ar(0);
            } else {
                if rpPortGamePid_t::get() != gamePid {
                    rpPortGamePid_t::set_ar(gamePid);
                }
                let ret = rpSyn_t::signalPortEvent(isTop > 0);
                if ret != 0 {
                    nsDbgPrint!(singalPortFailed, ret);
                }
            }
        }
        SVC_NWM_CMD_PARAMS_UPDATE => {
            if rpConfig_t::set_mar(norm_params) {
                rpResetThreads_t::set_ar(true);
            }
        }
        SVC_NWM_CMD_GAME_PID_UPDATE => {
            let gamePid = *norm_params.get(0).unwrap_or(&0);
            rpConfig_t::gamePid_set_ar(gamePid);
        }
        _ => (),
    }
}

fn htons(v: u16_) -> u16_ {
    v.to_be()
}

fn startUp(nwmHdr: nwmHdr_t) {
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

        if rpInited_t::get() {
            let mut needUpdate = false;
            let rpDaddr = rpConfig_t::dstAddr_get_ar();
            if (tcpHit && rpDaddr == 0) || udpHit {
                if rpDaddr != daddr {
                    rpConfig_t::dstAddr_set_ar_update(daddr);

                    needUpdate = true;
                }
            }
            if rpSrcAddr_t::get() != saddr {
                rpSrcAddr_t::set(saddr);

                needUpdate = true;
            }

            if needUpdate {
                rpNwmHdr_t::set(&nwmHdr);
            }
            return;
        }

        rpInited_t::set();

        rpNwmHdr_t::set(&nwmHdr);
        rpConfig_t::dstAddr_set_ar_update(daddr);
        rpSrcAddr_t::set(saddr);

        svcThread_t::create_from_pool(
            Some(rpThreadMain),
            0,
            RP_THREAD_STACK_SIZE,
            RP_THREAD_PRIO_DEFAULT as s32,
            2,
        );
    }
}
