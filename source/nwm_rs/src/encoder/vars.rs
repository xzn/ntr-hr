// Contains code modified from github.com/libjpeg-turbo/libjpeg-turbo for use in NTR-HR
// See LICENSE-libjpeg-turbo.md at the project root for license details

use super::*;

pub const LOSSLESS_BLOCK_SIZE: usize = 16;

const CONST_BITS: usize = 14;
pub const DCTSIZE: usize = 8;
pub const DCTSIZE2: usize = DCTSIZE * DCTSIZE;
const AANSCALES: [u16; DCTSIZE2] = [
    16384, 22725, 21407, 19266, 16384, 12873, 8867, 4520, 22725, 31521, 29692, 26722, 22725, 17855,
    12299, 6270, 21407, 29692, 27969, 25172, 21407, 16819, 11585, 5906, 19266, 26722, 25172, 22654,
    19266, 15137, 10426, 5315, 16384, 22725, 21407, 19266, 16384, 12873, 8867, 4520, 12873, 17855,
    16819, 15137, 12873, 10114, 6967, 3552, 8867, 12299, 11585, 10426, 8867, 6967, 4799, 2446,
    4520, 6270, 5906, 5315, 4520, 3552, 2446, 1247,
];

const BITS_DC_LUMINANCE: [u8; 17] = [
    /* 0-base */ 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
];
const VAL_DC_LUMINANCE: [u8; 12] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11];

const BITS_DC_CHROMINANCE: [u8; 17] = [
    /* 0-base */ 0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
];
const VAL_DC_CHROMINANCE: [u8; 12] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11];

const BITS_AC_LUMINANCE: [u8; 17] = [
    /* 0-base */ 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d,
];

const VAL_AC_LUMINANCE: [u8; 162] = [
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61, 0x07,
    0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0,
    0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49,
    0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69,
    0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5,
    0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2,
    0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa,
];

const BITS_AC_CHROMINANCE: [u8; 17] = [
    /* 0-base */ 0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77,
];

const VAL_AC_CHROMINANCE: [u8; 162] = [
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71,
    0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33, 0x52, 0xf0,
    0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48,
    0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68,
    0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5,
    0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3,
    0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8,
    0xf9, 0xfa,
];

#[derive(ConstDefault)]
pub struct HuffTbl {
    /* These two fields directly represent the contents of a JPEG DHT marker */
    /* bits[k] = # of symbols with codes of */
    /* length k bits; bits[0] is unused */
    pub bits: [u8; 17],
    /* The symbols, in order of incr code length */
    pub huff_vals: [u8; 256],
}

pub const NUM_HUFF_TBLS: usize = 2;

#[derive(ConstDefault)]
pub struct HuffTbls {
    pub dc_huff_tbls: [HuffTbl; NUM_HUFF_TBLS],
    pub ac_huff_tbls: [HuffTbl; NUM_HUFF_TBLS],
}

const fn add_huff_table(tbl: &mut HuffTbl, bits: &[u8; 17], val: &[u8]) {
    tbl.bits = *bits;

    let mut nsymbols: usize = 0;
    let mut len = 1;
    loop {
        nsymbols += bits[len] as usize;
        len += 1;
        if len > 16 {
            break;
        }
    }
    if nsymbols < 1 || nsymbols > 256 {
        panic!();
    }

    let mut i = 0;
    loop {
        tbl.huff_vals[i] = val[i];
        i += 1;
        if i >= nsymbols {
            break;
        }
    }
    let mut i = nsymbols;
    loop {
        tbl.huff_vals[i] = 0;
        i += 1;
        if i >= 256 {
            break;
        }
    }
}

impl HuffTbls {
    pub const fn once(&mut self) {
        add_huff_table(
            &mut self.dc_huff_tbls[0],
            &BITS_DC_LUMINANCE,
            VAL_DC_LUMINANCE.as_slice(),
        );
        add_huff_table(
            &mut self.ac_huff_tbls[0],
            &BITS_AC_LUMINANCE,
            VAL_AC_LUMINANCE.as_slice(),
        );
        add_huff_table(
            &mut self.dc_huff_tbls[1],
            &BITS_DC_CHROMINANCE,
            VAL_DC_CHROMINANCE.as_slice(),
        );
        add_huff_table(
            &mut self.ac_huff_tbls[1],
            &BITS_AC_CHROMINANCE,
            VAL_AC_CHROMINANCE.as_slice(),
        );
    }

    #[cfg(not(feature = "o3ds"))]
    pub fn once_dq(&mut self) {
        let mut freq: [usize; 257] = const_default();

        {
            freq.fill(0);
            let dc_lum_freq = &mut freq;
            for i in 0..=12 {
                dc_lum_freq[i] = 12 + 1 - i;
            }
            gen_optimal_table(&mut self.dc_huff_tbls[0], dc_lum_freq);
        }
        {
            freq.fill(0);
            let dc_chrom_freq = &mut freq;
            for i in 0..=12 {
                dc_chrom_freq[i] = 12 + 1 - i;
            }
            gen_optimal_table(&mut self.dc_huff_tbls[1], dc_chrom_freq);
        }
        let fill_ac_freq = |freq: &mut [usize; 257], base: &[u8]| {
            let dq_freq = (0x0b..(0xfb + 1)).step_by(0x10);
            let count = base.len() + dq_freq.len();
            for (i, v) in base.iter().enumerate() {
                freq[*v as usize] = count - i;
            }
            let count = count - base.len();
            for (i, v) in dq_freq.enumerate() {
                freq[v as usize] = count - i;
            }
        };
        {
            freq.fill(0);
            let ac_lum_freq = &mut freq;
            fill_ac_freq(ac_lum_freq, &VAL_AC_LUMINANCE);
            gen_optimal_table(&mut self.ac_huff_tbls[0], ac_lum_freq);
        }

        {
            freq.fill(0);
            let ac_chrom_freq = &mut freq;
            fill_ac_freq(ac_chrom_freq, &VAL_AC_CHROMINANCE);

            gen_optimal_table(&mut self.ac_huff_tbls[1], ac_chrom_freq);
        }
    }
}

