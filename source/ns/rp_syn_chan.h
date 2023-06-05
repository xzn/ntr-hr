#ifndef RP_SYN_CHAN_H
#define RP_SYN_CHAN_H

#include "rp_common.h"

struct rp_screen_encode_t {
    u32 pitch;
    u32 fbaddr;
    u8 buffer[RP_SCREEN_BUFFER_SIZE] ALIGN_4;
    struct rp_image_t *image;
    struct rp_const_image_t *image_prev;
    struct rp_screen_ctx_t {
        u8 format;
        u8 top_bot;
        u8 p_frame;
        u8 first_frame;
        u8 frame_n;
    } c;
};

struct rp_network_encode_t {
    u8 buffer[RP_JLS_ENCODE_BUFFER_SIZE] ALIGN_4;
    u8 top_bot;
    u8 frame_n;
    u8 bpp;
    u8 format;
    u32 size;
    u32 size_1;
    u8 p_frame;
};

struct rp_syn_comp_t;
struct rp_syn_comp_func_t;
int rp_screen_queue_init(struct rp_syn_comp_t *screen, struct rp_screen_encode_t *base, int count);
int rp_network_queue_init(struct rp_syn_comp_t *network, struct rp_network_encode_t *base, int count);
struct rp_screen_encode_t *rp_screen_transfer_acquire(struct rp_syn_comp_func_t *syn1, s64 timeout);
void rp_screen_encode_release(struct rp_syn_comp_func_t *syn1, struct rp_screen_encode_t *pos);
struct rp_screen_encode_t *rp_screen_encode_acquire(struct rp_syn_comp_func_t *syn1, s64 timeout);
int rp_screen_transfer_release(struct rp_syn_comp_func_t *syn1, struct rp_screen_encode_t *pos);
struct rp_network_encode_t *rp_network_transfer_acquire(struct rp_syn_comp_func_t *syn1, s64 timeout);
void rp_network_encode_release(struct rp_syn_comp_func_t *syn1, struct rp_network_encode_t *pos);
struct rp_network_encode_t *rp_network_encode_acquire(struct rp_syn_comp_func_t *syn1, s64 timeout, int multicore);
int rp_network_transfer_release(struct rp_syn_comp_func_t *syn1, struct rp_network_encode_t *pos, int multicore);

#endif
