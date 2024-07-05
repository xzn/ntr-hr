use super::*;

const CONST_BITS: usize = 14;
pub const DCTSIZE: usize = 8;
pub const DCTSIZE2: usize = DCTSIZE * DCTSIZE;
const aanscales: [u16; DCTSIZE2] = [
    16384, 22725, 21407, 19266, 16384, 12873, 8867, 4520, 22725, 31521, 29692, 26722, 22725, 17855,
    12299, 6270, 21407, 29692, 27969, 25172, 21407, 16819, 11585, 5906, 19266, 26722, 25172, 22654,
    19266, 15137, 10426, 5315, 16384, 22725, 21407, 19266, 16384, 12873, 8867, 4520, 12873, 17855,
    16819, 15137, 12873, 10114, 6967, 3552, 8867, 12299, 11585, 10426, 8867, 6967, 4799, 2446,
    4520, 6270, 5906, 5315, 4520, 3552, 2446, 1247,
];

const bits_dc_luminance: [u8; 17] = [
    /* 0-base */ 0, 0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0,
];
const val_dc_luminance: [u8; 12] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11];

const bits_dc_chrominance: [u8; 17] = [
    /* 0-base */ 0, 0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0,
];
const val_dc_chrominance: [u8; 12] = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11];

const bits_ac_luminance: [u8; 17] = [
    /* 0-base */ 0, 0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d,
];