const MAXJSAMPLE: usize = 255;
pub const CENTERJSAMPLE: usize = 128;

pub const R_Y_OFF: usize = 0; /* offset to R => Y section */
pub const G_Y_OFF: usize = 1 * (MAXJSAMPLE + 1); /* offset to G => Y section */
pub const B_Y_OFF: usize = 2 * (MAXJSAMPLE + 1); /* etc. */
pub const R_CB_OFF: usize = 3 * (MAXJSAMPLE + 1);
pub const G_CB_OFF: usize = 4 * (MAXJSAMPLE + 1);
pub const B_CB_OFF: usize = 5 * (MAXJSAMPLE + 1);
pub const R_CR_OFF: usize = B_CB_OFF; /* B=>Cb, R=>Cr are the same */
pub const G_CR_OFF: usize = 6 * (MAXJSAMPLE + 1);
pub const B_CR_OFF: usize = 7 * (MAXJSAMPLE + 1);
pub const TABLE_SIZE: usize = 8 * (MAXJSAMPLE + 1);

#[derive(ConstDefault)]
pub struct ColorConvTabs {
    pub rgb_ycc_tab: [i32; TABLE_SIZE],
    #[cfg(not(feature = "o3ds"))]
    pub rb_5_tab: [u8; 1 << 5],
    #[cfg(not(feature = "o3ds"))]
    pub g_6_tab: [u8; 1 << 6],
    #[cfg(not(feature = "o3ds"))]
    pub rgb_4_tab: [u8; 1 << 4],
}

pub const SCALEBITS: usize = 16; /* speediest right-shift on some machines */
const CBCR_OFFSET: isize = (CENTERJSAMPLE as isize) << SCALEBITS;
const ONE_HALF: isize = (1 as isize) << (SCALEBITS - 1);
const fn fix(x: f64) -> isize {
    ((x) * ((1 as isize) << SCALEBITS) as f64 + 0.5) as isize
}

impl ColorConvTabs {
    pub const fn once(&mut self) {
        let mut i = 0;
        loop {
            self.rgb_ycc_tab[i + R_Y_OFF] = (fix(0.29900) * i as isize) as i32;
            self.rgb_ycc_tab[i + G_Y_OFF] = (fix(0.58700) * i as isize) as i32;
            self.rgb_ycc_tab[i + B_Y_OFF] = ((fix(0.11400) * i as isize) + ONE_HALF) as i32;
            self.rgb_ycc_tab[i + R_CB_OFF] = ((-fix(0.16874)) * i as isize) as i32;
            self.rgb_ycc_tab[i + G_CB_OFF] = ((-fix(0.33126)) * i as isize) as i32;
            /* We use a rounding fudge-factor of 0.5-epsilon for Cb and Cr.
             * This ensures that the maximum output will round to _MAXJSAMPLE
             * not _MAXJSAMPLE+1, and thus that we don't have to range-limit.
             */
            self.rgb_ycc_tab[i + B_CB_OFF] =
                ((fix(0.50000) * i as isize) + CBCR_OFFSET + ONE_HALF - 1) as i32;
            /* B=>Cb and R=>Cr tables are the same
             * rgb_ycc_tab[i + R_CR_OFF] = fix(0.50000) * i + CBCR_OFFSET + ONE_HALF - 1;
             */
            self.rgb_ycc_tab[i + G_CR_OFF] = ((-fix(0.41869)) * i as isize) as i32;
            self.rgb_ycc_tab[i + B_CR_OFF] = ((-fix(0.08131)) * i as isize) as i32;

            i += 1;
            if i > MAXJSAMPLE {
                break;
            }
        }

        #[cfg(not(feature = "o3ds"))]
        {
            let mut i = 0;
            loop {
                self.rb_5_tab[i] = ((fix((((1 << 8) - 1) as f64) / ((1 << 5) - 1) as f64)
                    * i as isize
                    + ONE_HALF as isize)
                    >> SCALEBITS) as u8;

                i += 1;
                if i >= (1 << 5) {
                    break;
                }
            }

            let mut i = 0;
            loop {
                self.g_6_tab[i] = ((fix((((1 << 8) - 1) as f64) / ((1 << 6) - 1) as f64)
                    * i as isize
                    + ONE_HALF as isize)
                    >> SCALEBITS) as u8;

                i += 1;
                if i >= (1 << 6) {
                    break;
                }
            }

            let mut i = 0;
            loop {
                self.rgb_4_tab[i] = ((fix((((1 << 8) - 1) as f64) / ((1 << 4) - 1) as f64)
                    * i as isize
                    + ONE_HALF as isize)
                    >> SCALEBITS) as u8;

                i += 1;
                if i >= (1 << 4) {
                    break;
                }
            }
        }
    }
}

#[derive(ConstDefault)]
pub struct CompInfo {
    /* These values are fixed over the whole image. */
    /* For compression, they must be supplied by parameter setup; */
    /* for decompression, they are read from the SOF marker. */
    pub component_id: u8,  /* identifier for this component (0..255) */
    pub h_samp_factor: u8, /* horizontal sampling factor (1..4) */
    pub v_samp_factor: u8, /* vertical sampling factor (1..4) */
    pub h_samp_exp: u8,    /* horizontal sampling factor (1..4) */
    pub v_samp_exp: u8,    /* vertical sampling factor (1..4) */
    pub quant_tbl_no: u8,  /* quantization table selector (0..3) */
    /* These values may vary between scans. */
    /* For compression, they must be supplied by parameter setup; */
    /* for decompression, they are read from the SOS marker. */
    /* The decompressor output side may not use these variables. */
    pub dc_tbl_no: u8, /* DC entropy table selector (0..3) */
    pub ac_tbl_no: u8, /* AC entropy table selector (0..3) */
}

pub const MAX_COMPONENTS: usize = 3;

