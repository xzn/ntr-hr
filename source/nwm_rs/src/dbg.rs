use crate::*;

// From c_str_macro crate
macro_rules! c_str {
    ($lit:expr) => {
        (concat!($lit, "\0").as_ptr() as *const c_char)
    };
}

macro_rules! nsDbgPrint {
    ($fn:ident $(, $es:expr)*) => {
        nsDbgPrint_t::$fn(
            c_str!(module_path!()),
            line!() as c_int,
            c_str!(function_name!())
            $(, $es)*
        )
    };
}

macro_rules! nsDbgPrint_fn {
    ($fn:ident, $fmt:expr $(, $vn:ident: $ty:ty)*) => {
        pub fn $fn(
            file_name: *const c_char,
            line_number: c_int,
            func_name: *const c_char
            $(, $vn: $ty)*
        ) {
            unsafe {
                nsDbgPrintVerbose(
                    file_name,
                    line_number,
                    func_name,
                    c_str!($fmt)
                    $(, $vn)*
                );
            }
        }
    };
}

pub struct nsDbgPrint_t {
    _z: (),
}

impl nsDbgPrint_t {
    nsDbgPrint_fn!(singalPortFailed, "Signal port event failed: %08x\n", ret: s32);

    nsDbgPrint_fn!(openProcessFailed, "Open process failed: %08x\n", ret: s32);

    nsDbgPrint_fn!(copyRemoteMemoryFailed, "Copy remote memory failed: %08x\n", ret: s32);

    nsDbgPrint_fn!(gspInitFailed, "GSP init failed: %08x\n", ret: s32);

    nsDbgPrint_fn!(initJpegFailed, "JPEG init failed\n");

    nsDbgPrint_fn!(createPortEventFailed, "Create port event failed %08x\n", ret: s32);

    nsDbgPrint_fn!(createNwmEventFailed, "Create nwm event failed %08x\n", ret: s32);

    nsDbgPrint_fn!(createNwmSvcFailed, "Create remote play service thread failed: %08x\n", ret: s32);

    nsDbgPrint_fn!(setThreadPriorityFailed, "Set thread priority failed: %08x\n", res: s32);

    nsDbgPrint_fn!(createSemaphoreFailed, "Create %s semaphore failed: %08x\n", name: *const c_char, res: s32);

    nsDbgPrint_fn!(allocFailed, "Bad alloc, size: %x/%x\n", total_size: u32_, alloc_size: u32_);

    nsDbgPrint_fn!(sendBufferOverflow, "Send buffer overflow\n");

    nsDbgPrint_fn!(nwmEventSignalFailed, "Signal nwm event failed %08x\n", ret: s32);
}