const val_ac_luminance: [u8; 162] = [
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

const bits_ac_chrominance: [u8; 17] = [
    /* 0-base */ 0, 0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77,
];

const val_ac_chrominance: [u8; 162] = [
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
    pub huffVal: [u8; 256],
}

const NUM_HUFF_TBLS: usize = 2;

#[derive(ConstDefault)]
pub struct HuffTbls {
    pub dcHuffTbls: [HuffTbl; NUM_HUFF_TBLS],
    pub acHuffTbls: [HuffTbl; NUM_HUFF_TBLS],
}

fn addHuffTable(tbl: &mut HuffTbl, bits: &[u8; 17], val: &[u8]) {
    tbl.bits = *bits;

    let mut nsymbols: usize = 0;
    for len in 1..=16 {
        nsymbols += bits[len] as usize;
    }
    if nsymbols < 1 || nsymbols > 256 {
        panic!();
    }

    for i in 0..nsymbols {
        tbl.huffVal[i] = val[i];
    }
    for i in nsymbols..256 {
        tbl.huffVal[i] = 0;
    }
}

impl HuffTbls {
    pub fn init(&mut self) {
        addHuffTable(
            &mut self.dcHuffTbls[0],
            &bits_dc_luminance,
            val_dc_luminance.as_slice(),
        );
        addHuffTable(
            &mut self.acHuffTbls[0],
            &bits_ac_luminance,
            val_ac_luminance.as_slice(),
        );
        addHuffTable(
            &mut self.dcHuffTbls[1],
            &bits_dc_chrominance,
            val_dc_chrominance.as_slice(),
        );
        addHuffTable(
            &mut self.acHuffTbls[1],
            &bits_ac_chrominance,
            val_ac_chrominance.as_slice(),
        );
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
    pub rb_5_tab: [u8; 1 << 5],
    pub g_6_tab: [u8; 1 << 6],
}

pub const SCALEBITS: usize = 16; /* speediest right-shift on some machines */
const CBCR_OFFSET: isize = (CENTERJSAMPLE as isize) << SCALEBITS;
const ONE_HALF: isize = (1 as isize) << (SCALEBITS - 1);
const fn FIX(x: f64) -> isize {
    ((x) * ((1 as isize) << SCALEBITS) as f64 + 0.5) as isize
}

impl ColorConvTabs {
    pub fn init(&mut self) {
        for i in 0..=MAXJSAMPLE {
            self.rgb_ycc_tab[i + R_Y_OFF] = (FIX(0.29900) * i as isize) as i32;
            self.rgb_ycc_tab[i + G_Y_OFF] = (FIX(0.58700) * i as isize) as i32;
            self.rgb_ycc_tab[i + B_Y_OFF] = ((FIX(0.11400) * i as isize) + ONE_HALF) as i32;
            self.rgb_ycc_tab[i + R_CB_OFF] = ((-FIX(0.16874)) * i as isize) as i32;
            self.rgb_ycc_tab[i + G_CB_OFF] = ((-FIX(0.33126)) * i as isize) as i32;
            /* We use a rounding fudge-factor of 0.5-epsilon for Cb and Cr.
             * This ensures that the maximum output will round to _MAXJSAMPLE
             * not _MAXJSAMPLE+1, and thus that we don't have to range-limit.
             */
            self.rgb_ycc_tab[i + B_CB_OFF] =
                ((FIX(0.50000) * i as isize) + CBCR_OFFSET + ONE_HALF - 1) as i32;
            /*  B=>Cb and R=>Cr tables are the same
                rgb_ycc_tab[i + R_CR_OFF] = FIX(0.50000) * i  + CBCR_OFFSET + ONE_HALF - 1;
            */
            self.rgb_ycc_tab[i + G_CR_OFF] = ((-FIX(0.41869)) * i as isize) as i32;
            self.rgb_ycc_tab[i + B_CR_OFF] = ((-FIX(0.08131)) * i as isize) as i32;
        }

        for i in 0..(1 << 5) {
            self.rb_5_tab[i] = ((FIX((((1 << 8) - 1) as f64) / ((1 << 5) - 1) as f64) * i as isize
                + ONE_HALF as isize)
                >> SCALEBITS) as u8;
        }
        for i in 0..(1 << 6) {
            self.g_6_tab[i] = ((FIX((((1 << 8) - 1) as f64) / ((1 << 6) - 1) as f64) * i as isize
                + ONE_HALF as isize)
                >> SCALEBITS) as u8;
        }
    }
}

#[derive(ConstDefault)]
pub struct CompInfo {
    /* These values are fixed over the whole image. */
    /* For compression, they must be supplied by parameter setup; */
    /* for decompression, they are read from the SOF marker. */
    pub component_id: u8,    /* identifier for this component (0..255) */
    pub component_index: u8, /* its index in SOF or cinfo->comp_info[] */
    pub h_samp_factor: u8,   /* horizontal sampling factor (1..4) */
    pub v_samp_factor: u8,   /* vertical sampling factor (1..4) */
    pub quant_tbl_no: u8,    /* quantization table selector (0..3) */
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
    fn setComp(
        &mut self,
        index: usize,
        id: u8,
        hsamp: u8,
        vsamp: u8,
        quant: u8,
        dctbl: u8,
        actbl: u8,
    ) {
        let comp = &mut self.infos[index];
        comp.component_id = id;
        comp.component_index = index as u8;
        comp.h_samp_factor = hsamp;
        comp.v_samp_factor = vsamp;
        comp.quant_tbl_no = quant;
        comp.dc_tbl_no = dctbl;
        comp.ac_tbl_no = actbl;
    }

    pub fn setColorSpaceYCbCr(&mut self) {
        self.setComp(0, 1, 2, 2, 0, 0, 0);
        self.setComp(1, 2, 1, 1, 1, 1, 1);
        self.setComp(2, 3, 1, 1, 1, 1, 1);
    }
}

fn qualityScaling(quality: u32) -> u32
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

const std_luminance_quant_tbl: [u8; DCTSIZE2] = [
    16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56,
    14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113,
    92, 49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99,
];

const std_chrominance_quant_tbl: [u8; DCTSIZE2] = [
    17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99,
];

#[derive(ConstDefault)]
pub struct QuantTbl {
    /* This array gives the coefficient quantizers in natural array order
     * (not the zigzag order in which they are stored in a JPEG DQT marker).
     * CAUTION: IJG versions prior to v6a kept this array in zigzag order.
     */
    /* quantization step for each coefficient */
    pub quantval: [u8; DCTSIZE2],
}

const NUM_QUANT_TBLS: usize = 2;

#[derive(ConstDefault)]
pub struct QuantTbls {
    pub quantTbls: [QuantTbl; NUM_QUANT_TBLS],
}

impl QuantTbls {
    fn setQuantTable(&mut self, which_tbl: usize, basic_table: &[u8; DCTSIZE2], scale_factor: u32) {
        let tbl = &mut self.quantTbls[which_tbl];
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
            tbl.quantval[i] = temp as u8;
        }
    }

    fn setLinearQuality(&mut self, scale_factor: u32) {
        self.setQuantTable(0, &std_luminance_quant_tbl, scale_factor);
        self.setQuantTable(1, &std_chrominance_quant_tbl, scale_factor);
    }

    pub fn setQuality(&mut self, quality: u32)
    /* Set or change the 'quality' (quantization) setting, using default tables.
     * This is the standard quality-adjusting entry point for typical user
     * interfaces; only those who want detailed control over quantization tables
     * would use the preceding three routines directly.
     */
    {
        /* Convert user 0-100 rating to percentage scaling */
        let quality = qualityScaling(quality);

        /* Set up standard quality tables */
        self.setLinearQuality(quality);
    }
}

#[derive(ConstDefault)]
pub struct Divisors {
    pub divisors: [[[i16; DCTSIZE2]; 3]; NUM_QUANT_TBLS],
}

const ONE: u32 = 1;
fn RIGHT_SHIFT(x: u32, shft: u32) -> u32 {
    x >> shft
}
fn DESCALE(x: u32, n: u32) -> u16 {
    RIGHT_SHIFT(x + (ONE << (n - 1)), n) as u16
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
        if (val & 0x8000) == 0 {
            bit -= 1;
            #[allow(unused_assignments)]
            val <<= 1;
        }

        bit
    }
}

