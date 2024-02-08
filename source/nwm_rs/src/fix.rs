use crate::*;

pub const SCALE_BITS: u32_ = 16;

pub const ONE_HALF: u32_ = 1 << (SCALE_BITS - 1);

pub const fn FIX(x: c_double) -> u32_ {
    (x * (1 << SCALE_BITS) as c_double + 0.5) as u32_
}
