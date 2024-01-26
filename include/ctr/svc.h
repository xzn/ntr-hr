#ifndef SVC_H
#define SVC_H

#include "3dstypes.h"

#define CUR_PROCESS_HANDLE 0xFFFF8001

/// Arbitration modes.
typedef enum {
	ARBITRATION_SIGNAL                                  = 0, ///< Signal #value threads for wake-up.
	ARBITRATION_WAIT_IF_LESS_THAN                       = 1, ///< If the memory at the address is strictly lower than #value, then wait for signal.
	ARBITRATION_DECREMENT_AND_WAIT_IF_LESS_THAN         = 2, ///< If the memory at the address is strictly lower than #value, then decrement it and wait for signal.
	ARBITRATION_WAIT_IF_LESS_THAN_TIMEOUT               = 3, ///< If the memory at the address is strictly lower than #value, then wait for signal or timeout.
	ARBITRATION_DECREMENT_AND_WAIT_IF_LESS_THAN_TIMEOUT = 4, ///< If the memory at the address is strictly lower than #value, then decrement it and wait for signal or timeout.
} ArbitrationType;

/// Special value to signal all the threads
#define ARBITRATION_SIGNAL_ALL (-1)

/// Reset types (for use with events and timers)
typedef enum {
	RESET_ONESHOT = 0, ///< When the primitive is signaled, it will wake up exactly one thread and will clear itself automatically.
	RESET_STICKY  = 1, ///< When the primitive is signaled, it will wake up all threads and it won't clear itself automatically.
	RESET_PULSE   = 2, ///< Only meaningful for timers: same as ONESHOT but it will periodically signal the timer instead of just once.
} ResetType;

typedef enum {
	MEMOP_FREE    = 1, ///< Memory un-mapping
	MEMOP_RESERVE = 2, ///< Reserve memory
	MEMOP_ALLOC   = 3, ///< Memory mapping
	MEMOP_MAP     = 4, ///< Mirror mapping
	MEMOP_UNMAP   = 5, ///< Mirror unmapping
	MEMOP_PROT    = 6, ///< Change protection

	MEMOP_REGION_APP    = 0x100, ///< APPLICATION memory region.
	MEMOP_REGION_SYSTEM = 0x200, ///< SYSTEM memory region.
	MEMOP_REGION_BASE   = 0x300, ///< BASE memory region.

	MEMOP_OP_MASK     = 0xFF,    ///< Operation bitmask.
	MEMOP_REGION_MASK = 0xF00,   ///< Region bitmask.
	MEMOP_LINEAR_FLAG = 0x10000, ///< Flag for linear memory operations

	MEMOP_ALLOC_LINEAR = MEMOP_LINEAR_FLAG | MEMOP_ALLOC, ///< Allocates linear memory.
} MemOp;

/// Memory permission flags
typedef enum {
	MEMPERM_READ     = 1,          ///< Readable
	MEMPERM_WRITE    = 2,          ///< Writable
	MEMPERM_EXECUTE  = 4,          ///< Executable
	MEMPERM_READWRITE = MEMPERM_READ | MEMPERM_WRITE, ///< Readable and writable
	MEMPERM_READEXECUTE = MEMPERM_READ | MEMPERM_EXECUTE, ///< Readable and executable
	MEMPERM_DONTCARE = 0x10000000, ///< Don't care
} MemPerm;

/// Memory regions.
typedef enum
{
	MEMREGION_ALL = 0,         ///< All regions.
	MEMREGION_APPLICATION = 1, ///< APPLICATION memory.
	MEMREGION_SYSTEM = 2,      ///< SYSTEM memory.
	MEMREGION_BASE = 3,        ///< BASE memory.
} MemRegion;

/// Configuration flags for \ref DmaConfig.
enum {
	DMACFG_DST_IS_DEVICE = BIT(0), ///< DMA destination is a device/peripheral. Address will not auto-increment.
	DMACFG_SRC_IS_DEVICE = BIT(1), ///< DMA source is a device/peripheral. Address will not auto-increment.
	DMACFG_WAIT_AVAILABLE = BIT(2), ///< Make \ref svcStartInterProcessDma wait for the channel to be unlocked.
	DMACFG_KEEP_LOCKED = BIT(3), ///< Keep the channel locked after the transfer. Required for \ref svcRestartDma.
	DMACFG_USE_DST_CONFIG = BIT(6), ///< Use the provided destination device configuration even if the DMA destination is not a device.
	DMACFG_USE_SRC_CONFIG = BIT(7), ///< Use the provided source device configuration even if the DMA source is not a device.
};

/**
 * @brief Device configuration structure, part of \ref DmaConfig.
 * @note
 * - if (and only if) src/dst is a device, then src/dst won't be auto-incremented.
 * - the kernel uses DMAMOV instead of DMAADNH, when having to decrement (possibly working around an erratum);
 * this forces all loops to be unrolled -- you need to keep that in mind when using negative increments, as the kernel
 * uses a limit of 100 DMA instruction bytes per channel.
 */
