#ifndef CONSTANTS_H
#define CONSTANTS_H

#include "func.h"

#define ALIGNED(a) __attribute__ ((aligned (a)))

#define PATH_MAX (0x100)

#define LOCAL_TITLE_BUF_SIZE (0x80)
#define LOCAL_MSG_BUF_SIZE (0x200)
#define LOCAL_OPT_TEXT_BUF_SIZE (0x40)
#define LOCAL_TID_BUF_COUNT (0x80)
#define LOCAL_PID_BUF_COUNT (0x100)

#define DBG_VERBOSE_TITLE "[%"PRId32".%06"PRId32"][%"PRIx32"]%s:%d:%s"

// Require Luma3DS PA-VA mapping
static const u32 IoBaseLcd = 0x10202000 + 0x80000000;
static const u32 IoBasePad = 0x10146000 + 0x80000000;
static const u32 IoBasePdc = 0x10400000 + 0x80000000;

#define DIRECTIONAL_KEYS (KEY_DOWN | KEY_UP | KEY_LEFT | KEY_RIGHT)

#define REG32(reg) (*(vu32 *)reg)
// From Luma3DS
#define GPU_FB_TOP_SIZE             (IoBasePdc + 0x45c)
#define GPU_FB_TOP_LEFT_ADDR_1      (IoBasePdc + 0x468)
#define GPU_FB_TOP_LEFT_ADDR_2      (IoBasePdc + 0x46C)
#define GPU_FB_TOP_FMT              (IoBasePdc + 0x470)
#define GPU_FB_TOP_SEL              (IoBasePdc + 0x478)
#define GPU_FB_TOP_COL_LUT_INDEX    (IoBasePdc + 0x480)
#define GPU_FB_TOP_COL_LUT_ELEM     (IoBasePdc + 0x484)
#define GPU_FB_TOP_STRIDE           (IoBasePdc + 0x490)
#define GPU_FB_TOP_RIGHT_ADDR_1     (IoBasePdc + 0x494)
#define GPU_FB_TOP_RIGHT_ADDR_2     (IoBasePdc + 0x498)

#define GPU_FB_BOTTOM_SIZE          (IoBasePdc + 0x55c)
#define GPU_FB_BOTTOM_ADDR_1        (IoBasePdc + 0x568)
#define GPU_FB_BOTTOM_ADDR_2        (IoBasePdc + 0x56C)
#define GPU_FB_BOTTOM_FMT           (IoBasePdc + 0x570)
#define GPU_FB_BOTTOM_SEL           (IoBasePdc + 0x578)
#define GPU_FB_BOTTOM_COL_LUT_INDEX (IoBasePdc + 0x580)
#define GPU_FB_BOTTOM_COL_LUT_ELEM  (IoBasePdc + 0x584)
#define GPU_FB_BOTTOM_STRIDE        (IoBasePdc + 0x590)

#define GPU_PSC0_CNT                (IoBasePdc + 0x01C)
#define GPU_PSC1_CNT                (IoBasePdc + 0x02C)

#define GPU_TRANSFER_CNT            (IoBasePdc + 0xC18)
#define GPU_CMDLIST_CNT             (IoBasePdc + 0x18F0)

#define LCD_TOP_BRIGHTNESS          (IoBaseLcd + 0x240)
#define LCD_TOP_FILLCOLOR           (IoBaseLcd + 0x204)
#define LCD_BOT_BRIGHTNESS          (IoBaseLcd + 0xA40)
#define LCD_BOT_FILLCOLOR           (IoBaseLcd + 0xA04)

#define PDN_LGR_SOCMODE (0x10141300 + 0x80000000)

#define COPY_REMOTE_MEMORY_TIMEOUT (100000000)
#define PM_INIT_READY_TIMEOUT (200000000)
#define NWM_INIT_READY_TIMEOUT (200000000)

