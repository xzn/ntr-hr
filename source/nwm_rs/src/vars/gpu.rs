#![allow(dead_code)]

use super::{u32_, IoBasePdc};

// From Luma3DS
pub const GPU_FB_TOP_SIZE: u32_ = IoBasePdc + 0x45c;
pub const GPU_FB_TOP_LEFT_ADDR_1: u32_ = IoBasePdc + 0x468;
pub const GPU_FB_TOP_LEFT_ADDR_2: u32_ = IoBasePdc + 0x46C;
pub const GPU_FB_TOP_FMT: u32_ = IoBasePdc + 0x470;
pub const GPU_FB_TOP_SEL: u32_ = IoBasePdc + 0x478;
pub const GPU_FB_TOP_COL_LUT_INDEX: u32_ = IoBasePdc + 0x480;
pub const GPU_FB_TOP_COL_LUT_ELEM: u32_ = IoBasePdc + 0x484;
pub const GPU_FB_TOP_STRIDE: u32_ = IoBasePdc + 0x490;
pub const GPU_FB_TOP_RIGHT_ADDR_1: u32_ = IoBasePdc + 0x494;
pub const GPU_FB_TOP_RIGHT_ADDR_2: u32_ = IoBasePdc + 0x498;

pub const GPU_FB_BOTTOM_SIZE: u32_ = IoBasePdc + 0x55c;
pub const GPU_FB_BOTTOM_ADDR_1: u32_ = IoBasePdc + 0x568;
pub const GPU_FB_BOTTOM_ADDR_2: u32_ = IoBasePdc + 0x56C;
pub const GPU_FB_BOTTOM_FMT: u32_ = IoBasePdc + 0x570;
pub const GPU_FB_BOTTOM_SEL: u32_ = IoBasePdc + 0x578;
pub const GPU_FB_BOTTOM_COL_LUT_INDEX: u32_ = IoBasePdc + 0x580;
pub const GPU_FB_BOTTOM_COL_LUT_ELEM: u32_ = IoBasePdc + 0x584;
pub const GPU_FB_BOTTOM_STRIDE: u32_ = IoBasePdc + 0x590;
