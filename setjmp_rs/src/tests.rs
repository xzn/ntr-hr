#![allow(unreachable_code)]

use crate::{jmp_buf, longjmp, setjmp};
use std::mem::MaybeUninit;

struct TestStruct<'a> {
    testptr: &'a mut u32,
}

impl<'a> Drop for TestStruct<'a> {
    fn drop(&mut self) {
        *self.testptr = 0;
    }
}

#[test]
fn simple() {
    unsafe {
        let mut jmpbuf: MaybeUninit<jmp_buf> = MaybeUninit::uninit();
        if setjmp(jmpbuf.as_mut_ptr()) == 0 {
            longjmp(jmpbuf.as_mut_ptr(), 1);
            panic!("longjmp returned!");
        } else {
            return;
        }
    }
    panic!("unreachable!");
}

#[test]
fn longjmp_return() {
    unsafe {
        let mut jmpbuf: MaybeUninit<jmp_buf> = MaybeUninit::uninit();
        let val = setjmp(jmpbuf.as_mut_ptr());
        if val == 0 {
            longjmp(jmpbuf.as_mut_ptr(), 17);
            panic!("longjmp returned!");
        } else if val != 17 {
            panic!("setjmp returned unexpected value!");
        }
    }
}

#[test]
fn destructors() {
    let mut a = 1;
    let mut b = 1;
    unsafe {
        let _x1 = TestStruct { testptr: &mut a };
        let mut jmpbuf: MaybeUninit<jmp_buf> = MaybeUninit::uninit();
        if setjmp(jmpbuf.as_mut_ptr()) == 0 {
            // DANGER: longjmp will skip over x2's destructor!
            let _x2 = TestStruct { testptr: &mut b };
            longjmp(jmpbuf.as_mut_ptr(), 17);
            panic!("longjmp returned!");
        }
    }
    // x1 destructor should have been called
    assert!(a == 0);
    // x2 destructor was not called (DANGER!)
    assert!(b == 1);
}
