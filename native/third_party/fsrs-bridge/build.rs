#![allow(linker_messages)]

fn main() {
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() == Ok("android") {
        println!("cargo:rustc-cdylib-link-arg=-Wl,-soname,libquizapp_fsrs_bridge.so");
    }
}