#[derive(ConstDefault)]
pub struct CompInfos {
    pub infos: [CompInfo; MAX_COMPONENTS],
}

impl CompInfos {
    const fn set_comp(
        &mut self,
        index: usize,
        id: u8,
        h_samp_exp: u8,
        v_samp_exp: u8,
        quant: u8,
        dctbl: u8,
        actbl: u8,
    ) {
        let comp = &mut self.infos[index];
        comp.component_id = id;
        comp.h_samp_exp = h_samp_exp;
        comp.v_samp_exp = v_samp_exp;
        comp.h_samp_factor = unsafe { core::intrinsics::unchecked_shl(1, h_samp_exp) };
        comp.v_samp_factor = unsafe { core::intrinsics::unchecked_shl(1, v_samp_exp) };
        comp.quant_tbl_no = quant;
        comp.dc_tbl_no = dctbl;
        comp.ac_tbl_no = actbl;
    }

    pub const fn set_color_space_ycb_cr_420(&mut self) {
        self.set_comp(0, 1, 1, 1, 0, 0, 0);
        self.set_comp(1, 2, 0, 0, 1, 1, 1);
        self.set_comp(2, 3, 0, 0, 1, 1, 1);
    }

    pub const fn set_color_space_ycb_cr_422(&mut self) {
        self.set_comp(0, 1, 1, 0, 0, 0, 0);
        self.set_comp(1, 2, 0, 0, 1, 1, 1);
        self.set_comp(2, 3, 0, 0, 1, 1, 1);
    }

    pub const fn set_color_space_ycb_cr_444(&mut self) {
        self.set_comp(0, 1, 0, 0, 0, 0, 0);
        self.set_comp(1, 2, 0, 0, 1, 1, 1);
        self.set_comp(2, 3, 0, 0, 1, 1, 1);
    }
}

fn quality_scaling(quality: u32) -> u32
/* Convert a user-specified quality rating to a percentage scaling factor
 * for an underlying quantization table, using our recommended scaling curve.
 * The input 'quality' factor should be 0 (terrible) to 100 (very good).
 */
{
    /* Safety limit on quality factor.  Convert 0 to 1 to avoid zero divide. */
    let quality = if quality <= 0 {
        1
    } else if quality > 100 {
        100
    } else {
        quality
    };

    /* The basic table is used as-is (scaling 100) for a quality of 50.
     * Qualities 50..100 are converted to scaling percentage 200 - 2*Q;
     * note that at Q=100 the scaling is 0, which will cause jpeg_add_quant_table
     * to make all the table entries 1 (hence, minimum quantization loss).
     * Qualities 1..50 are converted to scaling percentage 5000/Q.
     */
    if quality < 50 {
        5000 / quality
    } else {
        200 - quality * 2
    }
}

/* These are the sample quantization tables given in Annex K (Clause K.1) of
 * Recommendation ITU-T T.81 (1992) | ISO/IEC 10918-1:1994.
 * The spec says that the values given produce "good" quality, and
 * when divided by 2, "very good" quality.
 */

pub const STD_LUMINANCE_QUANT_TBL: [u8; DCTSIZE2] = [
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113,
    92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99,
];

pub const STD_CHROMINANCE_QUANT_TBL: [u8; DCTSIZE2] = [
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
];

#[derive(ConstDefault, Clone, Copy)]
pub struct QuantTbl {
    /* This array gives the coefficient quantizers in natural array order
     * (not the zigzag order in which they are stored in a JPEG DQT marker).
     * CAUTION: IJG versions prior to v6a kept this array in zigzag order.
     */
    /* quantization step for each coefficient */
    pub quant_val: [u8; DCTSIZE2],
}

pub const NUM_QUANT_TBLS: usize = 2;

#[derive(ConstDefault, Clone, Copy)]
pub struct QuantTbls {
    pub quant_tbls: [QuantTbl; NUM_QUANT_TBLS],
}

impl QuantTbls {
    fn set_quant_table(
        &mut self,
        which_tbl: usize,
        basic_table: &[u8; DCTSIZE2],
        scale_factor: u32,
    ) {
        let tbl = &mut self.quant_tbls[which_tbl];
        for i in 0..DCTSIZE2 {
            let temp = ((basic_table[i] as u32) * scale_factor + 50) / 100;
            /* limit the values to the valid range */
            let temp = if temp <= 0 {
                1
            } else if temp > 255 {
                255
            } else {
                temp
            };
            tbl.quant_val[i] = temp as u8;
        }
    }

    fn set_linear_quality(&mut self, scale_factor: u32) {
        self.set_quant_table(0, &STD_LUMINANCE_QUANT_TBL, scale_factor);
        self.set_quant_table(1, &STD_CHROMINANCE_QUANT_TBL, scale_factor);
    }

    pub fn set_quality(&mut self, quality: u32)
    /* Set or change the 'quality' (quantization) setting, using default tables.
     * This is the standard quality-adjusting entry point for typical user
     * interfaces; only those who want detailed control over quantization tables
     * would use the preceding three routines directly.
     */
    {
        /* Convert user 0-100 rating to percentage scaling */
        let quality = quality_scaling(quality);

        /* Set up standard quality tables */
        self.set_linear_quality(quality);
    }
}

#[derive(ConstDefault, Clone, Copy)]
pub struct DivisorPart {
    pub recip: i16,
    pub corr: i16,
}

#[derive(ConstDefault, Clone, Copy)]
pub struct Divisors {
    pub divisors: [[DivisorPart; DCTSIZE2]; NUM_QUANT_TBLS],
}

const ONE: u32 = 1;
fn right_shift(x: u32, shft: u32) -> u32 {
    x >> shft
}
fn descale(x: u32, n: u32) -> u16 {
    right_shift(x + (ONE << (n - 1)), n) as u16
}

