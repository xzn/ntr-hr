use crate::*;

#[derive(Clone, Copy, PartialEq, Eq)]
pub struct CName {
    pub mod_path: *const c_char,
    pub line: c_int,
    pub function_name: *const c_char,
}

macro_rules! cname {
    () => {
        CName {
            mod_path: c_str!(module_path!()),
            line: line!() as c_int,
            function_name: c_str!(function_name!()),
        }
    };
}

// From c_str_macro crate
macro_rules! c_str {
    ($lit:expr) => {
        (concat!($lit, "\0").as_ptr() as *const c_char)
    };
}

macro_rules! ns_dbg_print {
    ($fn:ident $(, $es:expr)*) => {
        NsDbgPrint::$fn(
            c_str!(module_path!()),
            line!() as c_int,
            c_str!(function_name!())
            $(, $es)*
        )
    };
}

macro_rules! ns_dbg_print_cname {
    ($e:expr, $fn:ident $(, $es:expr)*) => {
        NsDbgPrint::$fn(
            $e.mod_path,
            $e.line,
            $e.function_name
            $(, $es)*
        )
    };
}

macro_rules! ns_dbg_print_fn {
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
                )
            }
        }
    };
}

pub struct NsDbgPrint(());

#[allow(dead_code)]
impl NsDbgPrint {
    ns_dbg_print_fn!(create_mutex_failed, "Create %s mutex failed %08x\n", name: *const c_char, ret: s32);
    ns_dbg_print_fn!(create_semaphore_failed, "Create %s semaphore failed: %08x\n", name: *const c_char, res: s32);
    ns_dbg_print_fn!(create_event_failed, "Create %s event failed: %08x\n", name: *const c_char, res: s32);
    ns_dbg_print_fn!(mp_init_failed, "Mem pool %s init failed\n", name: *const c_char);

    ns_dbg_print_fn!(wait_syn_failed, "Wait syn %s failed: %08x\n", what: *const c_char, ret: s32);
    ns_dbg_print_fn!(release_mutex_failed, "Release mutex %s failed: %08x\n", what: *const c_char, ret: s32);
    ns_dbg_print_fn!(release_sem_failed, "Release sem %s failed: %08x\n", what: *const c_char, ret: s32);

    ns_dbg_print_fn!(val, "%s: %d\n", what: *const c_char, ret: s32);
    ns_dbg_print_fn!(failed, "%s failed: %08x\n", what: *const c_char, ret: s32);
    ns_dbg_print_fn!(msg, "%s\n", msg: *const c_char);
    ns_dbg_print_fn!(mem_usage, "Mem usage: %08x\n", size: u32);
}
