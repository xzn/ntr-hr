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

pub fn downsample_screen_height(downsample: u8, is_top: bool) -> usize {
    let height = rp_screen_height!(is_top) as usize;
    match downsample {
        RP_DOWNSAMPLE_QUARTER => height / DOWNSAMPLE_FACTOR,
        RP_DOWNSAMPLE_EVEN_ODD | _ => height,
    }
}

#[cfg(not(feature = "mem3"))]
pub fn downsample_checker_screen_dim(is_top: bool) -> usize {
    let height = rp_screen_height!(is_top) as usize;
    (GSP_SCREEN_WIDTH as usize + height) / DOWNSAMPLE_FACTOR
}

#[cfg(not(feature = "mem3"))]
pub const fn downsample_checker_screen_dim_max(is_top: bool) -> usize {
    let height = rp_screen_height_max!(is_top) as usize;
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

#[cfg(not(feature = "o3ds"))]
const NOISE_BITS: usize = 4;

#[cfg(not(feature = "o3ds"))]
const NOISE_DIM: usize = 2usize.pow(NOISE_BITS as u32);

#[cfg(not(feature = "o3ds"))]
const C_DIM: usize = 2usize.pow(u8::BITS);

#[cfg(not(feature = "o3ds"))]
#[derive(ConstDefault)]
pub struct LosslessDitherTbl {
    pub t: [[u8; NOISE_DIM]; C_DIM],
}

#[cfg(not(feature = "o3ds"))]
#[derive(ConstDefault)]
pub struct LosslessNoiseTbl<const H_SAMP: bool, const V_SAMP: bool>
where
    [(); LosslessNoiseDim::<H_SAMP>::DIM]:,
    [(); LosslessNoiseDim::<V_SAMP>::DIM]:,
{
    pub n: [[u8; LosslessNoiseDim::<H_SAMP>::DIM]; LosslessNoiseDim::<V_SAMP>::DIM],
}

#[cfg(not(feature = "o3ds"))]
impl<const H_SAMP: bool, const V_SAMP: bool> LosslessNoiseTbl<H_SAMP, V_SAMP>
where
    [(); LosslessNoiseDim::<H_SAMP>::DIM]:,
    [(); LosslessNoiseDim::<V_SAMP>::DIM]:,
{
    const H_DIM: usize = LosslessNoiseDim::<H_SAMP>::DIM;
    const V_DIM: usize = LosslessNoiseDim::<V_SAMP>::DIM;

    const H_MASK: usize = Self::H_DIM - 1;
    const V_MASK: usize = Self::V_DIM - 1;
    const H_BITS: usize = Self::H_DIM.ilog2() as usize;

    pub fn params(&self) -> (*const u8, usize, usize, usize) {
        (
            unsafe { (*self.n.as_ptr()).as_ptr() },
            Self::H_MASK,
            Self::V_MASK,
            Self::H_BITS,
        )
    }
}

#[cfg(not(feature = "o3ds"))]
pub struct LosslessNoiseDim<const SAMP: bool> {}

#[cfg(not(feature = "o3ds"))]
impl<const SAMP: bool> LosslessNoiseDim<SAMP> {
    const DIM: usize = if SAMP {
        BLUE_NOISE_DIM / 2
    } else {
        BLUE_NOISE_DIM
    };
}

#[cfg(not(feature = "o3ds"))]
#[derive(ConstDefault)]
pub struct LosslessDitherTbls {
    pub t6: LosslessDitherTbl,
    pub t5: LosslessDitherTbl,
    pub t4: LosslessDitherTbl,
    pub n420: LosslessNoiseTbl<true, true>,
    pub n422: LosslessNoiseTbl<true, false>,
    pub n444: LosslessNoiseTbl<false, false>,
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
    #[cfg(not(feature = "o3ds"))]
    pub lossless_dither_tbls: LosslessDitherTbls,
    pub color_conv_tbls: ColorConvTabs,
    pub comp_infos_420: CompInfos,
    pub comp_infos_422: CompInfos,
    pub comp_infos_444: CompInfos,
}

#[cfg(not(feature = "o3ds"))]
impl LosslessDitherTbl {
    fn once(&mut self, b: usize) {
        let b_max = 2usize.pow(b as u32) - 1;
        let scale = b_max as f32;

        for i in 0..C_DIM {
            let f = i as f32 / (C_DIM - 1) as f32;

            for n in 0..NOISE_DIM {
                let d = n as f32 / (NOISE_DIM - 1) as f32;

                let o = unsafe { ctru::floorf(f * scale + d) };

                let o = (o as u32).clamp(0, b_max as u32) as s32;

                self.t[i][n] = o as u8;
            }
        }
    }
}

#[cfg(not(feature = "o3ds"))]
impl<const H_SAMP: bool, const V_SAMP: bool> LosslessNoiseTbl<H_SAMP, V_SAMP>
where
    [(); LosslessNoiseDim::<H_SAMP>::DIM]:,
    [(); LosslessNoiseDim::<V_SAMP>::DIM]:,
{
    fn once(&mut self) {
        for y in 0..Self::V_DIM {
            for x in 0..Self::H_DIM {
                let mut n = 0;
                let h_samp = if H_SAMP { 2 } else { 1 };
                let v_samp = if V_SAMP { 2 } else { 1 };

                for v in 0..v_samp {
                    for h in 0..h_samp {
                        n += blue_noise_get_fp(x * h_samp + h, y * v_samp + v);
                    }
                }

                n /= (h_samp * v_samp) as u16;

                self.n[y][x] = ((n + BLUE_NOISE_HALF) >> BLUE_NOISE_FRAC_B) as u8
                    >> (BLUE_NOISE_INT_B - NOISE_BITS);
            }
        }
    }
}

#[cfg(not(feature = "o3ds"))]
impl LosslessDitherTbls {
    fn once(&mut self) {
        self.t6.once(6);
        self.t5.once(5);
        self.t4.once(4);
        self.n420.once();
        self.n422.once();
        self.n444.once();
    }
}

impl EncodeTbls {
    pub fn once(&mut self) {
        *self = const_default();
        self.huff_tbls.once();
        self.entropy_tbls.set_entropy_tbls(&self.huff_tbls, false);
        #[cfg(not(feature = "o3ds"))]
        self.dq_huff_tbls.once_dq();
        #[cfg(not(feature = "o3ds"))]
        self.dq_entropy_tbls
            .set_entropy_tbls(&self.dq_huff_tbls, true);
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
                gen_optimal_table(&mut self.lossless_huff_tbl[name], &mut freq);
                do_set_derived_tbl(
                    &mut self.lossless_entropy_tbl[name],
                    &self.lossless_huff_tbl[name],
                    255,
                );
            };

            let bits = [8, 6, 5, 4];

            for bits in bits {
                set_tbl(bits);
            }

            self.lossless_dither_tbls.once();
        }
        self.color_conv_tbls.once();
        self.comp_infos_420.set_color_space_ycb_cr_420();
        self.comp_infos_422.set_color_space_ycb_cr_422();
        self.comp_infos_444.set_color_space_ycb_cr_444();
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
    [bool; SCREEN_COUNT as usize],
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
    let dither = [
        RP_CONFIG
            .no_dither(ScreenIndex::init(RP_SCREEN_TOP as u32))
            .load(Ordering::Acquire)
            == 0,
        RP_CONFIG
            .no_dither(ScreenIndex::init(RP_SCREEN_BOT as u32))
            .load(Ordering::Acquire)
            == 0,
    ];

    (chroma_ss, downsample, quality, dither)
}

