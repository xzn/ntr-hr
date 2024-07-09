// Adapted from ctru-sys build.rs

use bindgen::callbacks::ParseCallbacks;
use bindgen::{Builder, RustTarget};

use std::collections::HashSet;
use std::env;
use std::path::{Path, PathBuf};

use std::process::{Command, Output, Stdio};

#[derive(Debug)]
struct Callback {
    names: HashSet<String>,
    union_names: HashSet<String>,
}

impl Callback {
    fn new() -> Self {
        let mut names = HashSet::<String>::new();
        names.insert("jpeg_compress_struct".into());
        names.insert("rp_alloc_state".into());
        names.insert("rp_alloc_stats".into());
        names.insert("jpeg_error_mgr".into());

        let mut union_names = HashSet::<String>::new();
        union_names.insert("jpeg_error_mgr__bindgen_ty_1".into());

        Self { names, union_names }
    }
}

impl ParseCallbacks for Callback {
    fn add_derives(&self, info: &bindgen::callbacks::DeriveInfo<'_>) -> Vec<String> {
        if self.names.contains(info.name) {
            vec!["ConstDefault".into()]
        } else if self.union_names.contains(info.name) {
            vec!["ConstDefault".into()]
        } else {
            vec![]
        }
    }
}

fn main() {
    let devkitarm = env::var("DEVKITARM").unwrap();
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());

    println!("cargo:rerun-if-changed=build.rs");
    println!("cargo:rerun-if-env-changed=DEVKITPRO");

    let gcc_version = get_gcc_version(PathBuf::from(&devkitarm).join("bin/arm-none-eabi-gcc"));
    let include_path = Path::new("../../include");
    let ctru_include_path = Path::new("../../libctru/libctru/include");
    let nwm_header_str = "nwm_rs.h";
    let nwm_header = Path::new(nwm_header_str);

    println!("cargo:rerun-if-changed={nwm_header_str}");

    let sysroot = Path::new(&devkitarm).join("arm-none-eabi");
    let system_include = sysroot.join("include");
    let gcc_include = PathBuf::from(format!(
        "{devkitarm}/lib/gcc/arm-none-eabi/{gcc_version}/include"
    ));

    let bindings = Builder::default()
        .header(nwm_header.to_str().unwrap())
        .rust_target(RustTarget::Nightly)
        .use_core()
        .trust_clang_mangling(false)
        .must_use_type("Result")
        .layout_tests(false)
        .ctypes_prefix("::libc")
        .prepend_enum_name(false)
        .blocklist_type("u(8|16|32|64)")
        .blocklist_type("__builtin_va_list")
        .blocklist_type("__va_list")
        .blocklist_function("handlePortCmd")
        .blocklist_function("setjmp")
        .blocklist_function("longjmp")
        .blocklist_var("nsConfig")
        .blocklist_var("ntrConfig")
        .blocklist_var("rpConfig")
        .opaque_type("MiiData")
        .derive_default(true)
        .clang_args([
            "--target=arm-none-eabi",
            "--sysroot",
            sysroot.to_str().unwrap(),
            "-isystem",
            system_include.to_str().unwrap(),
            "-isystem",
            gcc_include.to_str().unwrap(),
            "-I",
            include_path.to_str().unwrap(),
            "-I",
            ctru_include_path.to_str().unwrap(),
            "-mfloat-abi=hard",
            "-march=armv6k",
            "-mtune=mpcore",
            "-mfpu=vfp",
            "-DARM11",
            "-D__3DS__",
        ])
        .parse_callbacks(Box::new(Callback::new()))
        .generate()
        .expect("unable to generate bindings");

    bindings
        .write_to_file(out_dir.join("bindings.rs"))
        .expect("Couldn't write bindings!");
}

fn get_gcc_version(path_to_gcc: PathBuf) -> String {
    let Output { stdout, .. } = Command::new(path_to_gcc)
        .arg("--version")
        .stderr(Stdio::inherit())
        .output()
        .unwrap();

    let stdout_str = String::from_utf8_lossy(&stdout);

    stdout_str
        .split(|c: char| c.is_whitespace())
        .nth(4)
        .unwrap()
        .to_string()
}
