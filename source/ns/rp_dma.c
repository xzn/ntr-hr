#include "rp_dma.h"

void rpInitDmaHome(struct rp_dma_ctx_t *ctx, const u8 dma_config[24]) {
	(void)rp_lock_init(ctx->mutex);
	svc_openProcess(&ctx->home_handle, 0xf);
	ctx->dma_config = dma_config;
}

void rpCloseGameHandle(struct rp_dma_ctx_t *ctx) {
	if (ctx->game_handle) {
		svc_closeHandle(ctx->game_handle);
		ctx->game_handle = 0;
		ctx->game_fcram_base = 0;
	}
}

Handle rpGetGameHandle(struct rp_dma_ctx_t *ctx) {
	int i;
	Handle hProcess;
	if (ctx->game_handle == 0) {
		for (i = 0x28; i < 0x38; i++) {
			int ret = svc_openProcess(&hProcess, i);
			if (ret == 0) {
				nsDbgPrint("Game process opened for screen capture: %d\n", i);
				ctx->game_handle = hProcess;
				break;
			}
		}
		if (ctx->game_handle == 0) {
			return 0;
		}
	}
	if (ctx->game_fcram_base == 0) {
		if (svc_flushProcessDataCache(hProcess, 0x14000000, 0x1000) == 0) {
			ctx->game_fcram_base = 0x14000000;
		}
		else if (svc_flushProcessDataCache(hProcess, 0x30000000, 0x1000) == 0) {
			ctx->game_fcram_base = 0x30000000;
		}
		else {
			return 0;
		}
	}
	return ctx->game_handle;
}

int isInVRAM(u32 phys) {
	if (phys >= 0x18000000) {
		if (phys < 0x18000000 + 0x00600000) {
			return 1;
		}
	}
	return 0;
}

int isInFCRAM(u32 phys) {
	if (phys >= 0x20000000) {
		if (phys < 0x20000000 + 0x10000000) {
			return 1;
		}
	}
	return 0;
}
