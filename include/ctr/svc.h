#ifndef SVC_H
#define SVC_H

#include "3dstypes.h"

typedef enum{	
	MEMOP_FREE = 1,
	MEMOP_RESERVE = 2,
	MEMOP_COMMIT = 3,
	MEMOP_MAP = 4,
	MEMOP_UNMAP = 5,
	MEMOP_PROTECT = 6,
	MEMOP_REGION_APP = 0x100,
	MEMOP_REGION_SYSTEM = 0x200,
	MEMOP_REGION_BASE = 0x300,
	MEMOP_LINEAR = 0x1000,
}MEMORY_OPERATION;

/// Arbitration modes.
typedef enum {
	ARBITRATION_SIGNAL                                  = 0, ///< Signal #value threads for wake-up.
	ARBITRATION_WAIT_IF_LESS_THAN                       = 1, ///< If the memory at the address is strictly lower than #value, then wait for signal.
	ARBITRATION_DECREMENT_AND_WAIT_IF_LESS_THAN         = 2, ///< If the memory at the address is strictly lower than #value, then decrement it and wait for signal.
	ARBITRATION_WAIT_IF_LESS_THAN_TIMEOUT               = 3, ///< If the memory at the address is strictly lower than #value, then wait for signal or timeout.
	ARBITRATION_DECREMENT_AND_WAIT_IF_LESS_THAN_TIMEOUT = 4, ///< If the memory at the address is strictly lower than #value, then decrement it and wait for signal or timeout.
} ArbitrationType;

/// Reset types (for use with events and timers)
typedef enum {
	RESET_ONESHOT = 0, ///< When the primitive is signaled, it will wake up exactly one thread and will clear itself automatically.
	RESET_STICKY  = 1, ///< When the primitive is signaled, it will wake up all threads and it won't clear itself automatically.
	RESET_PULSE   = 2, ///< Only meaningful for timers: same as ONESHOT but it will periodically signal the timer instead of just once.
} ResetType;

#define ARBITRATION_SIGNAL_ALL (-1)

	u32* getThreadCommandBuffer(void);
	u32* getThreadStaticBuffers(void);
	static inline void* getThreadLocalStorage(void) {
		void* ret;
		__asm__ ("mrc p15, 0, %[data], c13, c0, 3" : [data] "=r" (ret));
		return ret;
	}

	Result svc_backDoor(void* callback);
	Result svc_getThreadList(u32* threadCount, u32* threadIds, s32 threadIdMaxCount, Handle hProcess);
	Result svc_getDmaState(u32* state, Handle dma);
	Result svc_startInterProcessDma(Handle* hdma, Handle dstProcess, void* dst, Handle srcProcess, const void* src, u32 size, u32* config);

	Result svc_writeProcessMemory(Handle debug, void const* buffer, u32 addr, u32 size);
	Result svc_readProcessMemory(void* buffer, Handle debug, u32 addr, u32 size);
	Result svc_debugActiveProcess(s32* handle_out, u32 pid);
	Result svc_getProcessList(s32* processCount, u32* processIds, s32 processIdMaxCount);

	Result svc_controlProcessMemory(Handle hProcess, void* Addr0, void* Addr1, u32 size, u32 Type, u32 Permissions);

	Result svc_openProcess(Handle* process, u32 processId);
	Result svc_addCodeSegment(u32 addr, u32 size);
	Result svc_flushProcessDataCache(Handle handle, u32 addr, u32 size);
	Result svc_invalidateProcessDataCache(Handle handle, u32 addr, u32 size);
	Result svc_controlMemory(u32* outaddr, u32 addr0, u32 addr1, u32 size, u32 operation, u32 permissions); //(outaddr is usually the same as the input addr0)
	void svc_exitProcess(void);
	Result svc_createThread(Handle* thread, ThreadFunc entrypoint, u32 arg, u32* stacktop, s32 threadpriority, s32 processorid);
	void svc_exitThread();
	void svc_sleepThread(s64 ns);
	Result svc_openThread(Handle *thread, Handle process, u32 threadId);
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
	Result svc_closeHandle(Handle handle);
	u64 svc_getSystemTick();
	Result svc_getSystemInfo(s64* out, u32 type, s32 param);
	Result svc_connectToPort(volatile Handle* out, const char* portName);
	Result svc_sendSyncRequest(Handle session);
	Result svc_getProcessId(u32 *out, Handle handle);
	Result svc_getThreadId(u32 *out, Handle handle);
	Result svc_setThreadIdealProcessor(Handle handle, u32 processorid);
	Result svc_restartDma(Handle h, void * dst, void const* src, unsigned int size, signed char flag);
	Result svc_kernelSetState(unsigned int Type, unsigned int Param0, unsigned int Param1, unsigned int Param2);

	Result svc_createAddressArbiter(Handle *arbiter);
	Result svc_arbitrateAddress(Handle arbiter, u32 addr, ArbitrationType type, s32 value, s64 timeout_ns);
	Result svc_arbitrateAddressNoTimeout(Handle arbiter, u32 addr, ArbitrationType type, s32 value);

	/**
	* @brief Maps a block of process memory.
	* @param process Handle of the process.
	* @param destAddress Address of the mapped block in the current process.
	* @param srcAddress Address of the mapped block in the source process.
	* @param size Size of the block of the memory to map (truncated to a multiple of 0x1000 bytes).
	*/
	Result svcMapProcessMemoryEx(Handle process, u32 destAddr, u32 srcAddr, u32 size);

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

	Result svcGetResourceLimit(Handle* resourceLimit, Handle process);
	Result svcGetResourceLimitLimitValues(s64* values, Handle resourceLimit, ResourceLimitType* names, s32 nameCount);
	Result svcGetResourceLimitCurrentValues(s64* values, Handle resourceLimit, ResourceLimitType* names, s32 nameCount);
	Result svcSetProcessResourceLimits(Handle process, Handle resourceLimit);
	Result svcCreateResourceLimit(Handle* resourceLimit);
	Result svcSetResourceLimitValues(Handle resourceLimit, const ResourceLimitType* names, const s64* values, s32 nameCount);
#endif
