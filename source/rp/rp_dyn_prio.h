#ifndef RP_DYN_PROI_H
#define RP_DYN_PROI_H

#include "rp_syn.h"

#define RP_DYN_PRIO_FRAME_COUNT 2
struct rp_dyn_prio_t {
    struct rp_dyo_prio_screen_t {
        u16 frame_size_acc;
        u16 priority_size_acc;

        u8 frame_size_n;
        u8 frame_size[RP_DYN_PRIO_FRAME_COUNT];
        u8 priority_size[RP_DYN_PRIO_FRAME_COUNT];

        u8 frame_size_chn;
        u8 priority_size_chn;

        u8 frame_index;

        u32 tick[RP_DYN_PRIO_FRAME_COUNT];
        u8 frame_rate;
        u8 initializing;
        u8 priority;

        u16 frame_size_est;
        u16 priority_size_est;
    } s[SCREEN_COUNT];
    rp_lock_t mutex;
    u8 dyn;
    u8 frame_rate;
    u8 frame_size_count;
};

int rpInitPriorityCtx(struct rp_dyn_prio_t* dyn_prio, u8 screen_priority[SCREEN_COUNT], u8 dyn, u8 frame_rate, u8 frame_size_count);
int rpGetPriorityScreen(struct rp_dyn_prio_t* ctx, int *frame_rate);
void rpSetPriorityScreen(struct rp_dyn_prio_t* ctx, int top_bot, u32 size);

#endif
