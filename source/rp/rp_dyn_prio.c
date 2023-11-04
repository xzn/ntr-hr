#include "rp_dyn_prio.h"

int rpInitPriorityCtx(struct rp_dyn_prio_t* dyn_prio, u8 screen_priority[SCREEN_COUNT], u8 dyn, u8 frame_rate, u8 frame_size_count) {
	rp_lock_close(dyn_prio->mutex);
	memset(dyn_prio, 0, sizeof(struct rp_dyn_prio_t));
	int res;
	if ((res = rp_lock_init(dyn_prio->mutex)))
		return res;

	for (int i = 0; i < SCREEN_COUNT; ++i) {
		dyn_prio->s[i].initializing = RP_DYN_PRIO_FRAME_COUNT;
		dyn_prio->s[i].priority = screen_priority[i];
	}

	dyn_prio->dyn = dyn;
	dyn_prio->frame_rate = frame_rate;
	dyn_prio->frame_size_count = frame_size_count;
	return 0;
}

int rpGetPriorityScreen(struct rp_dyn_prio_t* ctx, int *frame_rate) {
	for (int i = 0; i < SCREEN_COUNT; ++i)
		if (ctx->s[i].priority == 0)
			return i;

	int top_bot;
	if (rp_lock_wait(ctx->mutex, RP_SYN_WAIT_MAX) != 0)
		return 0;

#define SET_WITH_SIZE(si, c0, c1, te, ta) do { \
	top_bot = si; \
	ctx->s[c1].te -=ctx->s[c0].te; \
	ctx->s[c0].te = ctx->s[c0].ta; \
} while (0)
#define SET_WITH_FRAME_SIZE(si, c0, c1) SET_WITH_SIZE(si, c1, c0, frame_size_est, frame_size_acc)
#define SET_WITH_PRIORITY_SIZE(si, c0, c1) SET_WITH_SIZE(si, c0, c1, priority_size_est, priority_size_acc)

#define SET_WITH_SIZE_0(si, c0, c1, te, ta) do { \
	top_bot = si; \
	ctx->s[c1].te = 0; \
	ctx->s[c0].te = ctx->s[c0].ta; \
} while (0)
#define SET_WITH_FRAME_SIZE_0(si, c0, c1) SET_WITH_SIZE_0(si, c1, c0, frame_size_est, frame_size_acc)

#define SET_SIZE(_, c0, c1, te, ta) do { \
	if (ctx->s[c0].te <= ctx->s[c1].te) { \
		ctx->s[c1].te -=ctx->s[c0].te; \
	} else { \
		ctx->s[c1].te = 0; \
	} \
	ctx->s[c0].te = ctx->s[c0].ta; \
} while (0)
#define SET_FRAME_SIZE(si, c0, c1) SET_SIZE(si, c1, c0, frame_size_est, frame_size_acc)
#define SET_PRIORITY_SIZE(si, c0, c1) SET_SIZE(si, c0, c1, priority_size_est, priority_size_acc)

#define SET_CASE_FRAME_SIZE(s0, s1) do { \
	int skip_prio = ctx->s[s0].priority < ctx->s[s1].priority; \
	if (skip_prio || ctx->s[s0].priority_size_est <= ctx->s[s1].priority) { \
		/* nsDbgPrint("frame_size_est %d %d\n", s0, ctx->s[s0].frame_size_acc); */ \
		SET_WITH_FRAME_SIZE(s0, s0, s1); \
		SET_PRIORITY_SIZE(s0, s0, s1); \
	} else { \
		SET_WITH_FRAME_SIZE_0(s1, s1, s0); \
		SET_PRIORITY_SIZE(s1, s1, s0); \
	} \
} while (0)

