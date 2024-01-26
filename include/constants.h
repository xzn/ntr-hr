#ifndef CONSTANTS_H
#define CONSTANTS_H

#define PATH_MAX (0x100)

#define LOCAL_DBG_BUF_SIZE (0x200)
#define LOCAL_TID_BUF_COUNT (0x80)

#define IoBaseLcd (0x10202000 | 0x80000000)

#define COPY_REMOTE_MEMORY_TIMEOUT (5000000000LL)

#define NS_CONFIG_ADDR (0x06000000)
#define NS_CONFIG_MAX_SIZE (0x1000)

#define STACK_SIZE (0x4000)

#endif
