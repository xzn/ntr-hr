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
    eq_names: HashSet<String>,
}

impl Callback {
    fn new() -> Self {
        let mut names = HashSet::<String>::new();
        names.insert("rp_cb".into());
        names.insert("DmaConfig".into());
        names.insert("DmaDeviceConfig".into());
        names.insert("DmaMemoryConfig".into());

        let mut union_names = HashSet::<String>::new();
        union_names.insert("nwm_cb".into());

        let mut eq_names = HashSet::<String>::new();
        eq_names.insert("nwm_cb".into());
        eq_names.insert("RP_CONFIG".into());
        eq_names.insert("RP_SCREEN_CONFIG".into());

        Self {
            names,
            union_names,
            eq_names,
        }
    }
}

impl ParseCallbacks for Callback {
    fn add_derives(&self, info: &bindgen::callbacks::DeriveInfo<'_>) -> Vec<String> {
        if self.names.contains(info.name) || self.union_names.contains(info.name) {
            vec!["ConstDefault".into()]
        } else if self.eq_names.contains(info.name) {
            vec!["ConstDefault".into(), "PartialEq".into()]
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
    let include_path_str = "../../include";
    let include_path = Path::new(include_path_str);
    let ctru_include_path_str = "../../libctru/libctru/include";
    let ctru_include_path = Path::new(ctru_include_path_str);
    let nwm_header_str = "nwm_rs.h";
    let nwm_header = Path::new(nwm_header_str);

    #[cfg(not(target_os = "windows"))]
    {
        let current_dir = env::current_dir().unwrap();
        let rerun_headers = |path: &str| {
            let stdout = match Command::new("find")
                .args([path, "-name", "*.h", "-type", "f"])
                .stderr(Stdio::inherit())
                .output()
            {
                Ok(Output { stdout, status, .. }) if status.success() => stdout,
                Ok(Output { status, .. }) => {
                    println!("cargo::error=find failed with status {status}");
                    return;
                }
                Err(err) => {
                    println!("cargo::error=find failed {err}");
                    return;
                }
            };
            for line in String::from_utf8_lossy(&stdout).lines() {
                let file = current_dir.join(line).canonicalize().unwrap();
                let file = file.display();
                println!("cargo:rerun-if-changed={file}");
            }
        };
        rerun_headers("..");
        rerun_headers(include_path_str);
        rerun_headers(ctru_include_path_str);
    }

    let sysroot = Path::new(&devkitarm).join("arm-none-eabi");
    let system_include = sysroot.join("include");
    let gcc_include = PathBuf::from(format!(
        "{devkitarm}/lib/gcc/arm-none-eabi/{gcc_version}/include"
    ));

    let bindings = Builder::default()
        .header(nwm_header.to_str().unwrap())
        .rust_target(RustTarget::nightly())
        .use_core()
        .trust_clang_mangling(false)
        .must_use_type("Result")
        .layout_tests(false)
        .ctypes_prefix("::libc")
        .prepend_enum_name(false)
        .blocklist_type("u(8|16|32|64)")
        .blocklist_type("__builtin_va_list")
        .blocklist_type("__va_list")
        .blocklist_type("timeval")
        .blocklist_type("in_addr")
        .blocklist_type("sockaddr_storage")
        .blocklist_type("(in_addr|socklen|suseconds|sa_family|time)_t")
        .blocklist_item("SOL_CONFIG")
        .blocklist_function("handlePortCmd")
        .blocklist_function("setjmp")
        .blocklist_function("longjmp")
        .blocklist_function("bcmp")
        .blocklist_function("memcmp")
        .blocklist_function("memcpy")
        .blocklist_function("memmove")
        .blocklist_function("memset")
        .blocklist_function("strlen")
        .blocklist_var("nsConfig")
        .blocklist_var("ntrConfig")
        .blocklist_var("rpConfig")
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
            "-mtp=soft",
            "-DARM11",
            "-D__3DS__",
            "-fshort-enums",
        ]);
    let bindings = if cfg!(feature = "o3ds") {
        bindings.clang_arg("-DOLD_3DS")
    } else {
        bindings.clang_arg("-DNEW_3DS")
    };
    let bindings = bindings
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
