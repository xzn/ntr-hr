#ifndef RP_COMMON_H
#define RP_COMMON_H

#include "rp_common_base.h"

#include "libavcodec/jpegls.h"
#include "libavcodec/get_bits.h"
#include "libavfilter/motion_estimation.h"
#include "libavfilter/scene_sad.h"
#include "../jpeg_ls/global.h"
#undef MAX_COMPONENTS
#include "../jpeg_ls/bitio.h"
#include "../imagezero/iz_c.h"
#include "../jpeg_turbo/jpeglib.h"
#define ZSTD_STATIC_LINKING_ONLY
#include "../zstd/zstd.h"

#endif