fn flss(mut val: u16) -> u32 {
    let mut bit;

    bit = 16;

    if val == 0 {
        0
    } else {
        if (val & 0xff00) == 0 {
            bit -= 8;
            val <<= 8;
        }
        if (val & 0xf000) == 0 {
            bit -= 4;
            val <<= 4;
        }
        if (val & 0xc000) == 0 {
            bit -= 2;
            val <<= 2;
        }
        #[allow(unused_assignments)]
        if (val & 0x8000) == 0 {
            bit -= 1;
            val <<= 1;
        }

        bit
    }
}

fn compute_reciprocal(divisor: u16, div_part: &mut DivisorPart, shift: &mut u8) {
    if divisor == 1 {
        /* divisor == 1 means unquantized, so these reciprocal/correction/shift
         * values will cause the C quantization algorithm to act like the
         * identity function.  Since only the C quantization algorithm is used in
         * these cases, the scale value is irrelevant.
         */
        div_part.recip = 1; /* reciprocal */
        div_part.corr = 0; /* correction */
        *shift = 0; /* shift */
        return;
    }

    let b: u32 = flss(divisor);
    let mut r: u32 = mem::size_of::<i16>() as u32 * 8 + b - 1;

    let divisor = divisor as u32;

    let mut fq = ((1 as u32) << r) / divisor;
    let fr = ((1 as u32) << r) % divisor;

    let mut c = divisor / 2; /* for rounding */

    if fr == 0 {
        /* divisor is power of two */
        /* fq will be one bit too large to fit in DCTELEM, so adjust */
        fq >>= 1;
        r -= 1;
    } else if fr <= (divisor / 2) {
        /* fractional part is < 0.5 */
        c += 1;
    } else {
        /* fractional part is > 0.5 */
        fq += 1;
    }

    div_part.recip = fq as i16; /* reciprocal */
    div_part.corr = c as i16; /* correction + roundfactor */
    *shift = r as u8; /* shift */
}

impl Divisors {
    pub fn set_divisors(
        &mut self,
        quant_tbls: &QuantTbls,
        div_shifts: &mut [[u8; DCTSIZE2]; NUM_QUANT_TBLS],
    ) {
        for t in 0..NUM_QUANT_TBLS {
            let divisors = &mut self.divisors[t];
            let quant_tbl = &quant_tbls.quant_tbls[t];

            for i in 0..DCTSIZE2 {
                compute_reciprocal(
                    descale(
                        (quant_tbl.quant_val[i] as u32) * (AANSCALES[i] as u32),
                        CONST_BITS as u32 - 3,
                    ),
                    &mut divisors[i],
                    &mut div_shifts[t][i],
                );
            }
        }
    }
}

#[derive(ConstDefault)]
pub struct DerivedTbl {
    pub ehufco: [u32; 256], /* code for each symbol */
    pub ehufsi: [u8; 256],  /* length of code for each symbol */
                            /* If no code has been allocated for a symbol S, ehufsi[S] contains 0 */
}

#[derive(ConstDefault)]
pub struct EntropyTbls {
    pub dc_derived_tbls: [DerivedTbl; NUM_HUFF_TBLS],
    pub ac_derived_tbls: [DerivedTbl; NUM_HUFF_TBLS],
}

const fn do_set_derived_tbl(tbl: &mut DerivedTbl, htbl: &HuffTbl, maxsymbol: u8) {
    /* Figure C.1: make table of Huffman code length for each symbol */

    let mut p: usize = 0;
    let mut huffsize: [u8; 257] = const_default();
    let mut l = 1;
    loop {
        let mut i = htbl.bits[l] as usize;
        if p + i > 256
        /* protect against table overrun */
        {
            panic!();
        }
        while i > 0 {
            i -= 1;
            huffsize[p] = l as u8;
            p += 1;
        }

        l += 1;
        if l > 16 {
            break;
        }
    }
    huffsize[p] = 0;
    let lastp = p;

    /* Figure C.2: generate the codes themselves */
    /* We also validate that the counts represent a legal Huffman code tree. */

    let mut code: u32 = 0;
    let mut si = huffsize[0];
    p = 0;
    let mut huffcode: [u32; 257] = const_default();
    while huffsize[p] > 0 {
        while huffsize[p] == si {
            huffcode[p] = code;
            p += 1;
            code += 1;
        }
        /* code is now 1 more than the last code used for codelength si; but
         * it must still fit in si bits, since no code is allowed to be all ones.
         */
        if code >= (1 << si) {
            panic!();
        }
        code <<= 1;
        si += 1;
    }

    /* Figure C.3: generate encoding tables */
    /* These are code and size indexed by symbol value */

    /* Set all codeless symbols to have code length 0;
     * this lets us detect duplicate VAL entries here, and later
     * allows emit_bits to detect any attempt to emit such symbols.
     */
    tbl.ehufco = const_default();
    tbl.ehufsi = const_default();

    /* This is also a convenient place to check for out-of-range and duplicated
     * VAL entries.  We allow 0..255 for AC symbols but only 0..15 for DC in
     * lossy mode and 0..16 for DC in lossless mode.  (We could constrain them
     * further based on data depth and mode, but this seems enough.)
     */

    let mut p = 0;
    loop {
        let i = htbl.huff_vals[p];
        if i > maxsymbol || tbl.ehufsi[i as usize] > 0 {
            panic!();
        }
        tbl.ehufco[i as usize] = huffcode[p];
        tbl.ehufsi[i as usize] = huffsize[p];
        p += 1;
        if p >= lastp {
            break;
        }
    }
}

const fn set_derived_tbl(
    tbl: &mut DerivedTbl,
    is_dc: bool,
    tblno: usize,
    huff_tbls: &HuffTbls,
    is_dq: bool,
) {
    /* Note that huffsize[] and huffcode[] are filled in code-length order,
     * paralleling the order of the symbols themselves in htbl->huffval[].
     */

    /* Find the input Huffman table */
    let htbl = if is_dc {
        &huff_tbls.dc_huff_tbls[tblno]
    } else {
        &huff_tbls.ac_huff_tbls[tblno]
    };

    do_set_derived_tbl(
        tbl,
        htbl,
        if is_dc {
            if is_dq { 16 } else { 15 }
        } else {
            255
        },
    );
}

