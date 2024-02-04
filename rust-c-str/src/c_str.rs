// Copyright 2017 Mikhail Zabaluev <mikhail.zabaluev@gmail.com>
// See the COPYRIGHT file at the top-level directory of this source tree.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#![warn(clippy::all)]
#![no_std]

//! A macro to produce C-compatible string data from literals.
//!
//! In Rust code using FFI bindings, it is often necessary to pass in a static
//! constant string that must follow the C string format, i.e. be terminated
//! with a 0 byte. Rust string literals, unlike in C or C++, do not
//! translate to implicitly null-terminated string data, so the terminating
//! `"\0"` has to be explicitly present to make the string safe to pass
//! to a C API. This is kludgy and can easily be forgotten.
//!
//! To alleviate this issue, this crate provides the `c_str!` macro that
//! takes a Rust string literal, appends a terminating 0 byte, and casts
//! the output value to a `std::ffi::CStr` reference.


/// Produce a `CStr` reference out of a static string literal.
///
/// This macro provides a convenient way to use string literals in
/// expressions where a `std::ffi::CStr` reference is accepted.
/// The macro parameter does not need to end with `"\0"`, as the 0 byte is
/// appended by the macro.
///
/// The lifetime of the output reference is inferred from the expansion
/// site. This is always safe because the string is static and immutable.
///
/// # Example
///
/// ```rust
/// use c_str_macro::c_str;
///
/// fn main() {
///     let s = c_str!("Hello, world!");
///     unsafe { libc::puts(s.as_ptr()) };
/// }
/// ```
#[macro_export]
macro_rules! c_str {
    ($lit:expr) => {
        // Currently, there is no working way to concatenate a byte string
        // literal out of bytestring or string literals. Otherwise, we could
        // use from_static_bytes and accept byte strings as well.
        // See https://github.com/rust-lang/rfcs/pull/566
        unsafe {
            core::ffi::CStr::from_ptr(concat!($lit, "\0").as_ptr()
                                      as *const core::ffi::c_char)
        }
    }
}
