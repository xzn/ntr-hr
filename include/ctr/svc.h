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

/// Configuration flags for \ref DmaConfig.
enum {
	DMACFG_SRC_IS_DEVICE = BIT(0), ///< DMA source is a device/peripheral. Address will not auto-increment.
	DMACFG_DST_IS_DEVICE = BIT(1), ///< DMA destination is a device/peripheral. Address will not auto-increment.
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

	u32* getThreadCommandBuffer(void);

	Result svc_backDoor(void* callback);
	Result svc_getThreadList(u32* threadCount, u32* threadIds, s32 threadIdMaxCount, Handle hProcess);
	Result svc_getDmaState(u32* state, Handle dma);
	Result svc_startInterProcessDma(Handle* hdma, Handle dstProcess, void* dst, Handle srcProcess, const void* src, u32 size, const DmaConfig* config);

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
	u64 svc_getSystemTick();
	Result svc_getSystemInfo(s64* out, u32 type, s32 param);
	Result svc_connectToPort(volatile Handle* out, const char* portName);
	Result svc_sendSyncRequest(Handle session);
	Result svc_getProcessId(u32 *out, Handle handle);
	Result svc_getThreadId(u32 *out, Handle handle);
	Result svc_setThreadIdealProcessor(Handle handle, u32 processorid);
	Result svc_restartDma(Handle h, void * dst, void const* src, unsigned int size, signed char flag);
	Result svc_kernelSetState(unsigned int Type, unsigned int Param0, unsigned int Param1, unsigned int Param2);

	/**
	* @brief Maps a block of process memory.
	* @param process Handle of the process.
	* @param destAddress Address of the mapped block in the current process.
	* @param srcAddress Address of the mapped block in the source process.
	* @param size Size of the block of the memory to map (truncated to a multiple of 0x1000 bytes).
	*/
	Result svcMapProcessMemoryEx(Handle process, u32 destAddr, u32 srcAddr, u32 size);
#endif
