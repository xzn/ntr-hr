#ifndef CONSTANTS_H
#define CONSTANTS_H

#include "3ds/types.h"

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

#define COPY_REMOTE_MEMORY_TIMEOUT (-1)
#define PM_INIT_READY_TIMEOUT (-1)
#define NWM_INIT_READY_TIMEOUT (-1)

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
#define RP_CORE_COUNT_MAX (3)
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
#define RP_CORE_COUNT_DEFAULT RP_CORE_COUNT_MAX

#define NWM_HDR_SIZE (0x2a + 8)
#define DATA_HDR_SIZE (4)
#define PACKET_SIZE (1448)

#define DEBUG_BUF_SIZE (0x2000)

#define SMALL_STACK_SIZE (0x1000)
#define STACK_SIZE (0x4000)
#define RP_THREAD_STACK_SIZE (0x10000)

#define DBG_CL_FATAL (0x10000ff)
#define DBG_CL_MSG (DBG_CL_FATAL)
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

#define NWM_HEAP_SIZE (0x4000)
#define NWM_WORK_COUNT (2)
#define NWM_THREAD_WAIT_NS (100000000)

#define RP_COMPRESSED_SIZE_MAX (0x30000)

#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define ROUND_UP(n, d) (DIV_ROUND_UP(n, d) * (d))

#define IKCP_WND_SND_MAX (DIV_ROUND_UP(RP_COMPRESSED_SIZE_MAX, PACKET_SIZE - IKCP_OVERHEAD_CONST - DATA_HDR_SIZE) * NWM_WORK_COUNT)
#define IKCP_WND_RCV_CONST 1
#define IKCP_OVERHEAD_CONST 4
#define IKCP_SEG_MEM_SIZE_CONST (sizeof(struct IKCPSEG))

#define ARP_PACKET_COUNT 1
#define ARP_OVERHEAD_CONST 2
#define ARP_DATA_SIZE (PACKET_SIZE - ARP_OVERHEAD_CONST)

#define SEND_BUFS_MP_COUNT (IKCP_WND_SND_MAX + RP_CORE_COUNT_MAX * NWM_WORK_COUNT)

#define RP_CONFIG_RELIABLE_STREAM_FLAG (1 << 30)

#endif
