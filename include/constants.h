#ifndef CONSTANTS_H
#define CONSTANTS_H

#define PATH_MAX (0x100)

#define LOCAL_DBG_BUF_SIZE (0x200)
#define LOCAL_TID_BUF_COUNT (0x80)

#define IoBaseLcd (0x10202000 | 0x80000000)
#define IoBasePad (10146000 | 0x80000000)

#define COPY_REMOTE_MEMORY_TIMEOUT (5000000000LL)

#define NS_CONFIG_ADDR (0x06000000)
#define NS_CONFIG_MAX_SIZE (0x1000)
#define NS_SOC_ADDR (0x06f00000)

#define LOCAL_POOL_ADDR (0x07000000)
#define PLG_POOL_ADDR (0x06200000)

#define NS_MENU_LISTEN_PORT (8000)
#define NS_HOOK_LISTEN_PORT (5000)

#define DEBUG_BUF_SIZE (0x2000)

#define STACK_SIZE (0x4000)

#endif
