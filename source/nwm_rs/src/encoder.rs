// Contains code modified from github.com/libjpeg-turbo/libjpeg-turbo for use in NTR-HR
// See LICENSE-libjpeg-turbo.md at the project root for license details

#![allow(unused_imports)]

#[cfg(not(feature = "o3ds"))]
mod delta_q;
mod encode;
mod init;
mod vars;
mod worker;
mod worker_dst;

#[cfg(feature = "o3ds")]
pub struct QuantizeRet();

use crate::*;
#[cfg(not(feature = "o3ds"))]
pub use delta_q::*;
pub use encode::*;
pub use init::*;
pub use vars::*;
pub use worker::*;
pub use worker_dst::*;

#[cfg(not(feature = "o3ds"))]
pub struct JpegDqRet {
    pub delta_q: u8,
}

#[cfg(not(feature = "o3ds"))]
pub struct JpegRet {
    pub quality: u8,
    pub mcus: u16,
}

#[cfg(not(feature = "o3ds"))]
pub enum JpegEncodeRet {
    JpegRet(JpegRet),
    JpegDqRet(JpegDqRet),
}

pub struct LosslessRet {
    #[cfg(not(feature = "o3ds"))]
    pub color_bias: u8,
}

pub enum LosslessEncodeRet {
    LosslessRet(LosslessRet),
}

#[cfg(not(feature = "mem3"))]
#[derive(ConstDefault)]
pub struct McuRowParams {
    pub mcu_col_start: u16,
    pub mcu_col_end: u16,
}

#[cfg(not(feature = "mem3"))]
#[derive(ConstDefault)]
pub struct CheckerParams {
    pub mcus: u16,
    pub mcu_rows: u16,
    pub mcus_per_row: u16,
    pub mcu_row_params: [McuRowParams; jdiv_round_up(downsample_checker_screen_dim(true), DCTSIZE)],
}

pub struct JpegScreenShared {
    pub max_blocks_in_mcu: usize,
    pub mcu_row_size: usize,
    pub mcu_col_size: usize,
    pub mcus_per_row: usize,
    pub mcu_rows: u16,
    pub mcus: u16,
    #[cfg(not(feature = "o3ds"))]
    delta_q_params: DeltaQParams,
    #[cfg(not(feature = "mem3"))]
    pub checker: CheckerParams,

    quant_tbls: QuantTbls,
    divisors: Divisors,
    div_shifts: [[u8; DCTSIZE2]; NUM_QUANT_TBLS],
}

pub struct JpegShared {
    pub quality: [u32; RP_SCREEN_COUNT as usize],
    pub screens: [JpegScreenShared; RP_SCREEN_COUNT as usize],
    #[cfg(not(feature = "o3ds"))]
    pub quality_need_update: AtomicBool,
    #[cfg(not(feature = "o3ds"))]
    pub quality_can_update: Handle,
    #[cfg(not(feature = "o3ds"))]
    pub quality_done_update: Handle,
    #[cfg(not(feature = "o3ds"))]
    pub div_delta_q_shifts: [[[u8; DCTSIZE2]; NUM_QUANT_TBLS]; DELTA_Q_COUNT as usize],
    #[cfg(not(feature = "o3ds"))]
    pub delta_q_tbls: [[[u8; DCTSIZE2]; NUM_QUANT_TBLS]; DELTA_Q_COUNT as usize],
    #[cfg(not(feature = "o3ds"))]
    pub delta_q0_tbls: [[[u8; DCTSIZE2]; NUM_QUANT_TBLS]; DELTA_Q_COUNT as usize],
}

#[cfg(not(feature = "o3ds"))]
pub struct JpegSharedMut {
    pub delta_q: RangedArray<[u8; DOWNSAMPLE_FACTOR], SCREEN_COUNT>,
    pub work_delta_q: RangedArray<u8, WORK_COUNT>,
    pub dq_rescale_prev: RangedArray<s8, WORK_COUNT>,
    pub rp_shifts: RangedArray<[[u8; DCTSIZE2]; NUM_QUANT_TBLS], WORK_COUNT>,
    pub delta_q_cache: RangedArray<[DeltaQCache; DELTA_Q_CACHE_TOTAL as usize], WORK_COUNT>,
    pub delta_q_cache_next: RangedArray<RangedArray<u8, { MAX_COMPONENTS as u32 }>, WORK_COUNT>,
    pub delta_q_calc: [[DeltaQManager; DOWNSAMPLE_FACTOR]; SCREEN_COUNT as usize],
    pub dq_prev_coeffs_top: [JCoef; DELTA_Q_PREV_COEFFS_TOP_N],
    pub dq_prev_coeffs_bot: [JCoef; DELTA_Q_PREV_COEFFS_BOT_N],
}