#define NS_CONFIG_ADDR (0x06000000)
#define NS_CONFIG_MAX_SIZE (0x1000)
#define NS_SOC_ADDR (0x06f00000)
#define NS_SOC_SHARED_BUF_SIZE (0x10000)
#define NS_CTX_ADDR (NS_SOC_ADDR + STACK_SIZE)

#define PLG_POOL_ADDR (0x07000000)
#define PLG_MEM_ADDR (0x06200000)

#define PROC_START_ADDR (0x00100000)

#define NS_MENU_LISTEN_PORT (8000)
#define NS_HOOK_LISTEN_PORT (5000)
// Manual RP init from NTR menu
// SRC is 3DS, DST is PC
// Intentionally different from the RP default to avoid sending
// garbage to unsuspecting viewer
#define NWM_INIT_SRC_PORT (8001)
#define NWM_INIT_DST_PORT (8000)
// RP data ports
#define RP_SRC_PORT (8000)
#define RP_DST_PORT_DEFAULT (8001)
#define RP_THREAD_PRIO_DEFAULT RP_THREAD_PRIO_MAX
#define RP_CORE_COUNT_MIN (1)
#define RP_CORE_COUNT_DEFAULT RP_CORE_COUNT_MAX
#ifdef OLD_3DS
#define RP_CORE_COUNT_MAX (1)
#define RP_NWM_PROCESS (0x19)
#elif defined(NEW_3DS)
#define RP_CORE_COUNT_MAX (3)
#define RP_NWM_PROCESS (0x1a)
#else
#define RP_CORE_COUNT_MAX (ntrConfig->isNew3DS ? 3 : 1)
#define RP_NWM_PROCESS (ntrConfig->isNew3DS ? 0x1a : 0x19)
#endif
#define NTR_LOADER_REGION ntr_loader_region
#define RP_QUALITY_DEFAULT (75)
#define RP_QUALITY_MIN (10)
#define RP_QUALITY_MAX (100)
// 2.0 MBps or 16 Mbps
#define RP_QOS_DEFAULT (2 * 1024 * 1024)
// 0.5 MBps or 4 Mbps
#define RP_QOS_MIN (1 * 1024 * 1024 / 2)
// 2.5 MBps or 20 Mbps
#define RP_QOS_MAX (5 * 1024 * 1024 / 2)
#define RP_PORT_MIN (1024)
#define RP_PORT_MAX (65535)
#define RP_THREAD_PRIO_MIN (0x10)
#define RP_THREAD_PRIO_MAX (0x3f)

#define NWM_HDR_SIZE (0x2a + 8)
#define DATA_HDR_SIZE (4)
#define PACKET_SIZE (1448)

#define DEBUG_BUF_SIZE (0x2000)

#define SMALL_STACK_SIZE (0x1000)
#define STACK_SIZE (0x4000)
#define RP_THREAD_STACK_SIZE (0x10000)

#define DBG_CL_FATAL (0x100007f)
#define DBG_CL_MSG (0x100ffff)
#define DBG_CL_INFO (0x1ff0000)
#define DBG_CL_USE_DBG (0x17f7f7f)
#define DBG_CL_USE_DBG_FAIL (0xff00ff)
#define DBG_CL_USE_INJECT (0x100ff00)

#define RES_HANDLE_CLOSED (0xC920181A)
#define RES_TIMEOUT (0x09401BFE)

#define SVC_PORT_NWM "nwm:rp"
#define SVC_PORT_MENU "menu:ns"

enum {
	SVC_NWM_CMD_OVERLAY_CALLBACK = 1,
	SVC_NWM_CMD_PARAMS_UPDATE,
	SVC_NWM_CMD_GAME_PID_UPDATE,
};

enum {
	SVC_MENU_CMD_DBG_PRINT = 1,
	SVC_MENU_CMD_SHOW_MSG,
};

enum {
	RP_CHROMASS_420,
	RP_CHROMASS_422,
	RP_CHROMASS_444,
	RP_CHROMASS_MIN = RP_CHROMASS_420,
	RP_CHROMASS_MAX = RP_CHROMASS_444,
};

