mod safe_impl {
    use super::*;

    pub fn thread_main(t: ThreadVars, s: &mut ThreadsStacks) {
        loop {
            todo!();

            todo!();
        }
    }
}

use crate::*;

pub struct ThreadVars(());

impl ThreadVars {}

pub struct ThreadsStacks<'a> {
    aux1: &'a mut StackRegion<{ RP_THREAD_STACK_SIZE as usize }>,
    aux2: &'a mut StackRegion<{ RP_THREAD_STACK_SIZE as usize }>,
    nwm: &'a mut StackRegion<{ STACK_SIZE as usize }>,
    screen: &'a mut StackRegion<{ STACK_SIZE as usize }>,
}

mod aux_impl {
    use super::*;

    unsafe fn set_nwm_data_buf(
        i: WorkIndex,
        j: ThreadId,
        buf: &'static mut MemRegion8<NWM_BUFFER_SIZE>,
    ) {
        let info = nwm_infos.get_mut(&i).get_mut(&j);
        info.buf = buf.to_ptr().add(NWM_HDR_SIZE as usize);
        info.buf_packet_last = buf.to_ptr().add(NWM_BUFFER_SIZE - PACKET_DATA_SIZE);
    }

    unsafe fn init_jpeg_compress() -> Result {
        todo!();
        0
    }

    unsafe fn create_event(h: *mut Handle) -> Result {
        svcCreateEvent(h, RESET_ONESHOT)
    }

    #[named]
    pub unsafe fn first_time_init<'a>() -> Option<ThreadsStacks<'a>> {
        let res;
        __system_initSyscalls();
        res = gspInit(1);
        if R_FAILED(res) {
            nsDbgPrint!(gspInitFailed, res);
            return None;
        }

        for i in WorkIndex::all() {
            for j in ThreadId::all() {
                if let Some(m) = request_mem_from_pool::<NWM_BUFFER_SIZE>() {
                    set_nwm_data_buf(i, j, m);
                } else {
                    return None;
                }
            }
        }

        for j in Ranged::<SCREEN_COUNT>::all() {
            for i in ImgWorkIndex::all() {
                if let Some(m) = request_mem_from_pool::<IMG_BUFFER_SIZE>() {
                    *img_infos.get_mut(&j).bufs.get_mut(&i) = m.to_ptr();
                } else {
                    return None;
                }
            }
        }

        for i in Ranged::<CINFOS_COUNT>::all() {
            let idx = i.get();
            if idx == 0 {
                *cinfos_alloc_sizes.get_mut(&i) = 0x18000;
            } else if idx == 1 {
                *cinfos_alloc_sizes.get_mut(&i) = 0x10000;
            } else {
                *cinfos_alloc_sizes.get_mut(&i) = 0x2000;
            }
        }

        if R_FAILED(init_jpeg_compress()) {
            nsDbgPrint!(initJpegFailed);
            return None;
        }

        if let Some(m) =
            request_mem_from_pool::<{ align_to_page_size(mem::size_of::<SynHandles>()) }>()
        {
            syn_handles = m.to_ptr() as *mut SynHandles;
        } else {
            return None;
        }

        for i in Ranged::<SCREEN_COUNT>::all() {
            let res = create_event((*syn_handles).port_screen_ready.get_mut(&i));
            if R_FAILED(res) {
                nsDbgPrint!(createPortEventFailed, res);
                return None;
            }
        }

        let res = create_event(&mut (*syn_handles).nwm_ready);
        if R_FAILED(res) {
            nsDbgPrint!(createNwmEventFailed, res);
            return None;
        }

        if R_FAILED(svcOpenProcess(
            &mut cap_params.home,
            (*ntr_config).HomeMenuPid,
        )) {
            nsDbgPrint!(openProcessFailed, res);
            return None;
        }

        let mut svc_thread = mem::MaybeUninit::uninit();
        let res = create_thread_from_pool::<{ STACK_SIZE as usize }>(
            svc_thread.as_mut_ptr(),
            Some(handlePortThread),
            0,
            0x10,
            1,
        );
        if R_FAILED(res) {
            nsDbgPrint!(createNwmSvcFailed, res);
        }

        let aux1Stack = request_mem_from_pool::<{ RP_THREAD_STACK_SIZE as usize }>()?;
        let aux2Stack = request_mem_from_pool::<{ RP_THREAD_STACK_SIZE as usize }>()?;
        let nwmStack = request_mem_from_pool::<{ STACK_SIZE as usize }>()?;
        let screenStack = request_mem_from_pool::<{ STACK_SIZE as usize }>()?;

        Some(ThreadsStacks {
            aux1: stack_region_from_mem_region(aux1Stack),
            aux2: stack_region_from_mem_region(aux2Stack),
            nwm: stack_region_from_mem_region(nwmStack),
            screen: stack_region_from_mem_region(screenStack),
        })
    }
}

pub extern "C" fn encode_thread_main(_: *mut c_void) {
    if let Some(mut s) = unsafe { aux_impl::first_time_init() } {
        safe_impl::thread_main(ThreadVars(()), &mut s)
    }
    unsafe { svcExitThread() }
}