impl EntropyTbls {
    pub const fn set_entropy_tbls(&mut self, huff_tbls: &HuffTbls, is_dq: bool) {
        let mut i = 0;
        loop {
            set_derived_tbl(&mut self.dc_derived_tbls[i], true, i, huff_tbls, is_dq);
            set_derived_tbl(&mut self.ac_derived_tbls[i], false, i, huff_tbls, is_dq);

            i += 1;
            if i >= NUM_HUFF_TBLS {
                break;
            }
        }
    }
}

pub const SAMP_FACTOR: usize = 2;

pub type JCoef = i16;
pub type JBlock = [JCoef; DCTSIZE2];
pub const MAX_BLOCKS_IN_MCU: usize = SAMP_FACTOR * SAMP_FACTOR + 1 + 1;

pub const DOWNSAMPLE_FACTOR: usize = 2;

pub const fn downsample_quality_scale_factor(downsample: u8) -> u32 {
    match downsample {
        RP_DOWNSAMPLE_QUARTER => 4,
        RP_DOWNSAMPLE_CHECKER | RP_DOWNSAMPLE_EVEN_ODD => 2,
        _ => 1,
    }
}

pub const fn downsample_quality_scale(downsample: u8, quality: u32) -> u32 {
    let factor = downsample_quality_scale_factor(downsample);
    100 - (100 - quality + factor / 2) / factor
}

pub const fn downsample_screen_width(downsample: u8) -> usize {
    match downsample {
        RP_DOWNSAMPLE_QUARTER | RP_DOWNSAMPLE_EVEN_ODD => {
            GSP_SCREEN_WIDTH as usize / DOWNSAMPLE_FACTOR
        }
        _ => GSP_SCREEN_WIDTH as usize,
    }
}

pub const fn downsample_screen_height(downsample: u8, is_top: bool) -> usize {
    let height = if is_top {
        GSP_SCREEN_HEIGHT_TOP as usize
    } else {
        GSP_SCREEN_HEIGHT_BOTTOM as usize
    };
    match downsample {
        RP_DOWNSAMPLE_QUARTER => height / DOWNSAMPLE_FACTOR,
        RP_DOWNSAMPLE_EVEN_ODD | _ => height,
    }
}

#[cfg(not(feature = "mem3"))]
pub const fn downsample_checker_screen_dim(is_top: bool) -> usize {
    let height = if is_top {
        GSP_SCREEN_HEIGHT_TOP as usize
    } else {
        GSP_SCREEN_HEIGHT_BOTTOM as usize
    };
    (GSP_SCREEN_WIDTH as usize + height) / DOWNSAMPLE_FACTOR
}

pub union WorkerColorBufDownsample {
    pub full: [u8; downsample_screen_width(RP_DOWNSAMPLE_NONE) * SAMP_FACTOR],
    pub even_odd: [u8; downsample_screen_width(RP_DOWNSAMPLE_EVEN_ODD) * SAMP_FACTOR],
}

pub struct WorkerColorBuf {
    pub buf: WorkerColorBufDownsample,
    pub ptr: *mut u8,
}

#[derive(Clone, Copy)]
pub struct WorkerBufComps<const SIZE: usize> {
    pub buf: [u8; SIZE],
}

const fn worker_buf_comps_size(width: usize) -> usize {
    let no_v_no_h = width * DCTSIZE * MAX_COMPONENTS;
    let no_v_h = width * DCTSIZE + width / SAMP_FACTOR * DCTSIZE * (MAX_COMPONENTS - 1);
    let v_h = width * SAMP_FACTOR * DCTSIZE + width / SAMP_FACTOR * DCTSIZE * (MAX_COMPONENTS - 1);
    assert!(v_h > no_v_h);
    assert!(v_h == no_v_no_h);
    v_h
}

impl<const SIZE: usize> WorkerBufComps<SIZE> {
    #[inline(always)]
    pub fn get(&self, ci: CompIndex, h_samp: bool, v_samp: bool) -> &[u8] {
        match (h_samp, v_samp) {
            (true, true) => {
                let width_dct = SIZE / MAX_COMPONENTS;
                let y_size = width_dct * SAMP_FACTOR;
                let u_v_size = (SIZE - y_size) / (MAX_COMPONENTS - 1);
                match ci.get() {
                    2 => &self.buf[y_size + u_v_size..SIZE],
                    1 => &self.buf[y_size..y_size + u_v_size],
                    _ => &self.buf[0..y_size],
                }
            }
            (true, false) => {
                let width_dct = SIZE / MAX_COMPONENTS;
                let y_size = width_dct;
                let u_v_size = width_dct / SAMP_FACTOR;
                match ci.get() {
                    2 => &self.buf[y_size + u_v_size..y_size + u_v_size * 2],
                    1 => &self.buf[y_size..y_size + u_v_size],
                    _ => &self.buf[0..y_size],
                }
            }
            (false, true) => panic!(),
            (false, false) => {
                let width_dct: usize = SIZE / MAX_COMPONENTS;
                let ci = ci.get() as usize;
                unsafe { self.buf.get_unchecked(width_dct * ci..width_dct * (ci + 1)) }
            }
        }
    }