enum {
	RP_DOWNSAMPLE_NONE,
	RP_DOWNSAMPLE_CHECKER,
	RP_DOWNSAMPLE_EVEN_ODD,
	RP_DOWNSAMPLE_QUARTER,
	RP_DOWNSAMPLE_MIN = RP_DOWNSAMPLE_NONE,
	RP_DOWNSAMPLE_MAX = RP_DOWNSAMPLE_QUARTER,
};

enum {
	RP_COLOR_BIAS_NONE,
	RP_COLOR_BIAS_1,
	RP_COLOR_BIAS_2,
	RP_COLOR_BIAS_MIN = RP_COLOR_BIAS_NONE,
	RP_COLOR_BIAS_MAX = RP_COLOR_BIAS_2,
};

enum {
	RP_FPS_LIMIT_NONE,
	RP_FPS_LIMIT_1,
	RP_FPS_LIMIT_2,
	RP_FPS_LIMIT_3,
	RP_FPS_LIMIT_4,
	RP_FPS_LIMIT_5,
	RP_FPS_LIMIT_6,
	RP_FPS_LIMIT_10,
	RP_FPS_LIMIT_12,
	RP_FPS_LIMIT_15,
	RP_FPS_LIMIT_20,
	RP_FPS_LIMIT_30,
	RP_FPS_LIMIT_MIN = RP_FPS_LIMIT_NONE,
	RP_FPS_LIMIT_MAX = RP_FPS_LIMIT_30,
};

enum {
	RP_SCREEN_TOP,
	RP_SCREEN_BOT,
	RP_SCREEN_COUNT,
};

#define RP_DELTA_Q_COEFS_COUNT (1)
// #define RP_DELTA_Q_COEFS_COUNT (3)

#define NWM_HEAP_SIZE (0x4000)
#ifdef NEW_3DS
#define NWM_WORK_COUNT (2)
#elif defined(OLD_3DS)
#define NWM_WORK_COUNT (1)
#endif
#define NWM_THREAD_WAIT_NS (100000000)

// JPEG
// #define RP_COMPRESSED_SIZE_MAX (0x30000)
// Lossless
#define RP_COMPRESSED_SIZE_MAX (0x48000)

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define ROUND_UP(n, d) (DIV_ROUND_UP(n, d) * (d))