typedef struct {
	s8 deviceId; ///< DMA device ID.
	s8 allowedAlignments; ///< Mask of allowed access alignments (8, 4, 2, 1).
	s16 burstSize; ///< Number of bytes transferred in a burst loop. Can be 0 (in which case the max allowed alignment is used as unit).
	s16 transferSize; ///< Number of bytes transferred in a "transfer" loop (made of burst loops).
	s16 burstStride; ///< Burst loop stride, can be <= 0.
	s16 transferStride; ///< "Transfer" loop stride, can be <= 0.
} DmaDeviceConfig;

/// Configuration stucture for \ref svcStartInterProcessDma.
typedef struct {
	s8 channelId; ///< Channel ID (Arm11: 0-7, Arm9: 0-1). Use -1 to auto-assign to a free channel (Arm11: 3-7, Arm9: 0-1).
	s8 endianSwapSize; ///< Endian swap size (can be 0).
	u8 flags; ///< DMACFG_* flags.
	u8 _padding;
	DmaDeviceConfig dstCfg; ///< Destination device configuration
	DmaDeviceConfig srcCfg; ///< Source device configuration
} DmaConfig;

/// The state of a memory block.
typedef enum {
	MEMSTATE_FREE       = 0,  ///< Free memory
	MEMSTATE_RESERVED   = 1,  ///< Reserved memory
	MEMSTATE_IO         = 2,  ///< I/O memory
	MEMSTATE_STATIC     = 3,  ///< Static memory
	MEMSTATE_CODE       = 4,  ///< Code memory
	MEMSTATE_PRIVATE    = 5,  ///< Private memory
	MEMSTATE_SHARED     = 6,  ///< Shared memory
	MEMSTATE_CONTINUOUS = 7,  ///< Continuous memory
	MEMSTATE_ALIASED    = 8,  ///< Aliased memory
	MEMSTATE_ALIAS      = 9,  ///< Alias memory
	MEMSTATE_ALIASCODE  = 10, ///< Aliased code memory
	MEMSTATE_LOCKED     = 11, ///< Locked memory
} MemState;

/// Memory information.
typedef struct {
	u32 base_addr; ///< Base address.
	u32 size;      ///< Size.
	u32 perm;      ///< Memory permissions. See @ref MemPerm
	u32 state;     ///< Memory state. See @ref MemState
} MemInfo;

/// Memory page information.
typedef struct {
	u32 flags; ///< Page flags.
} PageInfo;

/// Types of resource limit
typedef enum {
	RESLIMIT_PRIORITY       = 0,        ///< Thread priority
	RESLIMIT_COMMIT         = 1,        ///< Quantity of allocatable memory
	RESLIMIT_THREAD         = 2,        ///< Number of threads
	RESLIMIT_EVENT          = 3,        ///< Number of events
	RESLIMIT_MUTEX          = 4,        ///< Number of mutexes
	RESLIMIT_SEMAPHORE      = 5,        ///< Number of semaphores
	RESLIMIT_TIMER          = 6,        ///< Number of timers
	RESLIMIT_SHAREDMEMORY   = 7,        ///< Number of shared memory objects, see @ref svcCreateMemoryBlock
	RESLIMIT_ADDRESSARBITER = 8,        ///< Number of address arbiters
	RESLIMIT_CPUTIME        = 9,        ///< CPU time. Value expressed in percentage regular until it reaches 90.

	RESLIMIT_BIT            = BIT(31),  ///< Forces enum size to be 32 bits
} ResourceLimitType;

static inline void* getThreadLocalStorage(void)
{
	void* ret;
	__asm__ ("mrc p15, 0, %[data], c13, c0, 3" : [data] "=r" (ret));
	return ret;
}

u32* getThreadCommandBuffer(void);

Result svc_backDoor(void* callback);
Result svc_getThreadList(u32* threadCount, u32* threadIds, s32 threadIdMaxCount, Handle hProcess);
Result svc_getDmaState(u32* state, Handle dma);
Result svc_startInterProcessDma(Handle* hdma, Handle dstProcess, void* dst, Handle srcProcess, const void* src, u32 size, const DmaConfig* config);

Result svc_writeProcessMemory(Handle debug, void const* buffer, u32 addr, u32 size);
Result svc_readProcessMemory(void* buffer, Handle debug, u32 addr, u32 size);
Result svc_debugActiveProcess(Handle* handle_out, u32 pid);
Result svc_getProcessList(s32* processCount, u32* processIds, s32 processIdMaxCount);
Result svc_getProcessInfo(s64 *out, Handle process, u32 type);

Result svc_controlProcessMemory(Handle hProcess, void* Addr0, void* Addr1, u32 size, u32 Type, u32 Permissions);