#[named]
pub unsafe fn jpeg_update_quality(q: u32) {
    let encoder = unsafe { &mut *ENCODER };

    let (chroma_ss, _, quality, _) = jpeg_get_params(q);

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

#[cfg(not(feature = "o3ds"))]
fn blue_noise_get_fp(x: usize, y: usize) -> u16 {
    BLUE_NOISE_64_FP[y * BLUE_NOISE_DIM + x]
}

#[cfg(not(feature = "o3ds"))]
const BLUE_NOISE_DIM: usize = 64;
#[cfg(not(feature = "o3ds"))]
const BLUE_NOISE_SIZE: usize = BLUE_NOISE_DIM * BLUE_NOISE_DIM;

#[cfg(not(feature = "o3ds"))]
const BLUE_NOISE_INT_B: usize = 8;
#[cfg(not(feature = "o3ds"))]
const BLUE_NOISE_TOTAL_B: usize = 12;
#[cfg(not(feature = "o3ds"))]
const BLUE_NOISE_FRAC_B: usize = BLUE_NOISE_TOTAL_B - BLUE_NOISE_INT_B;

#[cfg(not(feature = "o3ds"))]
const BLUE_NOISE_HALF: u16 = 1u16 << (BLUE_NOISE_FRAC_B - 1);

#[cfg(not(feature = "o3ds"))]
/*
 * Generated with code from https://bartwronski.com/2021/04/21/superfast-void-and-cluster-blue-noise-in-python-numpy-jax/
 *
 * https://colab.research.google.com/github/bartwronski/BlogPostsExtraMaterial/blob/master/Jax_Void_and_cluster.ipynb
 *
 * ```
 * res = void_and_cluster(64)
 *
 * def f_to_fp(val):
 *   int_bits = 8
 *   total_bits = 12
 *   frac_bits = total_bits - int_bits
 *   max_val = ((1 << int_bits) - 1) << frac_bits
 *   scaled_val = round(val * max_val)
 *   return max(0, min(scaled_val, max_val))
 *
 * for y in range(0, 64):
 *   for x in range(0, 64):
 *     print(f_to_fp(res[x, y]), end=",")
 * ```
 */
pub const BLUE_NOISE_64_FP: [u16; BLUE_NOISE_SIZE] = [
    2737, 3298, 2446, 3157, 1967, 120, 1596, 2955, 1404, 3428, 1265, 3784, 2760, 280, 3869, 1283,
    2905, 1657, 3693, 1391, 3304, 895, 3616, 3431, 689, 2803, 3268, 1854, 2287, 1560, 325, 3068,
    2409, 36, 1705, 3131, 2450, 195, 2059, 522, 1180, 3320, 2568, 2078, 3706, 2321, 95, 1611, 3591,
    1908, 3083, 2392, 1203, 2067, 707, 359, 2777, 1539, 4070, 621, 3778, 3023, 494, 944, 3666,
    2232, 1766, 885, 423, 2543, 3701, 2265, 865, 2671, 100, 1043, 3196, 542, 3409, 1906, 49, 805,
    2390, 239, 2634, 2885, 135, 2338, 3061, 242, 2144, 816, 3904, 2877, 1102, 1890, 3284, 680,
    3872, 2193, 456, 1038, 2712, 3898, 1896, 1537, 3072, 449, 3368, 1445, 2735, 973, 2231, 1353,
    735, 3983, 102, 1771, 3184, 3735, 2154, 29, 1284, 2266, 3244, 1753, 2130, 1294, 740, 66, 3940,
    2966, 3575, 1226, 688, 3317, 1943, 3876, 1663, 2326, 2037, 1416, 2622, 2185, 4008, 3058, 3464,
    2055, 3919, 1869, 1544, 1137, 3837, 1648, 3668, 1433, 96, 3449, 1315, 2122, 2630, 3573, 1463,
    1168, 3748, 3011, 1401, 3567, 730, 69, 3827, 821, 1228, 1970, 4067, 3156, 462, 3405, 2910,
    1576, 2728, 3457, 2523, 1077, 1689, 2983, 3371, 781, 1068, 175, 2904, 3442, 1531, 2608, 1084,
    1438, 2091, 2718, 1739, 302, 525, 3040, 3505, 712, 3963, 2992, 872, 1140, 437, 1512, 1019, 661,
    1247, 394, 3216, 603, 1986, 2690, 418, 2516, 3140, 653, 3760, 443, 913, 238, 2869, 1957, 3338,
    860, 1775, 334, 2206, 2791, 2419, 1735, 2933, 284, 679, 2487, 1760, 192, 3840, 1996, 966, 587,
    1435, 3933, 477, 2428, 1912, 3699, 2696, 2477, 3993, 368, 3164, 1873, 499, 3425, 209, 3213,
    4062, 2421, 1166, 1466, 2524, 363, 1829, 157, 3587, 2449, 3742, 1805, 2706, 2318, 3662, 2986,
    2492, 3979, 1349, 886, 3379, 1156, 1919, 2376, 2757, 1597, 3962, 1758, 2507, 602, 110, 2601,
    2333, 3139, 3384, 1323, 1004, 3649, 2641, 3451, 1092, 3673, 2114, 863, 2585, 366, 3613, 2330,
    201, 2851, 874, 3541, 287, 1489, 541, 1659, 1190, 2012, 3619, 2842, 3793, 2260, 808, 1569, 990,
    2899, 2118, 3709, 940, 2749, 1272, 3292, 1621, 2818, 285, 3130, 3349, 117, 1693, 824, 2138,
    3480, 2, 2269, 2971, 4076, 1682, 993, 156, 3009, 3201, 2227, 3474, 1276, 4055, 1618, 3644,
    1116, 583, 3994, 2110, 174, 555, 2257, 1646, 1316, 2847, 3259, 1483, 1212, 3003, 3297, 2121,
    1837, 3141, 1248, 2086, 2802, 3205, 3813, 835, 2372, 620, 972, 1319, 2486, 3110, 1978, 3767,
    41, 3454, 0, 3161, 2236, 3802, 597, 2095, 756, 1347, 2018, 562, 3831, 1392, 2797, 282, 1052,
    1754, 2762, 588, 267, 3500, 2183, 3652, 1374, 725, 1055, 353, 2947, 2071, 759, 2809, 219, 1977,
    1593, 3007, 3269, 1437, 3939, 3095, 13, 3762, 624, 2276, 1892, 4036, 762, 1120, 3804, 2559,
    629, 4019, 986, 89, 2198, 3399, 228, 2683, 1713, 114, 3921, 401, 638, 1218, 2625, 1756, 771,
    1938, 1545, 2962, 1061, 2378, 4075, 3539, 1163, 2591, 948, 1935, 3588, 3155, 1522, 3775, 3290,
    2061, 1475, 802, 2600, 479, 1995, 3864, 2425, 1529, 3798, 3256, 440, 1414, 3768, 2452, 877,
    369, 1831, 2551, 770, 1975, 411, 2662, 1050, 3490, 44, 1660, 2679, 430, 1588, 133, 3344, 1746,
    2393, 2965, 1343, 1815, 3055, 1487, 2123, 3318, 2949, 1806, 2753, 3614, 2347, 1395, 3352, 3985,
    458, 244, 2651, 3361, 78, 1710, 2881, 2202, 3950, 410, 2358, 683, 2617, 444, 2432, 1177, 3102,
    3844, 2894, 1223, 3354, 2708, 60, 1897, 881, 2583, 2281, 1800, 3094, 3458, 2705, 3866, 1197,
    3611, 976, 3397, 2410, 1767, 3911, 2948, 59, 2458, 3671, 1357, 3078, 2275, 3615, 1450, 744,
    3734, 482, 1058, 3977, 349, 3705, 837, 1132, 1468, 2172, 3255, 299, 559, 2780, 1160, 2455,
    3524, 1382, 1842, 507, 3084, 833, 233, 3235, 1608, 3010, 1262, 3908, 1912, 906, 3544, 8, 1843,
    2332, 293, 1729, 626, 3556, 3146, 1189, 187, 3394, 978, 1254, 26, 676, 2310, 2860, 150, 2139,
    2980, 1546, 723, 1266, 2007, 3160, 971, 3377, 601, 2767, 1998, 1070, 372, 2678, 3248, 2057,
    2531, 3513, 1234, 2357, 2621, 3479, 189, 4027, 900, 1630, 3808, 2137, 862, 3065, 2006, 3877,
    962, 2312, 3746, 1484, 2493, 3476, 1078, 30, 2107, 3390, 221, 1402, 2805, 642, 1578, 3242, 922,
    4029, 1405, 2168, 2834, 1656, 3632, 592, 2896, 4018, 2169, 1506, 1708, 3207, 527, 1377, 4041,
    276, 3625, 2800, 498, 1444, 2181, 1807, 3967, 289, 845, 2935, 3914, 1901, 205, 1572, 2888, 698,
    1974, 3194, 501, 1883, 687, 2471, 3089, 1087, 2931, 1808, 3607, 141, 1600, 703, 3240, 2699,
    1243, 2068, 657, 1855, 3720, 2771, 813, 1716, 2967, 3688, 2239, 3969, 2569, 1112, 3623, 2959,
    2478, 812, 424, 3947, 2040, 1467, 2472, 1866, 450, 3586, 1039, 3805, 1920, 2485, 3286, 1124,
    2255, 2548, 3444, 3833, 794, 2386, 1232, 3227, 1675, 3441, 2461, 1159, 3558, 908, 2263, 54,
    3822, 2999, 1565, 3739, 2829, 1328, 1988, 86, 2400, 398, 3295, 1310, 2228, 2873, 333, 3622,
    158, 3989, 2936, 485, 1394, 2251, 4025, 2474, 550, 1033, 1953, 3358, 230, 2105, 513, 1929, 148,
    3341, 1062, 2648, 3069, 303, 3717, 836, 3294, 2592, 3020, 317, 891, 2732, 656, 1819, 124, 912,
    1684, 337, 2922, 161, 3698, 2587, 2112, 77, 1383, 609, 3045, 4068, 3347, 1721, 797, 37, 1072,
    2230, 335, 3550, 3333, 3881, 1473, 751, 2631, 4043, 568, 2488, 1144, 1933, 1653, 3303, 1013,
    2645, 3183, 288, 1170, 3275, 1563, 2694, 384, 1332, 798, 3135, 2748, 1494, 3763, 2305, 1307,
    1759, 704, 2273, 1173, 2766, 164, 2014, 1263, 2360, 3473, 1601, 3697, 3085, 3880, 2049, 3199,
    1153, 2646, 1932, 1526, 1012, 500, 3863, 2245, 1812, 2603, 414, 1313, 2778, 3443, 1399, 3957,
    2561, 883, 1709, 608, 2695, 2160, 3712, 1027, 3013, 1774, 3484, 3132, 850, 2195, 390, 2352,
    3571, 1742, 2025, 708, 3511, 115, 3026, 3896, 1704, 2404, 3814, 1202, 1782, 3017, 336, 3533,
    3890, 79, 3447, 3166, 1639, 3849, 1441, 695, 3974, 73, 2102, 464, 1286, 2876, 1499, 3551, 540,
    4079, 3044, 696, 3572, 2776, 3107, 814, 3727, 1513, 2100, 1014, 2371, 2657, 1797, 2082, 3208,
    2957, 1185, 197, 1852, 3147, 1256, 246, 2051, 1515, 40, 3934, 1427, 2845, 3807, 1279, 93, 864,
    3754, 2793, 2313, 1264, 2152, 925, 3557, 2644, 75, 3453, 675, 946, 2525, 2080, 2796, 1446,
    1934, 1001, 557, 2186, 3526, 2915, 1764, 3192, 1080, 2323, 2619, 286, 717, 2437, 2213, 1339,
    3382, 358, 2348, 1624, 1267, 3308, 304, 2856, 153, 3886, 536, 3576, 253, 713, 465, 3654, 2383,
    4017, 932, 3463, 509, 2517, 3642, 807, 2355, 520, 2628, 3420, 686, 1604, 2514, 2954, 1448, 510,
    3952, 1776, 3203, 593, 1993, 1460, 473, 2158, 4021, 3198, 1589, 537, 849, 2963, 2396, 4061,
    2633, 338, 926, 2459, 496, 2783, 1462, 3408, 4013, 1003, 1872, 3785, 10, 1763, 1064, 2087,
    3927, 144, 1980, 1049, 2380, 3628, 3181, 1925, 3015, 1230, 2296, 3331, 1641, 1363, 2010, 2801,
    1552, 2268, 2914, 1652, 3313, 2765, 3733, 1005, 1853, 198, 2072, 3086, 4071, 1840, 3400, 1011,
    330, 2490, 3646, 193, 2889, 3330, 1030, 2361, 2788, 178, 1181, 3751, 3306, 226, 1240, 3626,
    1796, 3098, 3335, 1193, 1949, 3643, 826, 169, 1695, 3262, 2989, 2702, 852, 3230, 2859, 2545,
    785, 2993, 3528, 2682, 648, 1699, 1150, 769, 1584, 4034, 991, 2736, 3819, 52, 3123, 365, 733,
    3832, 112, 1923, 1101, 341, 1345, 2987, 2272, 1200, 3618, 486, 1091, 214, 2115, 2669, 3120,
    1547, 782, 1158, 3865, 1649, 3070, 1344, 1811, 3609, 1997, 2675, 1694, 2161, 439, 776, 1408,
    101, 3800, 1595, 259, 3873, 2189, 2522, 571, 2074, 1364, 417, 3521, 1602, 243, 3772, 1455,
    1798, 420, 4023, 1410, 2207, 3365, 346, 2521, 125, 2932, 1887, 827, 2508, 1119, 3386, 2612,
    1308, 3585, 640, 4007, 2168, 3186, 1677, 3843, 3337, 842, 2759, 2382, 3283, 727, 3684, 1321,
    1960, 2826, 2221, 2539, 662, 298, 3926, 786, 412, 2439, 1022, 3874, 3050, 3417, 2549, 2868,
    2293, 2030, 2697, 706, 3038, 1298, 2857, 3496, 3738, 1133, 2283, 3915, 1959, 1182, 558, 2294,
    3430, 960, 2513, 12, 3821, 2751, 2013, 3689, 630, 2148, 1457, 3548, 531, 3918, 2175, 1821, 984,
    3079, 2441, 1485, 2665, 152, 721, 406, 2532, 1532, 1946, 1289, 3895, 1635, 2291, 31, 3924,
    3465, 416, 1841, 3715, 2103, 3387, 2930, 1477, 3477, 11, 639, 1505, 1894, 3928, 1117, 605,
    3583, 981, 2420, 3251, 1757, 356, 878, 1585, 123, 2510, 655, 3047, 2664, 3635, 3188, 1293,
    2902, 1905, 3125, 530, 923, 3033, 1320, 3486, 3172, 247, 2336, 1718, 2970, 1504, 229, 2841,
    2065, 422, 3260, 907, 3529, 2024, 2886, 3960, 81, 3174, 340, 2918, 577, 954, 3027, 28, 1042,
    3245, 1312, 2731, 116, 983, 2590, 2233, 3153, 1282, 2824, 2364, 939, 196, 1658, 2990, 403,
    3992, 1503, 16, 1094, 3945, 1917, 3165, 2806, 3395, 1440, 931, 2171, 98, 1666, 718, 290, 2141,
    1139, 3518, 1538, 2350, 1816, 1044, 2552, 3780, 1210, 3287, 902, 3630, 590, 3959, 3452, 1671,
    1209, 3718, 1813, 2407, 1375, 979, 2217, 1748, 3771, 2595, 2052, 3568, 2466, 1788, 1509, 2412,
    844, 3581, 1606, 1204, 1916, 533, 4047, 1770, 3702, 351, 3540, 3177, 2209, 3362, 1355, 1851,
    3460, 2134, 3624, 2325, 2624, 697, 2109, 296, 4050, 1844, 405, 3817, 2808, 2463, 3951, 3272,
    1724, 2626, 208, 3972, 392, 1644, 724, 2770, 445, 2027, 127, 2714, 2256, 1370, 809, 38, 2306,
    2787, 567, 1127, 3081, 3663, 516, 3426, 799, 1083, 1451, 3350, 476, 2781, 4049, 3090, 560,
    2270, 3006, 3296, 3795, 231, 924, 2129, 760, 2534, 1992, 1219, 2775, 815, 2484, 268, 2713, 637,
    2898, 467, 1329, 3854, 1007, 1707, 3221, 2391, 3459, 1113, 1999, 882, 3603, 1431, 641, 3755,
    829, 2853, 3301, 3851, 1945, 1388, 3035, 4046, 2457, 1141, 1904, 3170, 2582, 3850, 3025, 321,
    4054, 3360, 199, 2654, 1511, 2832, 2369, 137, 1870, 3732, 1235, 739, 2099, 211, 3745, 1961,
    362, 732, 2792, 2464, 1561, 3014, 3265, 1442, 524, 3975, 91, 3691, 2021, 3118, 1207, 896, 3214,
    1553, 179, 3019, 3517, 1224, 572, 2691, 1360, 2978, 1556, 488, 2238, 132, 2994, 2426, 2044,
    1198, 2225, 3099, 33, 941, 3446, 1571, 750, 2944, 3747, 265, 1041, 1755, 2020, 1409, 853, 1625,
    1921, 728, 2089, 1257, 4011, 3193, 2956, 310, 2614, 1616, 3314, 1097, 1413, 2635, 3982, 1728,
    1365, 3442, 3638, 126, 1100, 3812, 2653, 1822, 1570, 1066, 545, 3792, 1683, 4031, 1954, 2444,
    3660, 1825, 2214, 2863, 53, 3740, 766, 206, 3883, 3388, 2747, 1274, 1882, 438, 3448, 1465, 20,
    2618, 2401, 3633, 2196, 307, 1791, 3322, 487, 1540, 3407, 654, 3577, 2710, 2224, 3162, 2496,
    3882, 3527, 255, 1686, 627, 2210, 933, 3860, 2320, 2879, 1824, 3419, 905, 18, 2368, 1028, 421,
    1886, 2252, 2900, 300, 3424, 737, 2403, 3491, 2942, 2165, 2578, 129, 3412, 1069, 734, 345,
    2529, 876, 1587, 1958, 2277, 3180, 1761, 2500, 1065, 3112, 4057, 935, 2717, 3685, 660, 3998,
    1690, 1215, 596, 2672, 3907, 1299, 2339, 2084, 2846, 2415, 1238, 441, 3723, 51, 1183, 463,
    3004, 894, 2674, 3686, 1151, 1955, 3507, 99, 652, 402, 2156, 3596, 2838, 3217, 2073, 3930,
    2680, 633, 1327, 1669, 2104, 3143, 2833, 222, 1439, 834, 396, 1366, 2271, 2779, 3841, 1420,
    3342, 3936, 3101, 3552, 2575, 982, 374, 2069, 681, 3580, 1645, 283, 3215, 1787, 1106, 453,
    3266, 1968, 2871, 3158, 1017, 94, 3612, 915, 3938, 166, 3276, 964, 1856, 2908, 3404, 2354,
    1449, 1830, 3280, 2467, 435, 1564, 2985, 1380, 3965, 3111, 2501, 1199, 1555, 578, 2974, 1237,
    856, 3366, 2480, 4004, 937, 426, 1229, 1937, 3946, 3273, 3570, 2995, 569, 1650, 3121, 2002,
    503, 1099, 275, 1273, 585, 4014, 1469, 3710, 72, 2182, 2580, 1350, 2340, 2022, 2926, 1521, 861,
    180, 3806, 1456, 2451, 1879, 2640, 623, 1701, 3067, 1490, 2576, 3970, 1573, 649, 1036, 2146,
    3790, 21, 1306, 3413, 2721, 804, 2384, 1026, 1681, 3788, 765, 309, 3680, 1779, 176, 3765, 3064,
    1832, 19, 3530, 2300, 3758, 2639, 1697, 2373, 1138, 1859, 3731, 930, 87, 2638, 2363, 1737,
    2166, 2951, 1868, 3270, 2811, 1201, 3363, 2911, 523, 3855, 757, 151, 3422, 2544, 2285, 3574,
    409, 2142, 779, 3498, 3001, 1358, 387, 3761, 2164, 764, 264, 2028, 3677, 2701, 323, 3116, 778,
    2023, 4065, 241, 3178, 3655, 2046, 185, 2709, 1940, 2289, 3375, 2589, 2187, 1541, 367, 1074,
    2758, 1411, 3233, 566, 989, 48, 669, 2730, 251, 2133, 3257, 1213, 4056, 3508, 790, 3799, 2684,
    128, 1634, 754, 2388, 1914, 901, 1543, 3501, 2785, 1046, 3903, 3043, 1322, 2707, 1134, 1723,
    3243, 257, 4073, 2246, 2761, 1927, 1111, 3469, 2864, 1292, 3228, 1734, 3952, 2422, 2882, 1654,
    612, 2229, 1802, 1231, 454, 3310, 1428, 3048, 3858, 1076, 1368, 741, 3254, 2381, 3602, 2032,
    775, 1603, 2925, 2190, 3645, 3105, 1536, 3893, 2506, 716, 2901, 1528, 216, 3062, 1407, 942,
    3657, 2259, 326, 3829, 3073, 217, 1794, 3148, 1236, 2108, 607, 319, 1827, 3984, 3402, 2927,
    565, 1582, 1188, 949, 3634, 74, 3115, 2322, 534, 2503, 118, 929, 518, 1369, 1104, 3627, 2611,
    988, 3897, 2852, 2536, 3542, 632, 919, 88, 2874, 480, 4037, 1891, 2903, 582, 3878, 2537, 34,
    4069, 1810, 1252, 3351, 2011, 475, 1351, 3610, 2292, 1902, 581, 3376, 2442, 425, 1971, 3403,
    1073, 2663, 1378, 4001, 2540, 385, 2405, 3703, 1619, 2234, 977, 709, 46, 2335, 3852, 2056,
    2542, 3370, 1750, 690, 1403, 4016, 1623, 3592, 3021, 2237, 3478, 1956, 3329, 376, 3042, 1470,
    15, 738, 1599, 2147, 4009, 1814, 2443, 1638, 3488, 2693, 985, 147, 1277, 1714, 3133, 1114,
    3438, 400, 2454, 832, 2839, 1063, 3396, 1727, 354, 1015, 2814, 2081, 1148, 3988, 3137, 1510,
    549, 3537, 2048, 659, 998, 3604, 1926, 23, 3312, 3776, 3100, 1990, 2598, 1354, 887, 2812, 188,
    512, 2362, 3236, 2605, 332, 857, 1878, 1155, 3885, 1527, 2789, 149, 2315, 3825, 2070, 3468,
    3197, 1086, 266, 2958, 1290, 3237, 2054, 292, 2243, 1502, 3744, 3364, 2314, 315, 2140, 2742,
    1479, 3809, 212, 2250, 3996, 105, 3029, 2553, 3917, 3674, 1685, 61, 2729, 761, 2518, 2972,
    1719, 103, 2244, 3225, 2923, 1415, 846, 2773, 1221, 1549, 484, 3661, 3210, 1877, 3707, 3037,
    1508, 3887, 1989, 1079, 3721, 2135, 2720, 664, 297, 2465, 753, 1783, 910, 1301, 553, 2445,
    1880, 3782, 2301, 2660, 535, 3676, 1130, 3902, 682, 3190, 1987, 841, 2649, 3678, 546, 936,
    1983, 3041, 644, 1631, 2636, 1942, 871, 3220, 1269, 705, 2365, 3307, 1346, 1835, 220, 3925,
    1241, 2786, 3713, 1167, 1679, 470, 4064, 2512, 232, 3535, 2870, 1105, 1655, 355, 719, 1211,
    3516, 911, 2840, 162, 2982, 3436, 1443, 3267, 3694, 2093, 3127, 3980, 3545, 2938, 1643, 2734,
    388, 866, 1406, 3389, 155, 817, 2782, 3054, 1333, 2520, 404, 2975, 1191, 1562, 3954, 2912,
    1309, 3590, 3247, 1165, 3716, 1398, 517, 2153, 190, 1491, 2964, 478, 3781, 3471, 2159, 889,
    2375, 3355, 789, 263, 2607, 3450, 2050, 651, 2353, 3261, 2124, 171, 2489, 4006, 2307, 2085,
    2655, 395, 1664, 1303, 2248, 481, 1741, 3, 2913, 1261, 431, 1060, 2541, 245, 3258, 1142, 3617,
    3066, 1715, 3920, 2015, 1591, 2359, 1848, 65, 3823, 1698, 3555, 172, 1924, 745, 2483, 97, 2345,
    1817, 311, 3470, 2878, 2411, 3839, 3547, 2615, 1952, 952, 2279, 1121, 361, 3109, 1557, 493,
    1893, 3942, 2197, 3142, 1059, 1785, 1436, 859, 3830, 613, 2740, 3418, 1447, 85, 3299, 1861,
    3958, 3176, 772, 3811, 2395, 965, 2594, 1918, 3427, 1430, 2199, 674, 1969, 4024, 27, 2136, 617,
    2491, 1040, 3289, 433, 3504, 961, 2179, 618, 2772, 2288, 3373, 3152, 1722, 3842, 491, 2715,
    967, 2075, 678, 1592, 1035, 1790, 320, 4044, 3167, 1605, 2835, 3620, 1979, 3818, 2666, 2998,
    1302, 1518, 121, 2848, 3892, 2584, 3036, 1911, 1314, 970, 1792, 3114, 822, 3651, 2374, 600,
    2566, 3554, 1950, 1184, 4058, 574, 1628, 3848, 119, 2822, 3708, 1533, 2356, 1334, 2677, 3456,
    274, 2862, 1270, 4002, 2620, 1516, 3222, 1122, 4051, 1393, 987, 294, 3489, 1109, 1501, 2208,
    3144, 4026, 35, 3369, 3088, 2744, 810, 3410, 598, 139, 2558, 670, 1381, 67, 1008, 3519, 634,
    2470, 897, 3606, 415, 1696, 70, 3506, 2261, 2940, 3725, 521, 1126, 2810, 1367, 223, 1034, 1579,
    17, 2739, 3097, 3321, 791, 2327, 3051, 1743, 875, 3356, 495, 1018, 2969, 780, 1876, 3737, 2101,
    665, 2997, 313, 3711, 2017, 2556, 469, 3031, 2117, 2604, 658, 2854, 3704, 792, 1300, 2562, 447,
    2242, 1172, 1417, 2447, 2094, 1258, 3943, 1777, 3325, 2416, 2111, 329, 1801, 3789, 3293, 1982,
    1244, 3378, 773, 4030, 379, 1581, 194, 2560, 2180, 1632, 3779, 3071, 2116, 2921, 3398, 386,
    1482, 2062, 234, 2670, 3564, 1195, 312, 2599, 3134, 3828, 1620, 3563, 1432, 3169, 107, 1736,
    2311, 854, 2804, 140, 1613, 801, 3853, 1858, 1253, 3949, 3309, 182, 1909, 1706, 2950, 1991,
    3923, 43, 3719, 2916, 1658, 3522, 3018, 843, 2769, 1174, 4048, 3126, 2880, 2235, 203, 2704,
    584, 2875, 2043, 1145, 2456, 3271, 1994, 3879, 3434, 352, 1900, 752, 3990, 1255, 2436, 890,
    2267, 3764, 1337, 1021, 547, 2001, 3948, 2174, 1799, 191, 2448, 380, 2258, 938, 2579, 1152,
    3546, 1371, 1865, 3445, 2398, 3641, 2827, 50, 2329, 1633, 399, 2431, 1057, 3415, 3659, 1458,
    720, 3219, 1846, 490, 1009, 224, 2304, 378, 3636, 543, 1691, 1421, 800, 1096, 1598, 3968, 2328,
    3621, 3103, 1492, 2756, 917, 693, 1281, 3012, 955, 2687, 3315, 472, 1687, 3683, 628, 3497,
    1809, 2932, 3891, 2462, 3393, 1384, 715, 2867, 1161, 2042, 4072, 586, 2764, 3856, 3319, 497,
    3971, 3052, 370, 1206, 3163, 1464, 576, 3594, 918, 2035, 3092, 2685, 548, 237, 2379, 959, 3509,
    2149, 2711, 3862, 3151, 1903, 1474, 2170, 3246, 167, 2453, 3472, 442, 3211, 1348, 969, 1789,
    145, 539, 3916, 3565, 1839, 2337, 24, 1497, 3605, 2222, 159, 2629, 1981, 2843, 1135, 106, 3200,
    383, 1551, 2799, 47, 3189, 3741, 899, 3401, 2934, 1259, 1836, 1594, 227, 2440, 1976, 968, 650,
    2212, 1765, 1045, 2652, 3238, 2887, 1356, 3531, 1554, 3899, 1222, 2830, 1668, 2535, 318, 1318,
    767, 2497, 1107, 3913, 2642, 957, 3756, 2041, 2743, 3698, 1888, 2533, 322, 3786, 2647, 2200,
    1326, 249, 3188, 2817, 4059, 611, 2476, 1118, 1396, 3150, 3870, 295, 1607, 2593, 2167, 768,
    1913, 958, 2331, 1670, 466, 2526, 1493, 5, 3239, 3664, 788, 2177, 2895, 1486, 2688, 3291, 3889,
    181, 3730, 1928, 314, 4032, 710, 42, 2278, 830, 2096, 3154, 4074, 622, 3002, 1567, 3305, 3582,
    80, 2892, 647, 1838, 2976, 1242, 668, 62, 3039, 2151, 726, 1568, 1053, 3455, 2430, 1662, 451,
    1154, 2031, 1740, 3223, 3753, 1867, 803, 1016, 2309, 3282, 4038, 1246, 3665, 3063, 3495, 4005,
    1225, 2719, 3637, 684, 1964, 2377, 344, 3096, 1095, 3777, 76, 3562, 1287, 2502, 2939, 777,
    2427, 2163, 1169, 1712, 2586, 3759, 3279, 408, 1803, 111, 1090, 3783, 1939, 2220, 511, 1732,
    1341, 3482, 272, 1548, 3976, 2346, 1676, 921, 3894, 3311, 2821, 3129, 1863, 774, 2984, 3846,
    2606, 3481, 855, 324, 2724, 526, 2981, 3536, 1453, 672, 455, 1738, 2423, 202, 614, 2077, 328,
    3300, 2204, 1725, 3901, 974, 2676, 1397, 3416, 570, 1700, 831, 2090, 348, 1577, 3327, 1385,
    3461, 514, 3060, 1884, 1037, 2929, 1342, 3560, 2727, 2303, 3435, 204, 943, 4012, 2406, 2033,
    3173, 2555, 468, 3324, 1110, 3534, 2668, 489, 1250, 2034, 606, 83, 3631, 975, 1422, 2188, 142,
    2907, 1535, 2290, 3999, 82, 2468, 2004, 2919, 3462, 2746, 1071, 1402, 2610, 1795, 2979, 1031,
    63, 1311, 3056, 506, 2119, 4003, 1907, 2565, 2282, 3204, 4060, 1862, 552, 994, 3931, 32, 2725,
    3687, 2205, 207, 636, 2505, 2005, 858, 1523, 1275, 2577, 2815, 3076, 1162, 742, 3766, 927,
    2216, 1899, 4, 2890, 1434, 2435, 213, 3743, 2319, 3981, 2563, 1972, 3250, 643, 3769, 1054,
    3380, 1288, 2076, 1642, 1146, 3835, 218, 898, 2106, 3722, 3264, 3912, 848, 1530, 3847, 2429,
    3429, 2798, 3553, 1617, 146, 748, 2968, 281, 1164, 2741, 3074, 3639, 2613, 2008, 1627, 3185,
    892, 1476, 3884, 3391, 1612, 3966, 301, 3696, 3218, 699, 397, 1626, 3640, 136, 2752, 1425,
    3868, 3057, 787, 2088, 4035, 1778, 3437, 838, 1507, 1278, 413, 2836, 277, 1622, 2413, 1895,
    3629, 702, 3077, 3316, 419, 2596, 1860, 3175, 1542, 39, 434, 2249, 2844, 3601, 528, 1898, 763,
    278, 1171, 2515, 3278, 1295, 3584, 3815, 1520, 920, 134, 2262, 1233, 2928, 701, 2389, 1268,
    360, 2825, 2334, 1157, 3108, 544, 2906, 1768, 2063, 3906, 1359, 2184, 3285, 357, 1692, 2469,
    3566, 1304, 551, 3202, 343, 1085, 2609, 2953, 3332, 1731, 3543, 1147, 4020, 3106, 471, 2571,
    235, 947, 2813, 3650, 784, 1317, 3964, 2367, 3005, 746, 1948, 1280, 173, 3117, 2650, 1423,
    2274, 3941, 909, 1818, 2837, 446, 2422, 2029, 663, 3483, 1726, 432, 3857, 270, 3525, 2064,
    3288, 1749, 819, 56, 2157, 2673, 1047, 2385, 25, 3340, 2495, 999, 1850, 2988, 631, 1081, 200,
    2807, 3714, 1586, 2192, 3082, 1849, 184, 2127, 722, 2511, 2264, 869, 1335, 2784, 1730, 3922,
    2349, 1459, 1784, 2223, 2722, 556, 1175, 3385, 1678, 2528, 3487, 1089, 2162, 1640, 3770, 2924,
    1985, 610, 3700, 2176, 1025, 1673, 3334, 3955, 1336, 2499, 3249, 1075, 1826, 2661, 4010, 591,
    2546, 3749, 3492, 1871, 1424, 3816, 3579, 839, 2945, 515, 3549, 2659, 4077, 2058, 3414, 2298,
    1820, 995, 2482, 677, 3597, 3810, 532, 1023, 3910, 2996, 1, 2039, 3326, 57, 2155, 1196, 575,
    3423, 122, 3787, 308, 3515, 2036, 980, 3824, 2763, 305, 4066, 667, 3302, 963, 108, 3229, 350,
    3091, 1419, 64, 2623, 3016, 260, 2790, 2126, 3736, 783, 3113, 1517, 138, 997, 3008, 1285, 436,
    3232, 731, 254, 1674, 1251, 1963, 1525, 236, 795, 1217, 1500, 3159, 461, 3953, 2689, 3348, 92,
    1239, 2280, 2716, 3467, 1389, 1583, 3750, 616, 3589, 1524, 3801, 3231, 2941, 1984, 1082, 3104,
    1637, 2897, 2481, 154, 508, 3191, 1352, 1864, 2434, 382, 3514, 1324, 2574, 1609, 2342, 4040,
    3433, 825, 1847, 573, 951, 1478, 84, 2341, 2865, 1179, 2241, 3593, 1629, 2000, 2370, 4053,
    2861, 2211, 2567, 3421, 3978, 2343, 3128, 3692, 2226, 71, 2588, 879, 1372, 316, 2016, 1488,
    2893, 1667, 256, 1965, 3136, 391, 2656, 1857, 2917, 1020, 755, 2637, 377, 3986, 2414, 625,
    1387, 811, 4015, 1823, 1495, 2286, 867, 2960, 3690, 2738, 1786, 2132, 3900, 1048, 714, 2750,
    1220, 3667, 2295, 3226, 3888, 1962, 3411, 529, 3656, 1944, 389, 3209, 2745, 225, 914, 1519,
    1148, 3080, 619, 371, 1067, 2703, 1745, 580, 2891, 1881, 3466, 3794, 1717, 3046, 3670, 851,
    3932, 3263, 736, 1214, 2402, 892, 3439, 1178, 2479, 279, 2317, 1889, 1580, 893, 3538, 2658,
    3346, 2203, 1216, 3075, 3729, 3432, 2053, 22, 1534, 1187, 806, 502, 2883, 3598, 1885, 273,
    2060, 429, 1559, 1115, 2573, 2952, 1271, 1703, 2602, 868, 3820, 1376, 671, 3875, 2504, 3374,
    104, 1772, 3867, 3212, 2098, 131, 1390, 953, 3929, 2399, 1108, 694, 2816, 2218, 563, 2418,
    1093, 2550, 3559, 3991, 2823, 2201, 160, 1672, 4033, 3182, 1325, 3672, 168, 1227, 2120, 1804,
    68, 3653, 382, 2572, 700, 1098, 2667, 595, 3973, 3059, 2387, 3195, 163, 1472, 3353, 2494, 3053,
    3838, 2820, 165, 3523, 373, 743, 4063, 210, 3336, 2394, 1793, 2937, 2125, 504, 3658, 2755,
    1951, 1361, 870, 2819, 3796, 3512, 3241, 407, 1574, 250, 3274, 1260, 130, 3503, 1875, 364,
    2113, 14, 1751, 538, 3224, 3836, 711, 2128, 492, 3406, 2733, 3087, 3774, 2884, 457, 3179, 1029,
    1966, 2849, 215, 1733, 3599, 3234, 2178, 269, 1874, 3834, 1129, 2254, 599, 945, 1702, 1331,
    685, 2097, 1773, 2438, 3171, 2173, 1575, 3034, 1103, 58, 3485, 1245, 1651, 1006, 2284, 306,
    3494, 2460, 1610, 673, 1915, 2564, 2131, 3024, 3647, 2026, 2509, 4028, 1418, 3168, 2943, 1297,
    3752, 1481, 1010, 1941, 1379, 2686, 2961, 934, 2019, 1711, 635, 2475, 820, 1426, 1665, 3861,
    2324, 1340, 3935, 2417, 427, 1614, 950, 1362, 2597, 3499, 1636, 2768, 3956, 90, 3206, 3561,
    2366, 3937, 904, 1429, 3682, 1032, 2726, 589, 2038, 3944, 828, 2692, 3281, 3032, 692, 3726,
    1131, 2991, 460, 2316, 1149, 183, 1291, 793, 2723, 1780, 1002, 645, 2678, 1615, 903, 448, 2616,
    2297, 3049, 3675, 347, 3493, 1558, 7, 3909, 1123, 339, 2240, 4078, 3383, 2698, 594, 2977, 847,
    3367, 1931, 1208, 2754, 3791, 3345, 638, 873, 375, 2079, 3681, 2557, 1947, 1088, 505, 3028,
    3328, 45, 2872, 1936, 342, 3600, 1471, 2527, 428, 1910, 170, 4000, 1496, 2632, 2045, 6, 4045,
    3149, 3608, 2858, 3372, 3961, 1452, 459, 3724, 2143, 271, 3859, 3357, 729, 3475, 2850, 186,
    823, 2498, 1194, 2308, 3648, 2554, 1454, 3277, 1922, 128, 1186, 2066, 258, 3520, 1514, 109,
    3145, 747, 3000, 2302, 9, 2003, 2920, 3124, 1205, 1412, 758, 2909, 248, 1590, 2627, 1192, 2253,
    646, 3803, 1305, 3392, 2219, 3138, 3757, 1386, 2433, 2145, 393, 1744, 3381, 796, 1461, 1833,
    928, 1661, 564, 2247, 55, 3122, 3343, 2866, 1176, 2344, 1834, 2047, 1136, 4042, 1680, 2150,
    3339, 561, 1828, 3119, 749, 2855, 3578, 916, 3022, 3679, 2547, 996, 1845, 3826, 2191, 2530,
    4052, 331, 1480, 3695, 1720, 3997, 2424, 177, 3359, 1752, 2215, 3728, 3440, 2009, 381, 3987,
    1747, 2581, 880, 143, 1688, 666, 2831, 1143, 3510, 884, 3187, 1249, 2828, 3773, 2194, 2700,
    240, 2473, 3845, 1930, 1125, 2408, 1647, 840, 3569, 113, 3093, 1566, 291, 615, 1330, 3797,
    2794, 1024, 3995, 262, 2083, 474, 2397, 1769, 1550, 691, 2299, 3253, 452, 2774, 1128, 554,
    1762, 2092, 1056, 2570, 1296, 483, 1000, 3595, 2681, 579, 4039, 956, 1338, 2795, 818, 3532,
    1498, 3252, 2946, 2351, 4022, 992, 252, 1781, 2973, 604, 3871, 2519, 261, 1051, 519, 3323,
    3669, 1373, 3030, 888, 3502, 2643, 327, 1973, 1400, 2538, 3905,
];