fn computeReciprocal(divisor: u16, divisors: &mut [[i16; DCTSIZE2]; 3], i: usize) {
    if divisor == 1 {
        /* divisor == 1 means unquantized, so these reciprocal/correction/shift
         * values will cause the C quantization algorithm to act like the
         * identity function.  Since only the C quantization algorithm is used in
         * these cases, the scale value is irrelevant.
         */
        divisors[0][i] = 1; /* reciprocal */
        divisors[1][i] = 0; /* correction */
        divisors[2][i] = 0; /* shift */
        return;
    }

    let b: u32 = flss(divisor) - 1;
    let mut r: u32 = mem::size_of::<i16>() as u32 * 8 + b;

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

    divisors[0][i] = fq as i16; /* reciprocal */
    divisors[1][i] = c as i16; /* correction + roundfactor */
    divisors[2][i] = r as i16; /* shift */
}

impl Divisors {
    pub fn setDivisors(&mut self, quantTbls: &QuantTbls) {
        for t in 0..NUM_QUANT_TBLS {
            let divisors = &mut self.divisors[t];
            let quantTbl = &quantTbls.quantTbls[t];

            for i in 0..DCTSIZE2 {
                computeReciprocal(
                    DESCALE(
                        (quantTbl.quantval[i] as u32) * (aanscales[i] as u32),
                        CONST_BITS as u32 - 3,
                    ),
                    divisors,
                    i,
                );
            }
        }
    }
}

#[derive(ConstDefault)]
pub struct DerivedTbl {
    ehufco: [u32; 256], /* code for each symbol */
    ehufsi: [u8; 256],  /* length of code for each symbol */
                        /* If no code has been allocated for a symbol S, ehufsi[S] contains 0 */
}

#[derive(ConstDefault)]
pub struct EntropyTbls {
    dc_derived_tbls: [DerivedTbl; NUM_HUFF_TBLS],
    ac_derived_tbls: [DerivedTbl; NUM_HUFF_TBLS],
}

