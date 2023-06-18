#include "rp_color.h"
#include "rp_me.h"
#include "rp_syn.h"
#include "rp_syn_chan.h"
#include "rp_net.h"
#include "rp_dyn_prio.h"
#include "rp_ctx.h"
#include "rp_main.h"

NWMSendPacket_t nwmSendPacket;
static RT_HOOK nwmValParamHook;

static u8 rpInited;

static mp_pool_t *mp_pool;
static void *rp_kcp_malloc(size_t size) {
	if (size > sizeof(*((struct rp_ctx_t *)0)->kcp_ctx.seg_mem))
		return 0;
	return mp_malloc(mp_pool);
}
static void rp_kcp_free(void *p) {
	return mp_free(mp_pool, p);
}

static int nwmValParamCallback(u8* buf, int buflen UNUSED) {
	int ret;
	Handle hThread;

	if (rpInited) {
		return 0;
	}
	if (buf[0x17 + 0x8] == 6) {
		if ((*(u16*)(&buf[0x22 + 0x8])) == 0x401f) {  // src port 8000
			rpInited = 1;
			rtDisableHook(&nwmValParamHook);

			int storage_size = rtAlignToPageSize(sizeof(struct rp_ctx_t));
			struct rp_ctx_t *rp_ctx = (struct rp_ctx_t *)plgRequestMemory(storage_size);
			if (!rp_ctx) {
				nsDbgPrint("Request memory for RemotePlay failed\n");
				return 0;
			}
			memset(rp_ctx, 0, sizeof(*rp_ctx));
			nsDbgPrint("RemotePlay memory: 0x%08x (0x%x bytes)\n", rp_ctx, storage_size);

			memcpy(rp_ctx->nwm_send_buffer, buf, 0x22 + 8);

			if ((ret = mp_init(sizeof(*rp_ctx->kcp_ctx.seg_mem),
				sizeof(rp_ctx->kcp_ctx.seg_mem) / sizeof(*rp_ctx->kcp_ctx.seg_mem),
				rp_ctx->kcp_ctx.seg_mem,
				(mp_pool = &rp_ctx->kcp_ctx.seg_pool))
			)) {
				nsDbgPrint("umm_init_heap failed\n");
				return 0;
			}
			ikcp_allocator(rp_kcp_malloc, rp_kcp_free);

			rp_ctx->dma_config[2] = 4;

			ret = svc_createThread(&hThread, rpThreadStart, (u32)rp_ctx, (u32 *)&rp_ctx->thread_stack[RP_STACK_SIZE - 40], 0x10, 2);
			if (ret != 0) {
				nsDbgPrint("Create RemotePlay thread failed: %d\n", ret);
			}
		}
	}

	return 0;
}

void remotePlayMain(void) {
	nwmSendPacket = (NWMSendPacket_t)g_nsConfig->startupInfo[12];
	rtInitHookThumb(&nwmValParamHook, g_nsConfig->startupInfo[11], (u32)nwmValParamCallback);
	rtEnableHook(&nwmValParamHook);
}
