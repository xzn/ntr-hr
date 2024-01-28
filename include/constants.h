#ifndef CONSTANTS_H
#define CONSTANTS_H

#define PATH_MAX (0x100)

#define LOCAL_TITLE_BUF_SIZE (0x80)
#define LOCAL_MSG_BUF_SIZE (0x200)
#define LOCAL_TID_BUF_COUNT (0x80)
#define LOCAL_DIR_LIST_BUF_COUNT (0x1000)

// Require Luma3DS PA-VA mapping
#define IoBaseLcd (0x10202000 + 0x80000000)
#define IoBasePad (0x10146000 + 0x80000000)
#define IoBasePdc (0x10400000 + 0x80000000)

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

#define COPY_REMOTE_MEMORY_TIMEOUT (-1)

#define NS_CONFIG_ADDR (0x06000000)
#define NS_CONFIG_MAX_SIZE (0x1000)
#define NS_SOC_ADDR (0x06f00000)
#define NS_SOC_SHARED_BUF_SIZE (0x10000)
#define NS_CTX_ADDR (NS_SOC_ADDR + NS_SOC_SHARED_BUF_SIZE)

#define PLG_POOL_ADDR (0x07000000)
#define PLG_MEM_ADDR (0x06200000)
#define PLG_LOADER_ADDR PLG_POOL_ADDR

#define PROC_START_ADDR (0x00100000)

#define NS_MENU_LISTEN_PORT (8000)
#define NS_HOOK_LISTEN_PORT (5000)

#define DEBUG_BUF_SIZE (0x2000)

#define STACK_SIZE (0x4000)

#endif