fn setDerivedTbl(tbl: &mut DerivedTbl, isDC: bool, tblno: usize, huffTbls: &HuffTbls) {
    /* Note that huffsize[] and huffcode[] are filled in code-length order,
     * paralleling the order of the symbols themselves in htbl->huffval[].
     */

    /* Find the input Huffman table */
    if tblno >= NUM_HUFF_TBLS {
        panic!();
    }
    let htbl = if isDC {
        &huffTbls.dcHuffTbls[tblno]
    } else {
        &huffTbls.acHuffTbls[tblno]
    };

    /* Figure C.1: make table of Huffman code length for each symbol */

    let mut p: usize = 0;
    let mut huffsize: [u8; 257] = const_default();
    for l in 1..=16 {
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
    let maxsymbol = if isDC { 15 } else { 255 };

    for p in 0..lastp {
        let i = htbl.huffVal[p];
        if i > maxsymbol || tbl.ehufsi[i as usize] > 0 {
            panic!();
        }
        tbl.ehufco[i as usize] = huffcode[p];
        tbl.ehufsi[i as usize] = huffsize[p];
    }
}

impl EntropyTbls {
    pub fn setEntropyTbls(&mut self, huffTbls: &HuffTbls) {
        for i in 0..NUM_HUFF_TBLS {
            setDerivedTbl(&mut self.dc_derived_tbls[i], true, i, huffTbls);
            setDerivedTbl(&mut self.ac_derived_tbls[i], false, i, huffTbls);
        }
    }
}

pub const MAX_SAMP_FACTOR: usize = 2;
pub const GSP_SCREEN_WIDTH: usize = ctru::GSP_SCREEN_WIDTH as usize;

type JCoef = i16;
pub type JBlock = [JCoef; DCTSIZE2];
const MAX_BLOCKS_IN_MCU: usize = MAX_SAMP_FACTOR * MAX_SAMP_FACTOR + MAX_COMPONENTS - 1;

#[derive(ConstDefault)]
pub struct WorkerBufs {
    pub color: [[[u8; GSP_SCREEN_WIDTH]; MAX_SAMP_FACTOR]; MAX_COMPONENTS],
    pub prep: [[[u8; GSP_SCREEN_WIDTH]; MAX_SAMP_FACTOR * DCTSIZE]; MAX_COMPONENTS],
    pub mcu: [JBlock; MAX_BLOCKS_IN_MCU],
}

#[derive(Default)]
pub enum ColorSpace {
    #[default]
    XBGR,
    BGR,
    RGB565,
    RGB5A1,
    RGB4,
}

pub const OUTPUT_BUF_SIZE: u32 = PACKET_SIZE - DATA_HDR_SIZE;

pub const M_SOI: u8 = 0xd8;
pub const M_APP0: u8 = 0xe0;
pub const M_DQT: u8 = 0xdb;
pub const M_SOF0: u8 = 0xc0;
pub const M_DHT: u8 = 0xc4;
pub const M_DRI: u8 = 0xdd;
pub const M_SOS: u8 = 0xda;
pub const M_EOI: u8 = 0xd9;
pub const JPEG_RST0: u8 = 0xD0;

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

pub const jpeg_natural_order: [u8; DCTSIZE2] = [
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20,
    13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59,
    52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63,
];

pub const in_rows_blk: usize = DCTSIZE * MAX_SAMP_FACTOR;
pub const in_rows_blk_half: usize = in_rows_blk / 2;

const fn jdiv_round_up(a: u32, b: u32) -> u32
/* Compute a/b rounded up to next integer, ie, ceil(a/b) */
/* Assumes a >= 0, b > 0 */
{
    (a + b - 1) / b
}

pub const MCUs_per_row: u8 =
    jdiv_round_up(GSP_SCREEN_WIDTH as u32, (MAX_SAMP_FACTOR * DCTSIZE) as u32) as u8;
