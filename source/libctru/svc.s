.arm

.align 4

.global getThreadCommandBuffer
.type getThreadCommandBuffer, %function
getThreadCommandBuffer:
	mrc p15, 0, r0, c13, c0, 3
	add r0, #0x80
	bx lr


.global svc_controlMemory
.type svc_controlMemory, %function
svc_controlMemory:
	stmfd sp!, {r0, r4}
	ldr r0, [sp, #0x8]
	ldr r4, [sp, #0x8+0x4]
	svc 0x01
	ldr r2, [sp], #4
	str r1, [r2]
	ldr r4, [sp], #4
	bx lr

.global svc_exitProcess
.type svc_exitProcess, %function
svc_exitProcess:
	svc 0x03
	bx lr

.global svc_createThread
.type svc_createThread, %function
svc_createThread:
	stmfd sp!, {r0, r4}
	ldr r0, [sp, #0x8]
	ldr r4, [sp, #0x8+0x4]
	svc 0x08
	ldr r2, [sp], #4
	str r1, [r2]
	ldr r4, [sp], #4
	bx lr

.global svc_exitThread
.type svc_exitThread, %function
svc_exitThread:
	svc 0x09
	bx lr

.global svc_sleepThread
.type svc_sleepThread, %function
svc_sleepThread:
	svc 0x0A
	bx lr

.global svcCreatePort
.type svcCreatePort, %function
svcCreatePort:
	push {r0, r1}
	svc 0x47
	ldr r3, [sp, #0]
	str r1, [r3]
	ldr r3, [sp, #4]
	str r2, [r3]
	add sp, sp, #8
	bx  lr

.global svcAcceptSession
.type svcAcceptSession, %function
svcAcceptSession:
	str r0, [sp, #-4]!
	svc 0x4A
	ldr r2, [sp]
	str r1, [r2]
	add sp, sp, #4
	bx  lr

.global svcReplyAndReceive
.type svcReplyAndReceive, %function
svcReplyAndReceive:
	str r0, [sp, #-4]!
	svc 0x4F
	ldr r2, [sp]
	str r1, [r2]
	add sp, sp, #4
	bx  lr

.global svc_createMutex
.type svc_createMutex, %function
svc_createMutex:
	str r0, [sp, #-4]!
	svc 0x13
	ldr r3, [sp], #4
	str r1, [r3]
	bx lr

.global svc_releaseMutex
.type svc_releaseMutex, %function
svc_releaseMutex:
	svc 0x14
	bx lr

.global svc_createSemaphore
.type svc_createSemaphore, %function
svc_createSemaphore:
	push {r0}
	svc 0x15
	pop {r3}
	str r1, [r3]
	bx  lr

.global svc_releaseSemaphore
.type svc_releaseSemaphore, %function
svc_releaseSemaphore:
	str r0, [sp,#-4]!
	svc 0x16
	ldr r2, [sp], #4
	str r1, [r2]
	bx lr

.global svcCreateEvent
svcCreateEvent:
.global svc_createEvent
.type svc_createEvent, %function
svc_createEvent:
	str r0, [sp,#-4]!
	svc 0x17
	ldr r2, [sp], #4
	str r1, [r2]
	bx lr

.global svcSignalEvent
svcSignalEvent:
.global svc_signalEvent
.type svc_signalEvent, %function
svc_signalEvent:
	svc 0x18
	bx lr

.global svcClearEvent
svcClearEvent:
.global svc_clearEvent
.type svc_clearEvent, %function
svc_clearEvent:
	svc 0x19
	bx lr

.global svc_createMemoryBlock
.type svc_createMemoryBlock, %function
svc_createMemoryBlock:
	str r0, [sp, #-4]!
	ldr r0, [sp, #4]
	svc 0x1E
	ldr r2, [sp], #4
	str r1, [r2]
	bx lr

.global svc_mapMemoryBlock
.type svc_mapMemoryBlock, %function
svc_mapMemoryBlock:
	svc 0x1F
	bx lr

.global svc_unmapMemoryBlock
.type svc_unmapMemoryBlock, %function
svc_unmapMemoryBlock:
	svc 0x20
	bx lr

.global svcArbitrateAddress
svcArbitrateAddress:
.global svc_arbitrateAddress
.type svc_arbitrateAddress, %function
svc_arbitrateAddress:
		svc 0x22
		bx lr

.global svcCloseHandle
svcCloseHandle:
.global svc_closeHandle
.type svc_closeHandle, %function
svc_closeHandle:
	svc 0x23
	bx lr

.global svcWaitSynchronization
svcWaitSynchronization:
.global svc_waitSynchronization1
.type svc_waitSynchronization1, %function
svc_waitSynchronization1:
	svc 0x24
	bx lr

.global svc_waitSynchronizationN
.type svc_waitSynchronizationN, %function
svc_waitSynchronizationN:
	str r5, [sp, #-4]!
	str r4, [sp, #-4]!
	mov r5, r0
	ldr r0, [sp, #0x8]
	ldr r4, [sp, #0x8+0x4]
	svc 0x25
	str r1, [r5]
	ldr r4, [sp], #4
	ldr r5, [sp], #4
	bx  lr

.global svc_getSystemTick
.type svc_getSystemTick, %function
svc_getSystemTick:
	svc 0x28
	bx lr

.global svc_getSystemInfo
.type svc_getSystemInfo, %function
svc_getSystemInfo:
	stmfd sp!, {r0, r4}
	svc 0x2A
	ldr r4, [sp], #4
	str r1, [r4]
	str r2, [r4, #4]
	# str r3, [r4, #8] # ?
	ldr r4, [sp], #4
	bx lr

.global svc_getProcessInfo
.type svc_getProcessInfo, %function
svc_getProcessInfo:
	stmfd sp!, {r0, r4}
	svc 0x2B
	ldr r4, [sp], #4
	str r1, [r4]
	str r2, [r4, #4]
	ldr r4, [sp], #4
	bx lr

.global svc_connectToPort
.type svc_connectToPort, %function
svc_connectToPort:
	str r0, [sp,#-0x4]!
	svc 0x2D
	ldr r3, [sp], #4
	str r1, [r3]
	bx lr

.global svcSendSyncRequest
svcSendSyncRequest:
.global svc_sendSyncRequest
.type svc_sendSyncRequest, %function
svc_sendSyncRequest:
	svc 0x32
	bx lr

.global svc_getProcessId
.type svc_getProcessId, %function
svc_getProcessId:
	str r0, [sp,#-0x4]!
	svc 0x35
	ldr r3, [sp], #4
	str r1, [r3]
	bx lr

.global svc_getThreadId
.type svc_getThreadId, %function
svc_getThreadId:
	str r0, [sp,#-0x4]!
	svc 0x37
	ldr r3, [sp], #4
	str r1, [r3]
	bx lr


.global svc_setThreadIdealProcessor
.type svc_setThreadIdealProcessor, %function
svc_setThreadIdealProcessor:
	svc 0x10
	bx lr

.global svc_openThread
.type svc_openThread, %function
svc_openThread:
	str r0, [sp,#-0x4]!
	svc 0x34
	ldr r3, [sp], #4
	str r1, [r3]
	bx lr

.global svc_flushProcessDataCache
.type svc_flushProcessDataCache, %function
svc_flushProcessDataCache:
	svc 0x54
	bx lr

.global svc_invalidateProcessDataCache
.type svc_invalidateProcessDataCache, %function
svc_invalidateProcessDataCache:
	svc 0x52
	bx lr

.global svc_queryMemory
.type svc_queryMemory, %function
svc_queryMemory:
	svc 0x02
	bx lr

.global svc_addCodeSegment
.type svc_addCodeSegment, %function
svc_addCodeSegment:
	svc 0x7a
	bx lr

.global svc_terminateProcess
.type svc_terminateProcess, %function
svc_terminateProcess:
	svc 0x76
	bx lr

.global svc_openProcess
.type svc_openProcess, %function
svc_openProcess:
	str r0, [sp,#-0x4]!
	svc 0x33
	ldr r3, [sp], #4
	str r1, [r3]
	bx lr



.global svc_controlProcessMemory
.type svc_controlProcessMemory, %function
svc_controlProcessMemory:

	stmfd sp!, {r0, r4, r5}
	ldr r4, [sp, #0xC+0x0]
	ldr r5, [sp, #0xC+0x4]
	svc 0x70
	ldmfd sp!, {r2, r4, r5}
	bx lr

.global svc_restartDma
.type svc_restartDma, %function
svc_restartDma:

	stmfd sp!, {r0, r4, r5}
	ldr r4, [sp, #0xC+0x0]
	ldr r5, [sp, #0xC+0x4]
	svc 0x58
	ldmfd sp!, {r2, r4, r5}
	bx lr


.global svc_mapProcessMemory
.type svc_mapProcessMemory, %function
svc_mapProcessMemory:

	svc 0x71
	bx lr

.global svc_startInterProcessDma
.type svc_startInterProcessDma, %function
svc_startInterProcessDma:

	stmfd sp!, {r0, r4, r5}
	ldr r0, [sp, #0xC]
	ldr r4, [sp, #0xC+0x4]
	ldr r5, [sp, #0xC+0x8]
	svc 0x55
	ldmfd sp!, {r2, r4, r5}
	str	r1, [r2]
	bx lr

.global svc_getDmaState
.type svc_getDmaState, %function
svc_getDmaState:

	str r0, [sp,#-0x4]!
	svc 0x57
	ldr r3, [sp], #4
	strb r1, [r3]
	bx lr


.global svc_backDoor
.type svc_backDoor, %function
svc_backDoor:
	svc 0x7b
	bx lr


.global svc_getProcessList
.type svc_getProcessList, %function
svc_getProcessList:
	str r0, [sp,#-0x4]!
	svc 0x65
	ldr r3, [sp], #4
	str r1, [r3]
	bx lr


.global svc_getThreadList
.type svc_getThreadList, %function
svc_getThreadList:
	str r0, [sp,#-0x4]!
	svc 0x66
	ldr r3, [sp], #4
	str r1, [r3]
	bx lr

.global svc_getThreadContext
.type svc_getThreadContext, %function
svc_getThreadContext:
	svc 0x3b
	bx lr



.global svc_debugActiveProcess
.type svc_debugActiveProcess, %function
svc_debugActiveProcess:
	str r0, [sp,#-0x4]!
	svc 0x60
	ldr r3, [sp], #4
	str r1, [r3]
	bx lr

.global svc_readProcessMemory
.type svc_readProcessMemory, %function
svc_readProcessMemory:
	svc 0x6a
	bx lr

.global svc_writeProcessMemory
.type svc_writeProcessMemory, %function
svc_writeProcessMemory:
	svc 0x6b
	bx lr


.global svc_duplicateHandle
.type svc_duplicateHandle, %function
svc_duplicateHandle:
	str r0, [sp, #-0x4]!
	svc 0x27
	ldr r3, [sp], #4
	str r1, [r3]
	bx  lr


	.global svc_kernelSetState
.type svc_kernelSetState, %function
svc_kernelSetState:
	svc 0x7c
	bx lr


	.global svcSetThreadPriority
.type svcSetThreadPriority, %function
svcSetThreadPriority:
	svc 0x0C
	bx  lr


.macro BEGIN_ASM_FUNC name, linkage=global, section=text
	.section        .\section\().\name, "ax", %progbits
	.align          2
	.\linkage       \name
	.type           \name, %function
	.func           \name
	.cfi_sections   .debug_frame
	.cfi_startproc
	\name:
.endm

.macro END_ASM_FUNC
	.cfi_endproc
	.endfunc
.endm

.macro SVC_BEGIN name
	BEGIN_ASM_FUNC \name
.endm

.macro SVC_END
	END_ASM_FUNC
.endm


SVC_BEGIN svcCreateAddressArbiter
	push {r0}
	svc 0x21
	pop {r2}
	str r1, [r2]
	bx  lr
SVC_END

SVC_BEGIN svcArbitrateAddressNoTimeout
	svc 0x22
	bx  lr
SVC_END

SVC_BEGIN svcQueryMemory
	push {r0, r1, r4-r6}
	svc  0x02
	ldr  r6, [sp]
	str  r1, [r6]
	str  r2, [r6, #4]
	str  r3, [r6, #8]
	str  r4, [r6, #0xc]
	ldr  r6, [sp, #4]
	str  r5, [r6]
	add  sp, sp, #8
	pop  {r4-r6}
	bx   lr
SVC_END

SVC_BEGIN svcMapMemoryBlock
	svc 0x1F
	bx  lr
SVC_END

SVC_BEGIN svcUnmapMemoryBlock
	svc 0x20
	bx  lr
SVC_END