    #[inline(always)]
    pub fn get_mut(&mut self, ci: CompIndex, h_samp: bool, v_samp: bool) -> &mut [u8] {
        match (h_samp, v_samp) {
            (true, true) => {
                let width_dct = SIZE / MAX_COMPONENTS;
                let y_size = width_dct * SAMP_FACTOR;
                let u_v_size = (SIZE - y_size) / (MAX_COMPONENTS - 1);
                match ci.get() {
                    2 => &mut self.buf[y_size + u_v_size..SIZE],
                    1 => &mut self.buf[y_size..y_size + u_v_size],
                    _ => &mut self.buf[0..y_size],
                }
            }
            (true, false) => {
                let width_dct = SIZE / MAX_COMPONENTS;
                let y_size = width_dct;
                let u_v_size = width_dct / SAMP_FACTOR;
                match ci.get() {
                    2 => &mut self.buf[y_size + u_v_size..y_size + u_v_size * 2],
                    1 => &mut self.buf[y_size..y_size + u_v_size],
                    _ => &mut self.buf[0..y_size],
                }
            }
            (false, true) => panic!(),
            (false, false) => {
                let width_dct: usize = SIZE / MAX_COMPONENTS;
                let ci = ci.get() as usize;
                unsafe {
                    self.buf
                        .get_unchecked_mut(width_dct * ci..width_dct * (ci + 1))
                }
            }
        }
    }
}

pub type CompIndex = Ranged<{ MAX_COMPONENTS as u32 }>;

#[derive(Clone, Copy)]
pub struct WorkerPrepBufDownsampleQuarter {
    pub buf:
        WorkerBufComps<{ worker_buf_comps_size(downsample_screen_width(RP_DOWNSAMPLE_QUARTER)) }>,
    pub prep:
        [[u8; downsample_screen_width(RP_DOWNSAMPLE_QUARTER) * DOWNSAMPLE_FACTOR]; MAX_COMPONENTS],
}

pub union WorkerPrepBufDownsample {
    pub full:
        WorkerBufComps<{ worker_buf_comps_size(downsample_screen_width(RP_DOWNSAMPLE_NONE)) }>,
    pub quarter: WorkerPrepBufDownsampleQuarter,
    pub even_odd:
        WorkerBufComps<{ worker_buf_comps_size(downsample_screen_width(RP_DOWNSAMPLE_EVEN_ODD)) }>,
}

#[derive(Clone, Copy)]
pub struct JpegWorkerData {
    pub mcu: [JBlock; MAX_BLOCKS_IN_MCU],
}

#[derive(Clone, Copy)]
pub struct LosslessWorkerData {
    pub ptr: *const u8,
}

pub union WorkerData {
    pub jpeg: JpegWorkerData,
    pub lossless: LosslessWorkerData,
}

pub struct WorkerBufs {
    pub color: [WorkerColorBuf; MAX_COMPONENTS],
    pub prep: WorkerPrepBufDownsample,
    pub data: WorkerData,
}

#[derive(Default, Clone, Copy)]
pub enum ColorSpace {
    #[default]
    RGBA8,
    RGB8,
    RGB565,
    RGB5A1,
    RGB4,
}

pub const M_SOI: u8 = 0xd8;
pub const M_APP0: u8 = 0xe0;
pub const M_DQT: u8 = 0xdb;
pub const M_SOF0: u8 = 0xc0;
pub const M_DHT: u8 = 0xc4;
#[cfg(not(feature = "o3ds"))]
pub const M_DRI: u8 = 0xdd;
pub const M_SOS: u8 = 0xda;
pub const M_EOI: u8 = 0xd9;
#[cfg(not(feature = "o3ds"))]
pub const M_RST0: u8 = 0xd0;

/*
 * jpeg_natural_order[i] is the natural-order position of the i'th element
 * of zigzag order.
 *
 * When reading corrupted data, the Huffman decoders could attempt
 * to reference an entry beyond the end of this array (if the decoded
 * zero run length reaches past the end of the block).  To prevent
 * wild stores without adding an inner-loop test, we put some extra
 * "63"s after the real entries.  This will cause the extra coefficient
 * to be stored in location 63 of the block, not somewhere random.
 * The worst case would be a run-length of 15, which means we need 16
 * fake entries.
 */

#[cfg(not(feature = "o3ds"))]
pub enum LosslessTblName {
    Tbl8,
    Tbl6,
    Tbl5,
    Tbl4,
    TblCount,
}

#[cfg(not(feature = "o3ds"))]
pub const LOSSLESS_TBL_COUNT: usize = LosslessTblName::TblCount as usize;

#[cfg(not(feature = "o3ds"))]
pub const fn lossless_tbl_name_from_bits(bits: u8) -> LosslessTblName {
    match bits {
        8 => LosslessTblName::Tbl8,
        6 => LosslessTblName::Tbl6,
        5 => LosslessTblName::Tbl5,
        4 => LosslessTblName::Tbl4,
        _ => panic!(),
    }
}

#[derive(ConstDefault)]
pub struct EncodeTbls {
    pub huff_tbls: HuffTbls,
    pub entropy_tbls: EntropyTbls,
    #[cfg(not(feature = "o3ds"))]
    pub dq_huff_tbls: HuffTbls,
    #[cfg(not(feature = "o3ds"))]
    pub dq_entropy_tbls: EntropyTbls,
    #[cfg(not(feature = "o3ds"))]
    pub lossless_huff_tbl: [HuffTbl; LOSSLESS_TBL_COUNT],
    #[cfg(not(feature = "o3ds"))]
    pub lossless_entropy_tbl: [DerivedTbl; LOSSLESS_TBL_COUNT],
    pub color_conv_tbls: ColorConvTabs,
    pub comp_infos_420: CompInfos,
    pub comp_infos_422: CompInfos,
    pub comp_infos_444: CompInfos,
}

