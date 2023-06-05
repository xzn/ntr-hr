#ifndef RP_DMA_H
#define RP_DMA_H

#include "rp_common.h"

struct rp_dma_ctx_t {
	Handle home_handle, game_handle;
	u32 game_fcram_base;
	const u8 *dma_config;
};

void rpInitDmaHome(struct rp_dma_ctx_t *ctx, const u8 dma_config[24]);
void rpCloseGameHandle(struct rp_dma_ctx_t *ctx);
Handle rpGetGameHandle(struct rp_dma_ctx_t *ctx);
int isInVRAM(u32 phys);
int isInFCRAM(u32 phys);

#endif
