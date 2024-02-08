use crate::*;

pub const SCALE_BITS: u32_ = 16;

pub const ONE_HALF: u32_ = 1 << (SCALE_BITS - 1);

pub const fn FIX(x: c_double) -> u32_ {
    (x * (1 << SCALE_BITS) as c_double + 0.5) as u32_
}

#[derive(Copy, Clone, ConstDefault, PartialEq, Eq, PartialOrd, Ord)]
pub struct Fix(pub u64_);

#[derive(Copy, Clone, ConstDefault, PartialEq, Eq, PartialOrd, Ord)]
pub struct Fix32(pub u32_);

impl Fix {
    pub const fn fix(v: u32_) -> Self {
        Self((v as u64_) << SCALE_BITS)
    }

    pub const fn unfix(&self) -> u32_ {
        ((self.0 + ONE_HALF as u64_) >> SCALE_BITS) as u32_
    }

    pub const fn store_to_u32(&self) -> Fix32 {
        Fix32(self.0 as u32_)
    }

    pub const fn fix32(v: u32_) -> Fix32 {
        Self::fix(v).store_to_u32()
    }

    pub const fn load_from_u32(v: Fix32) -> Self {
        Self(v.0 as u64_)
    }
}

impl Add for Fix {
    type Output = Self;

    fn add(self, rhs: Self) -> Self::Output {
        Self(self.0 + rhs.0)
    }
}

impl Sub for Fix {
    type Output = Self;

    fn sub(self, rhs: Self) -> Self::Output {
        Self(self.0 - rhs.0)
    }
}

impl Mul for Fix {
    type Output = Self;

    fn mul(self, rhs: Self) -> Self::Output {
        Self((self.0 * rhs.0) >> SCALE_BITS)
    }
}

impl Div for Fix {
    type Output = Self;

    fn div(self, rhs: Self) -> Self::Output {
        Self((self.0 << SCALE_BITS) / rhs.0)
    }
}