impl EncodeTbls {
    pub fn once() -> Self {
        let mut tbls: Self = const_default();
        tbls.huff_tbls.once();
        tbls.entropy_tbls.set_entropy_tbls(&tbls.huff_tbls, false);
        #[cfg(not(feature = "o3ds"))]
        tbls.dq_huff_tbls.once_dq();
        #[cfg(not(feature = "o3ds"))]
        tbls.dq_entropy_tbls
            .set_entropy_tbls(&tbls.dq_huff_tbls, true);
        #[cfg(not(feature = "o3ds"))]
        {
            const P: f32 = core::f32::consts::SQRT_2;
            let mut freq: [usize; 257] = const_default();
            let mut set_tbl = |bits| {
                freq.fill(0);
                let end = 1 << (bits as usize - 1);
                for i in 0..=end {
                    let i_pos = 128 + i;
                    let i_neg = 128 - i;
                    let count = (if i < 24 {
                        unsafe { powf(P, (24 - i) as f32) * 2f32 }
                    } else {
                        2f32
                    } * if i == 0 {
                        unsafe { powf(P, (8 - bits + 1) as f32) }
                    } else {
                        1f32
                    }) as usize;
                    freq[i_pos] = count;
                    freq[i_neg] = count;
                }
                freq[128 + end] = 0;

                let name = lossless_tbl_name_from_bits(bits) as usize;
                gen_optimal_table(&mut tbls.lossless_huff_tbl[name], &mut freq);
                do_set_derived_tbl(
                    &mut tbls.lossless_entropy_tbl[name],
                    &tbls.lossless_huff_tbl[name],
                    255,
                );
            };

            let bits = [8, 6, 5, 4];

            for bits in bits {
                set_tbl(bits);
            }
        }
        tbls.color_conv_tbls.once();
        tbls.comp_infos_420.set_color_space_ycb_cr_420();
        tbls.comp_infos_422.set_color_space_ycb_cr_422();
        tbls.comp_infos_444.set_color_space_ycb_cr_444();
        tbls
    }
}

pub const JPEG_NATURAL_ORDER: [u8; DCTSIZE2] = [
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20,
    13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59,
    52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
];

#[cfg(not(feature = "o3ds"))]
pub fn gen_optimal_table(tbl: &mut HuffTbl, freq: &mut [usize; 257]) {
    #![allow(unused_assignments)]

    const MAX_C_LEN: usize = 32; /* assumed maximum initial code length */
    let mut bits: [u8; MAX_C_LEN + 1] = const_default(); /* bits[k] = # of symbols with code length k */
    let mut bit_pos: [isize; MAX_C_LEN + 1] = const_default(); /* # of symbols with smaller code length */
    let mut codesize: [isize; 257] = const_default(); /* codesize[k] = code length of symbol k */
    let mut nz_index: [isize; 257] = const_default(); /* index of nonzero symbol in the original freq
    array */
    let mut others: [isize; 257] = const_default(); /* next symbol in current branch of tree */
    let (mut c1, mut c2): (isize, isize) = const_default();
    let (mut p, mut i, mut j): (isize, isize, isize) = const_default();
    let mut num_nz_symbols: isize = const_default();
    let (mut v, mut v2): (usize, usize) = const_default();

    /* This algorithm is explained in section K.2 of the JPEG standard */

    for i in 0..257 {
        others[i] = -1; /* init links to empty */
    }

    freq[256] = 1; /* make sure 256 has a nonzero count */
    /* Including the pseudo-symbol 256 in the Huffman procedure guarantees
     * that no real symbol is given code-value of all ones, because 256
     * will be placed last in the largest codeword category.
     */

    /* Group nonzero frequencies together so we can more easily find the
     * smallest.
     */
    num_nz_symbols = 0;
    for i in 0..257 {
        if freq[i] > 0 {
            nz_index[num_nz_symbols as usize] = i as isize;
            freq[num_nz_symbols as usize] = freq[i];
            num_nz_symbols += 1;
        }
    }

    /* Huffman's basic algorithm to assign optimal code lengths to symbols */

    loop {
        /* Find the two smallest nonzero frequencies; set c1, c2 = their symbols */
        /* In case of ties, take the larger symbol number.  Since we have grouped
         * the nonzero symbols together, checking for zero symbols is not
         * necessary.
         */
        c1 = -1;
        c2 = -1;
        v = 1000000000;
        v2 = 1000000000;
        for i in 0..num_nz_symbols {
            if freq[i as usize] <= v2 {
                if freq[i as usize] <= v {
                    c2 = c1;
                    v2 = v;
                    v = freq[i as usize];
                    c1 = i;
                } else {
                    v2 = freq[i as usize];
                    c2 = i;
                }
            }
        }

        /* Done if we've merged everything into one frequency */
        if c2 < 0 {
            break;
        }

        /* Else merge the two counts/trees */
        freq[c1 as usize] += freq[c2 as usize];
        /* Set the frequency to a very high value instead of zero, so we don't have
         * to check for zero values.
         */
        freq[c2 as usize] = 1000000001;

        /* Increment the codesize of everything in c1's tree branch */
        codesize[c1 as usize] += 1;
        while others[c1 as usize] >= 0 {
            c1 = others[c1 as usize];
            codesize[c1 as usize] += 1;
        }

        others[c1 as usize] = c2; /* chain c2 onto c1's tree branch */

        /* Increment the codesize of everything in c2's tree branch */
        codesize[c2 as usize] += 1;
        while others[c2 as usize] >= 0 {
            c2 = others[c2 as usize];
            codesize[c2 as usize] += 1;
        }
    }

    /* Now count the number of symbols of each code length */
    for i in 0..num_nz_symbols {
        /* The JPEG standard seems to think that this can't happen, */
        /* but I'm paranoid... */
        if codesize[i as usize] > MAX_C_LEN as isize {
            panic!();
        }

        bits[codesize[i as usize] as usize] += 1;
    }

    /* Count the number of symbols with a length smaller than i bits, so we can
     * construct the symbol table more efficiently.  Note that this includes the
     * pseudo-symbol 256, but since it is the last symbol, it will not affect the
     * table.
     */
    p = 0;
    for i in 0..=MAX_C_LEN {
        bit_pos[i] = p;
        p += bits[i] as isize;
    }

    /* JPEG doesn't allow symbols with code lengths over 16 bits, so if the pure
     * Huffman procedure assigned any such lengths, we must adjust the coding.
     * Here is what Rec. ITU-T T.81 | ISO/IEC 10918-1 says about how this next
     * bit works: Since symbols are paired for the longest Huffman code, the
     * symbols are removed from this length category two at a time.  The prefix
     * for the pair (which is one bit shorter) is allocated to one of the pair;
     * then, skipping the BITS entry for that prefix length, a code word from the
     * next shortest nonzero BITS entry is converted into a prefix for two code
     * words one bit longer.
     */

    for i in (17..=MAX_C_LEN).rev() {
        while bits[i] > 0 {
            j = i as isize - 2; /* find length of new prefix to be used */
            while bits[j as usize] == 0 {
                j -= 1;
            }

            bits[i] -= 2; /* remove two symbols */
            bits[i - 1] += 1; /* one goes in this length */
            bits[j as usize + 1] += 2; /* two new symbols in this length */
            bits[j as usize] -= 1; /* symbol of this length is now a prefix */
        }
    }

    i = 16;
    /* Remove the count for the pseudo-symbol 256 from the largest codelength */
    while bits[i as usize] == 0
    /* find largest codelength still in use */
    {
        i -= 1;
    }
    bits[i as usize] -= 1;

    /* Return final symbol counts (only for lengths 0..16) */
    tbl.bits = bits[0..tbl.bits.len()].try_into().unwrap();

    /* Return a list of the symbols sorted by code length */
    /* It's not real clear to me why we don't need to consider the codelength
     * changes made above, but Rec. ITU-T T.81 | ISO/IEC 10918-1 seems to think
     * this works.
     */
    for i in 0..(num_nz_symbols - 1) {
        tbl.huff_vals[bit_pos[codesize[i as usize] as usize] as usize] = nz_index[i as usize] as u8;
        bit_pos[codesize[i as usize] as usize] += 1;
    }
}

