#ifndef RP_COMMON_H
#define RP_COMMON_H

#include "global.h"
#include "ctr/syn.h"
#include "ctr/res.h"

#include "umm_malloc.h"
#include "ikcp.h"
#include "libavcodec/jpegls.h"
#include "libavcodec/get_bits.h"
#include "libavfilter/motion_estimation.h"
#include "libavfilter/scene_sad.h"
#include "../jpeg_ls/global.h"
#include "../jpeg_ls/bitio.h"
#include "../imagezero/iz_c.h"
#include "xxhash.h"

#define RP_ME_INTERPOLATE (1)
#define RP_ENCODE_MULTITHREAD (1)
// (0) svc (1) syn
#define RP_SYN_METHOD (0)
#define RP_SYN_EX (1)

#define RP_SVC_MS(ms) ((u64)ms * 1000 * 1000)

#define RP_SYN_WAIT_MAX RP_SVC_MS(2000)
#define RP_SYN_WAIT_IDLE RP_SVC_MS(1000)
#define RP_THREAD_LOOP_WAIT_COUNT (25)
#define RP_THREAD_LOOP_SLOW_WAIT RP_SVC_MS(50)
#define RP_THREAD_LOOP_MED_WAIT RP_SVC_MS(25)
#define RP_THREAD_LOOP_FAST_WAIT RP_SVC_MS(10)
#define RP_THREAD_LOOP_IDLE_WAIT RP_SVC_MS(100)
#define RP_THREAD_LOOP_ULTRA_FAST_WAIT RP_SVC_MS(1)
#define RP_THREAD_KCP_LOOP_WAIT RP_SVC_MS(1)
#define RP_THREAD_ENCODE_BEGIN_WAIT RP_SVC_MS(50)
#define RP_THREAD_NETWORK_BEGIN_WAIT RP_SVC_MS(100)

#define KCP_TIMEOUT_TICKS (2000 * SYSTICK_PER_MS)
#define RP_PACKET_SIZE (KCP_PACKET_SIZE - IKCP_OVERHEAD)

#define RP_DEST_PORT (8001)
#define RP_SCREEN_BUFFER_SIZE (SCREEN_WIDTH_MAX * SCREEN_HEIGHT * 4)
#define RP_UMM_HEAP_SIZE (192 * 1024)
#define RP_STACK_SIZE (0x8000)
#define RP_MISC_STACK_SIZE (0x1000)
#define RP_CONTROL_RECV_BUFFER_SIZE (2000)

#define IKCP_OVERHEAD (24)
#define KCP_PACKET_SIZE 1448
#define NWM_HEADER_SIZE (0x2a + 8)
#define NWM_PACKET_SIZE (KCP_PACKET_SIZE + NWM_HEADER_SIZE)

#define RP_KCP_MAGIC 0x87654321

#define RP_BANDWIDTH_CONTROL_RATIO_NUM 2
#define RP_BANDWIDTH_CONTROL_RATIO_DENUM 3

#define RP_KCP_MIN_MINRTO (10)
#define RP_KCP_MIN_SNDWNDSIZE (32)
#define RP_ME_MIN_BLOCK_SIZE_LOG2 (2)
#define RP_ME_MIN_BLOCK_SIZE (1 << RP_ME_MIN_BLOCK_SIZE_LOG2)
#define RP_ME_MIN_SEARCH_PARAM (8)

#define RP_MAX(a, b) ((a) > (b) ? (a) : (b))
#define RP_MIN(a, b) ((a) > (b) ? (b) : (a))

#define SCREEN_TOP 0
#define SCREEN_BOT 1
#define SCREEN_MAX 2

#define SCREEN_CHOOSE(s, a, b) ((s) == SCREEN_TOP ? (a) : (s) == SCREEN_BOT ? (b) : 0)
#define SCREEN_CHOOSE_MAX(c, ...) RP_MAX(c(SCREEN_TOP, ## __VA_ARGS__), c(SCREEN_BOT, ## __VA_ARGS__))

#define SCREEN_WIDTH(s) SCREEN_CHOOSE(s, 400, 320)
#define SCREEN_WIDTH_MAX SCREEN_CHOOSE_MAX(SCREEN_WIDTH)
#define SCREEN_HEIGHT (240)

