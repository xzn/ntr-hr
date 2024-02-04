// Copyright 2017 Mikhail Zabaluev <mikhail.zabaluev@gmail.com>
// See the COPYRIGHT file at the top-level directory of this distribution
// and at http://rust-lang.org/COPYRIGHT.
//
// Licensed under the Apache License, Version 2.0 <LICENSE-APACHE or
// http://www.apache.org/licenses/LICENSE-2.0> or the MIT license
// <LICENSE-MIT or http://opensource.org/licenses/MIT>, at your
// option. This file may not be copied, modified, or distributed
// except according to those terms.

#[macro_use]
extern crate c_str_macro;

use std::ffi::CStr;

fn check_c_str(c_str: &CStr, expected: &[u8]) {
    let buf = c_str.as_ptr();
    let len = expected.len();
    for i in 0 .. len {
        let byte = unsafe { *buf.offset(i as isize) as u8 };
        assert_eq!(byte, expected[i]);
    }
    let term = unsafe { *buf.offset(len as isize) as u8 };
    assert_eq!(term, 0);
}

#[test]
fn test_c_str_macro() {
    let c_str = c_str!("hello");
    check_c_str(c_str, b"hello");
}