#define ARQ_OVERHEAD_SIZE 2
#define ARQ_DATA_SIZE (PACKET_SIZE - ARQ_OVERHEAD_SIZE)
#define ARQ_DATA_HDR_SIZE 2
#define ARQ_RP_DATA_SIZE (ARQ_DATA_SIZE - ARQ_DATA_HDR_SIZE)
#define RP_DATA_SIZE (PACKET_SIZE - DATA_HDR_SIZE)
#define RP_LOSSLESS_HDR_SIZE 2
#define RP_LOSSLESS_DATA_SIZE (RP_DATA_SIZE - RP_LOSSLESS_HDR_SIZE)
// JPEG
// #define RP_COMPRESSED_COUNT_MAX (DIV_ROUND_UP(RP_COMPRESSED_SIZE_MAX, RP_DATA_SIZE) * NWM_WORK_COUNT)
// Lossless
#define RP_COMPRESSED_COUNT_MAX (DIV_ROUND_UP(RP_COMPRESSED_SIZE_MAX, RP_LOSSLESS_DATA_SIZE) * NWM_WORK_COUNT)
#define RP_QOS_PACKET_RATE_MAX (DIV_ROUND_UP(RP_QOS_MAX, PACKET_SIZE))
// 250 ms or 1/4 of a second of buffered packets
#define ARQ_PREFERRED_BUFFER_DURATION_FACTOR (4)
#define ARQ_PREFERRED_COUNT_MAX DIV_ROUND_UP(RP_QOS_PACKET_RATE_MAX, ARQ_PREFERRED_BUFFER_DURATION_FACTOR)
// 25 ms or 1/40 of a second of queued packets for send
#define ARQ_CUR_COUNT_MAX DIV_ROUND_UP(RP_QOS_PACKET_RATE_MAX, 40)
// Additional allocable count is multiplied by max recovery to original ratio for FEC
#define ARQ_CUR_COUNT_MAX_2 DIV_ROUND_UP(ARQ_PREFERRED_COUNT_MAX, 2)
// Additional allocable count is multiplied by max recovery to original ratio
#define ARQ_PREFERRED_COUNT_MAX_2 (ARQ_PREFERRED_COUNT_MAX * 2)
// Additional count for buffer room for encoding
#define RP_ARQ_ENCODE_COUNT (RP_CORE_COUNT_MAX * NWM_WORK_COUNT)
#define RP_ARQ_ENCODE_COUNT_MAX (ARQ_PREFERRED_COUNT_MAX + RP_ARQ_ENCODE_COUNT)
// Additional count for audio packets
#define RP_ARQ_AUDIO_COUNT 32
// Additional count for finalizing frames
#define RP_FRAME_RATE_EXPECTED_MAX (120)
#define RP_ARQ_TERM_COUNT ((RP_CORE_COUNT_MAX + 1) * DIV_ROUND_UP(RP_FRAME_RATE_EXPECTED_MAX, ARQ_PREFERRED_BUFFER_DURATION_FACTOR))
#define RP_ARQ_EXTRA_COUNT_MAX (RP_ARQ_TERM_COUNT + RP_ARQ_AUDIO_COUNT)
#define RP_ARQ_PREFERRED_COUNT_MAX (RP_ARQ_ENCODE_COUNT_MAX + RP_ARQ_EXTRA_COUNT_MAX)
// Includes FEC_OVERHEAD_SIZE
#define ARQ_SEG_SIZE (sizeof(struct IKCPSEG))

#define FEC_OVERHEAD_SIZE 2
#define FEC_DATA_SIZE (PACKET_SIZE - FEC_OVERHEAD_SIZE)

_Static_assert((NWM_HDR_SIZE + FEC_OVERHEAD_SIZE) % sizeof(void *) == 0, "Need adjusting overhead for alignment.");
_Static_assert(RP_DATA_SIZE % sizeof(void *) == 0, "Need adjusting packet size for alignment.");

#define SEND_BUFS_DATA_COUNT MAX(RP_COMPRESSED_COUNT_MAX, RP_ARQ_PREFERRED_COUNT_MAX)

#define RP_CONFIG_FLAG_RELIABLE_STREAM (1 << 30)
#define RP_CONFIG_FLAG_RELIABLE_STREAM_DELTA (1 << 31)
#define RP_CONFIG_FLAG_LOSSLESS (1 << 29)
#define RP_CONFIG_FLAG_LOSSLESS_DATA_MASK ((1 << 2) - 1)
#define RP_CONFIG_FLAG_LOSSLESS_DATA_SHIFT (27)
#define RP_CONFIG_FLAG_LOSSLESS_DATA (RP_CONFIG_FLAG_LOSSLESS_DATA_MASK << RP_CONFIG_FLAG_LOSSLESS_DATA_SHIFT)
#define RP_CONFIG_FLAGS (RP_CONFIG_FLAG_RELIABLE_STREAM | RP_CONFIG_FLAG_RELIABLE_STREAM_DELTA | RP_CONFIG_FLAG_LOSSLESS)

#define RP_CONFIG_PORT_MASK (0xffff)
#define RP_CONFIG_FLAGS_DATA_MASK (0xffff0000)

#define RP_KCP_HDR_QUALITY_NBITS (7)
#define RP_KCP_HDR_CHROMASS_NBITS (2)
#define RP_KCP_HDR_DOWNSAMPLE_NBITS (2)

#define RP_KCP_EXHDR_EVEN_ODD_NBITS (1)

#endif