#define SCREEN_DS_WIDTH(s, ds) DS_DIM(SCREEN_WIDTH(s), ds)
#define SCREEN_DS_HEIGHT(ds) DS_DIM(SCREEN_HEIGHT, ds)

// (+ 1) since motion estimation operates on images downscaled at least once
#define ME_DS_WIDTH(s, ds) SCREEN_DS_WIDTH(s, 1 + RP_ME_MIN_BLOCK_SIZE_LOG2 + ds)
#define ME_DS_HEIGHT(ds) SCREEN_DS_HEIGHT(1 + RP_ME_MIN_BLOCK_SIZE_LOG2 + ds)

#define SCREEN_PADDED_SIZE(s) PADDED_SIZE(SCREEN_WIDTH(s), SCREEN_HEIGHT)
#define SCREEN_PADDED_DS_SIZE(s, ds) PADDED_SIZE(SCREEN_DS_WIDTH(s, ds), SCREEN_DS_HEIGHT(ds))

#define SCREEN_SIZE_MAX SCREEN_CHOOSE_MAX(SCREEN_PADDED_SIZE)
#define SCREEN_DS_SIZE_MAX(ds) SCREEN_CHOOSE_MAX(SCREEN_PADDED_DS_SIZE, ds)
#define ME_PADDED_DS_SIZE(s, ds) PADDED_SIZE(ME_DS_WIDTH(s, ds), ME_DS_HEIGHT(ds))
#define ME_PADDED_SIZE(s) ME_PADDED_DS_SIZE(s, 0)
#define ME_SIZE_MAX SCREEN_CHOOSE_MAX(ME_PADDED_SIZE)

#define RP_TOP_BOT_STR(top_bot) ((top_bot) == 0 ? "top" : "bot")

#define PADDED_HEIGHT(h) ((h) + LEFTMARGIN + RIGHTMARGIN)
#define PADDED_SIZE(w, h) ((w) * PADDED_HEIGHT(h))
#define DS_DIM(w, ds) ((w) >> (ds))

#define RP_ASSERT(c, ...) do { if (!(c)) { nsDbgPrint(__VA_ARGS__); } } while (0)
#define RP_DBG(c, ...) RP_ASSERT(!(c), __VA_ARGS__)

#define SYSTICK_PER_US (268)
#define SYSTICK_PER_MS (268123)
#define SYSTICK_PER_SEC (268123480)

// attribute aligned
#define ALIGN_4 ALIGN(4)
// assume aligned
#define ASSUME_ALIGN_4(a) (a = __builtin_assume_aligned (a, 4))
#define UNUSED __attribute__((unused))
#define FALLTHRU __attribute__((fallthrough));
#define ALWAYS_INLINE __attribute__((always_inline)) inline

#define rshift_to_even(n, s) (((n) + ((s) > 1 ? (1 << ((s) - 1)) : 0)) >> (s))
#define srshift_to_even(t, n, s) ((t)((n) + ((s) > 1 ? (1 << ((s) - 1)) : 0)) >> (s))

#define RP_ENCODE_THREAD_COUNT (1 + RP_ENCODE_MULTITHREAD)
// (+ 1) for screen/network transfer then (+ 1) again for start/finish at different time
#define RP_ENCODE_BUFFER_COUNT (RP_ENCODE_THREAD_COUNT + 2)
// (+ 1) for motion estimation reference
#define RP_IMAGE_BUFFER_COUNT (RP_ENCODE_THREAD_COUNT + 1)
#define RP_IMAGE_ME_SELECT_BITS (6)
#define RP_IMAGE_FRAME_N_BITS (3)
#define RP_IMAGE_FRAME_N_RANGE (1 << RP_IMAGE_FRAME_N_BITS)

typedef u32 (*NWMSendPacket_t)(u8 *, u32);
extern NWMSendPacket_t nwmSendPacket;

enum {
    RP_MAIN_ENCODE_THREAD_ID,
    RP_SECOND_ENCODE_THREAD_ID,
    RP_NETWORK_TRANSFER_THREAD_ID = -1,
    RP_SCREEN_TRANSFER_THREAD_ID = -2,
};

enum {
    RP_ENCODER_FFMPEG_JLS,
    RP_ENCODER_HP_JLS,
    RP_ENCODER_JLS_COUNT,
    RP_ENCODER_IMAGE_ZERO = RP_ENCODER_JLS_COUNT
};

#endif