#define SET_CASE_PRIORITY_SIZE(s0, s1) do { \
	SET_WITH_PRIORITY_SIZE(s0, s0, s1); \
	if (ctx->dyn) \
		SET_FRAME_SIZE(s0, s0, s1); \
} while (0)

	if (ctx->dyn &&
		ctx->s[SCREEN_TOP].frame_rate + ctx->s[SCREEN_BOT].frame_rate >= ctx->frame_rate &&
		(
			ctx->s[SCREEN_TOP].frame_rate != 0 && ctx->s[SCREEN_BOT].frame_rate != 0 &&
			ctx->s[SCREEN_TOP].frame_size_est != 0 && ctx->s[SCREEN_BOT].frame_size_est != 0
		)
	) {
		if (ctx->s[SCREEN_TOP].frame_size_est >= ctx->s[SCREEN_BOT].frame_size_est) {
			SET_CASE_FRAME_SIZE(SCREEN_TOP, SCREEN_BOT);
		} else {
			SET_CASE_FRAME_SIZE(SCREEN_BOT, SCREEN_TOP);
		}
	} else {
		if (ctx->s[SCREEN_TOP].priority_size_est <= ctx->s[SCREEN_BOT].priority_size_est) {
			SET_CASE_PRIORITY_SIZE(SCREEN_TOP, SCREEN_BOT);
		} else {
			SET_CASE_PRIORITY_SIZE(SCREEN_BOT, SCREEN_TOP);
		}
	}

#undef SET_CASE_PRIORITY_SIZE
#undef SET_CASE_FRAME_SIZE

#undef SET_WITH_FRAME_SIZE
#undef SET_WITH_PRIORITY_SIZE
#undef SET_WITH_SIZE

#undef SET_WITH_FRAME_SIZE_0
#undef SET_WITH_SIZE_0

#undef SET_FRAME_SIZE
#undef SET_PRIORITY_SIZE
#undef SET_SIZE

	if (frame_rate)
		*frame_rate = ctx->s[SCREEN_TOP].frame_rate + ctx->s[SCREEN_BOT].frame_rate;
	rp_lock_rel(ctx->mutex);
	return top_bot;
}

void rpSetPriorityScreen(struct rp_dyn_prio_t* ctx, int top_bot, u32 size) {
	for (int i = 0; i < SCREEN_COUNT; ++i)
		if (ctx->s[i].priority == 0)
			return;

	struct rp_dyo_prio_screen_t *sctx;
	sctx = &ctx->s[top_bot];

	if (rp_lock_wait(ctx->mutex, RP_SYN_WAIT_MAX))
		return;

#define SET_SIZE(t, ta, tc) do { \
	sctx->ta += sctx->tc; \
	sctx->ta -= sctx->t[sctx->frame_index]; \
	sctx->t[sctx->frame_index] = sctx->tc; \
	sctx->tc = 0; \
} while (0)

	int frame_next = 0;
	if (++sctx->frame_size_n == ctx->frame_size_count) {
		frame_next = 1;
		sctx->frame_size_n = 0;
	}

	if (ctx->dyn) {
		sctx->frame_size_chn += av_ceil_log2(size);
		// sctx->frame_size_chn += size;

		if (frame_next) {
			SET_SIZE(frame_size, frame_size_acc, frame_size_chn);
			// nsDbgPrint("frame_size_acc %d %d\n", top_bot, sctx->frame_size_acc);

			u32 tick = svc_getSystemTick();
			u32 tick_delta = tick - sctx->tick[sctx->frame_index];
			sctx->tick[sctx->frame_index] = tick;

			if (sctx->initializing) {
				--sctx->initializing;
				sctx->frame_rate = 0;
			} else {
				sctx->frame_rate = (u64)SYSTICK_PER_SEC * RP_DYN_PRIO_FRAME_COUNT / tick_delta;
			}
		}
	}

	if (frame_next) {
		sctx->priority_size_chn += sctx->priority;
		SET_SIZE(priority_size, priority_size_acc, priority_size_chn);

		++sctx->frame_index;
		sctx->frame_index %= RP_DYN_PRIO_FRAME_COUNT;
	}

#undef SET_SIZE

	rp_lock_rel(ctx->mutex);
}
#undef RP_DYN_PRIO_FRAME_COUNT
