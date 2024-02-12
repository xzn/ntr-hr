use super::vars::NwmHdr;
use super::*;

pub fn start_up(t: ThreadVars, nwm_hdr: NwmHdr) {
    let protocol = nwm_hdr.protocol();
    let src_port = nwm_hdr.src_port();
    let dst_port = nwm_hdr.dst_port();

    let tcp_hit = protocol == 0x6 && src_port == htons(NS_MENU_LISTEN_PORT as u16);
    let udp_hit = protocol == 0x11
        && src_port == htons(NWM_INIT_SRC_PORT as u16)
        && dst_port == htons(NWM_INIT_DST_PORT as u16);

    if tcp_hit || udp_hit {
        let saddr = nwm_hdr.srcAddr();
        let daddr = nwm_hdr.dstAddr();

        if t.inited() {
            let mut need_update = false;
            let rp_daddr = t.config().dst_addr_ar();
            if (tcp_hit && rp_daddr == 0) || udp_hit {
                if rp_daddr != daddr {
                    t.config().set_dst_addr_ar_and_update(daddr);

                    need_update = true;
                }
            }
            if t.src_addr() != saddr {
                t.set_src_addr(saddr);

                need_update = true;
            }

            if need_update {
                t.set_nwm_hdr(&nwm_hdr);
            }
            return;
        }

        if !t.open_home_process() {
            return;
        }

        t.set_inited();

        t.set_nwm_hdr(&nwm_hdr);
        t.config().set_dst_addr_ar_and_update(daddr);
        t.set_src_addr(saddr);

        create_thread_from_pool::<{ RP_THREAD_STACK_SIZE as usize }>(
            t.thread_main_handle(),
            Some(crate::entries::thread_main::encode_thread_main),
            0,
            RP_THREAD_PRIO_DEFAULT as s32,
            2,
        );
    }
}