pub static mut ENCODER: *mut Encoder = const_default();

pub fn jpeg_get_params(
    quality: u32,
) -> (
    [u32; SCREEN_COUNT as usize],
    [u32; SCREEN_COUNT as usize],
    [u32; SCREEN_COUNT as usize],
) {
    let chroma_ss = [
        RP_CONFIG
            .chroma_ss(ScreenIndex::init(RP_SCREEN_TOP as u32))
            .load(Ordering::Acquire),
        RP_CONFIG
            .chroma_ss(ScreenIndex::init(RP_SCREEN_BOT as u32))
            .load(Ordering::Acquire),
    ];
    let downsample = [
        RP_CONFIG
            .downsample(ScreenIndex::init(RP_SCREEN_TOP as u32))
            .load(Ordering::Acquire),
        RP_CONFIG
            .downsample(ScreenIndex::init(RP_SCREEN_BOT as u32))
            .load(Ordering::Acquire),
    ];
    let quality = [
        encoder::downsample_quality_scale(downsample[RP_SCREEN_TOP as usize] as u8, quality),
        encoder::downsample_quality_scale(downsample[RP_SCREEN_BOT as usize] as u8, quality),
    ];

    (chroma_ss, downsample, quality)
}

#[named]
pub unsafe fn jpeg_update_quality(q: u32) {
    let encoder = unsafe { &mut *ENCODER };

    let (chroma_ss, _, quality) = jpeg_get_params(q);

    encoder
        .jpeg_shared
        .init(quality, chroma_ss, &mut encoder.shared);

    ns_dbg_print!(val, c_str!("JPEG quality updated"), q as s32);
}

#[named]
#[allow(unused)]
pub unsafe fn jpeg_quality_work_acquire() -> Option<()> {
    #[cfg(not(feature = "o3ds"))]
    {
        let encoder = unsafe { &*ENCODER };
        let shared = &encoder.jpeg_shared;

        if shared.quality_need_update.load(Ordering::Acquire) {
            wait_syn(
                cname!(),
                shared.quality_done_update,
                c_str!("quality_done_update"),
            )?;
        }
        wait_syn(
            cname!(),
            shared.quality_can_update,
            c_str!("quality_can_update"),
        )?;
    }
    Some(())
}

#[named]
#[allow(unused)]
pub unsafe fn jpeg_quality_work_release() {
    #[cfg(not(feature = "o3ds"))]
    {
        let encoder = unsafe { &*ENCODER };
        let shared = &encoder.jpeg_shared;

        unsafe {
            release_sem(
                cname!(),
                shared.quality_can_update,
                c_str!("quality_can_update"),
            );
        }
    }
}

#[named]
pub unsafe fn jpeg_quality_update_acquire(#[allow(unused)] shared: &JpegShared) -> Option<()> {
    #[cfg(not(feature = "o3ds"))]
    {
        let res = unsafe { svcClearEvent(shared.quality_done_update) };
        if res != 0 {
            ns_dbg_print!(failed, c_str!("Clear quality_done_update failed"), res);
            set_reset_threads();
            return None;
        }
        shared.quality_need_update.store(true, Ordering::Release);
        for _ in 0..WORK_COUNT {
            wait_syn(
                cname!(),
                shared.quality_can_update,
                c_str!("quality_can_update"),
            )?;
        }
    }

    Some(())
}

#[named]
pub unsafe fn jpeg_quality_update_release(#[allow(unused)] shared: &JpegShared) -> Option<()> {
    #[cfg(not(feature = "o3ds"))]
    {
        unsafe {
            release_sem_count(
                cname!(),
                shared.quality_can_update,
                c_str!("quality_can_update"),
                WORK_COUNT as s32,
            );
        }
        shared.quality_need_update.store(false, Ordering::Release);
        let res = unsafe { svcSignalEvent(shared.quality_done_update) };
        if res != 0 {
            ns_dbg_print!(failed, c_str!("Signal quality_done_update failed"), res);
            set_reset_threads();
            return None;
        }
    }

    Some(())
}

pub const fn jdiv_round_up(a: usize, b: usize) -> usize
/* Compute a/b rounded up to next integer, ie, ceil(a/b) */
/* Assumes a >= 0, b > 0 */
{
    (a + b - 1) / b
}

pub const fn jround_up(a: usize, b: usize) -> usize
/* Compute a/b rounded up to next integer, ie, ceil(a/b) */
/* Assumes a >= 0, b > 0 */
{
    jdiv_round_up(a, b) * b
}