pub struct LosslessShared {
    pub color_bias: [u8; RP_SCREEN_COUNT as usize],
}

pub struct ScreenShared {
    pub max_h_samp_factor: usize,
    pub max_v_samp_factor: usize,
    pub downsample: u8,
    pub width: u16,
    pub height: u16,
}

pub struct EncoderShared {
    pub comp_infos: [*const CompInfos; RP_SCREEN_COUNT as usize],
    #[cfg(not(feature = "o3ds"))]
    pub rel_stream: bool,
    #[cfg(not(feature = "o3ds"))]
    pub delta_prog: bool,
    #[cfg(not(feature = "o3ds"))]
    pub core_count: CoreCount,
    pub screens: [ScreenShared; RP_SCREEN_COUNT as usize],
    pub last_restart_range: u32,
    pub encode_tbls: EncodeTbls,
    #[cfg(not(feature = "o3ds"))]
    pub work_sem: RangedArray<Handle, WORK_COUNT>,
    #[cfg(not(feature = "o3ds"))]
    pub screen_sem: RangedArray<Handle, SCREEN_COUNT>,
}

#[cfg(not(feature = "o3ds"))]
pub struct LosslessSharedMut {
    pub prev_coeffs_top: [u8; LOSSLESS_PREV_COEFFS_TOP_N],
    pub prev_coeffs_bot: [u8; LOSSLESS_PREV_COEFFS_BOT_N],
}

#[cfg(not(feature = "o3ds"))]
pub union EncoderSharedMut {
    pub jpeg: mem::ManuallyDrop<JpegSharedMut>,
    pub lossless: mem::ManuallyDrop<LosslessSharedMut>,
}

#[cfg(not(feature = "o3ds"))]
pub struct CommonSharedMut {
    pub rand32: Rand32,
    pub compressed_size: RangedArray<[AtomicU32; DOWNSAMPLE_FACTOR], SCREEN_COUNT>,
    pub work_inited: RangedArray<AtomicBool, WORK_COUNT>,
    pub work_sem_count: RangedArray<AtomicU8, WORK_COUNT>,
    pub screen_bool: RangedArray<AtomicBool, SCREEN_COUNT>,
    pub last_restart_interval: RangedArray<u16, SCREEN_COUNT>,
}

pub struct Encoder {
    pub jpeg_shared: JpegShared,
    #[cfg(not(feature = "o3ds"))]
    pub encoder_shared_mut: EncoderSharedMut,
    #[cfg(not(feature = "o3ds"))]
    pub common_shared_mut: CommonSharedMut,
    pub lossless_shared: LosslessShared,
    pub shared: EncoderShared,
    pub bufs: [WorkerBufs; RP_CORE_COUNT_MAX as usize],
    pub info: [CInfo; WORK_COUNT as usize],
}

#[cfg(not(feature = "o3ds"))]
const DELTA_Q_PREV_COEFFS_TOP_N: usize =
    GSP_SCREEN_WIDTH as usize * GSP_SCREEN_HEIGHT_TOP as usize * MAX_COMPONENTS;
#[cfg(not(feature = "o3ds"))]
const DELTA_Q_PREV_COEFFS_BOT_N: usize =
    GSP_SCREEN_WIDTH as usize * GSP_SCREEN_HEIGHT_BOTTOM as usize * MAX_COMPONENTS;

#[cfg(not(feature = "o3ds"))]
const LOSSLESS_PREV_COEFFS_TOP_N: usize = DELTA_Q_PREV_COEFFS_TOP_N;
#[cfg(not(feature = "o3ds"))]
const LOSSLESS_PREV_COEFFS_BOT_N: usize = DELTA_Q_PREV_COEFFS_BOT_N;