Result svc_openProcess(Handle* process, u32 processId);
Result svc_addCodeSegment(u32 addr, u32 size);
Result svc_flushProcessDataCache(Handle handle, u32 addr, u32 size);
Result svc_invalidateProcessDataCache(Handle handle, u32 addr, u32 size);
Result svc_controlMemory(u32* outaddr, u32 addr0, u32 addr1, u32 size, u32 operation, u32 permissions); //(outaddr is usually the same as the input addr0)
void svc_exitProcess(void);
Result svc_createThread(Handle* thread, ThreadFunc entrypoint, u32 arg, u32* stacktop, s32 threadpriority, s32 processorid);
void svc_exitThread(void) __attribute__((noreturn));
void svc_sleepThread(s64 ns);
Result svc_createMutex(Handle* mutex, bool initialLocked);
Result svc_releaseMutex(Handle handle);
Result svc_createSemaphore(Handle* semaphore, s32 initial_count, s32 max_count);
Result svc_releaseSemaphore(s32* count, Handle semaphore, s32 releaseCount);
Result svc_createEvent(Handle* event, u8 resettype);
Result svc_signalEvent(Handle handle);
Result svc_clearEvent(Handle handle);
Result svc_createMemoryBlock(Handle* memblock, u32 addr, u32 size, u32 mypermission, u32 otherpermission);
Result svc_mapMemoryBlock(Handle memblock, u32 addr, u32 mypermissions, u32 otherpermission);
Result svc_unmapMemoryBlock(Handle memblock, u32 addr);
Result svc_waitSynchronization1(Handle handle, s64 nanoseconds);
Result svc_waitSynchronizationN(s32* out, Handle* handles, s32 handlecount, bool waitAll, s64 nanoseconds);
Result svc_arbitrateAddress(Handle arbiter, u32 addr, u8 type, s32 value, s64 nanoseconds);
Result svc_closeHandle(Handle handle);
u64 svc_getSystemTick(void);
Result svc_getSystemInfo(s64* out, u32 type, s32 param);
Result svc_connectToPort(volatile Handle* out, const char* portName);
Result svc_sendSyncRequest(Handle session);
Result svc_getProcessId(u32 *out, Handle handle);
Result svc_getThreadId(u32 *out, Handle handle);
Result svc_setThreadIdealProcessor(Handle handle, u32 processorid);
Result svc_restartDma(Handle h, void * dst, void const* src, unsigned int size, signed char flag);
Result svc_kernelSetState(unsigned int Type, unsigned int Param0, unsigned int Param1, unsigned int Param2);
Result svc_openThread(Handle *thread, Handle process, u32 threadId);

Result svc_duplicateHandle(Handle* out, Handle original);
Result svc_mapProcessMemory(Handle process, u32 destAddress, u32 size);

Result svcCreatePort(Handle* portServer, Handle* portClient, const char* name, s32 maxSessions);
Result svcAcceptSession(Handle* session, Handle port);
Result svcReplyAndReceive(s32* index, const Handle* handles, s32 handleCount, Handle replyTarget);
Result svcSetThreadPriority(Handle thread, s32 prio);
Result svcCreateAddressArbiter(Handle *arbiter);
Result svcArbitrateAddressNoTimeout(Handle arbiter, u32 addr, ArbitrationType type, s32 value);
Result svcQueryMemory(MemInfo* info, PageInfo* out, u32 addr);
Result svcMapMemoryBlock(Handle memblock, u32 addr, MemPerm my_perm, MemPerm other_perm);
Result svcUnmapMemoryBlock(Handle memblock, u32 addr);

Result svcArbitrateAddress(Handle arbiter, u32 addr, u8 type, s32 value, s64 nanoseconds);
Result svcCloseHandle(Handle handle);
Result svcCreateEvent(Handle* event, u8 resettype);
Result svcSignalEvent(Handle handle);
Result svcClearEvent(Handle handle);
Result svcWaitSynchronization(Handle handle, s64 nanoseconds);
Result svcSendSyncRequest(Handle session);
Result svcGetResourceLimit(Handle* resourceLimit, Handle process);
Result svcGetResourceLimitLimitValues(s64* values, Handle resourceLimit, ResourceLimitType* names, s32 nameCount);
Result svcGetResourceLimitCurrentValues(s64* values, Handle resourceLimit, ResourceLimitType* names, s32 nameCount);
Result svcSetResourceLimitValues(Handle resourceLimit, const ResourceLimitType* names, const s64* values, s32 nameCount);

static inline u32 IPC_MakeHeader(u16 command_id, unsigned normal_params, unsigned translate_params) {
	return ((u32) command_id << 16) | (((u32) normal_params & 0x3F) << 6) | (((u32) translate_params & 0x3F) << 0);
}

static inline u32 IPC_Desc_StaticBuffer(u32 size, unsigned buffer_id)
{
	return (size << 14) | ((buffer_id & 0xF) << 10) | 0x2;
}

static inline u32 IPC_Desc_SharedHandles(unsigned number)
{
	return ((u32)(number - 1) << 26);
}

#endif
