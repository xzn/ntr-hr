#include "rp_conf.h"

int rp_set_params(struct rp_conf_t *conf) {
	u8 multicore_encode = conf->multicore_encode;

	conf->arg0 = g_nsConfig->startupInfo[8];
	conf->arg1 = g_nsConfig->startupInfo[9];
	conf->arg2 = g_nsConfig->startupInfo[10];

	conf->updated = 0;

	conf->kcp_conv = conf->arg0;

	conf->yuv_option = (conf->arg1 & 0x3);
	conf->color_transform_hp = (conf->arg1 & 0xc) >> 2;
	conf->downscale_uv = (conf->arg1 & 0x10) >> 4;
	conf->encoder_which = (conf->arg1 & 0x20) >> 5;

	conf->me.method = (conf->arg1 & 0x1c0) >> 6;
	conf->me.block_size = RP_ME_MIN_BLOCK_SIZE << ((conf->arg1 & 0x600) >> 9);
	conf->me.block_size_log2 = av_ceil_log2(conf->me.block_size);
	conf->me.search_param = ((conf->arg1 & 0xf800) >> 11) + RP_ME_MIN_SEARCH_PARAM;
	conf->me.bpp = av_ceil_log2(conf->me.search_param * 2 + 1);
	conf->me.bpp_half_range = (1 << conf->me.bpp) >> 1;
	conf->me.downscale = ((conf->arg1 & 0x10000) >> 16);
#if RP_ME_INTERPOLATE
	conf->me.interpolate = ((conf->arg1 & 0x20000) >> 17);
#else
	conf->me.interpolate = 0;
#endif
	conf->encode_verify = ((conf->arg1 & 0x40000) >> 18);

	conf->screen_priority[SCREEN_TOP] = (conf->arg2 & 0xf);
	conf->screen_priority[SCREEN_BOT] = (conf->arg2 & 0xf0) >> 4;
	conf->low_latency = (conf->arg2 & 0x2000) >> 13;
	if (RP_ENCODE_MULTITHREAD)
		conf->multicore_encode = (conf->arg2 & 0x4000) >> 14;
	conf->dynamic_priority = (conf->arg2 & 0x8000) >> 15;
	conf->target_frame_rate = (conf->arg2 & 0xff0000) >> 16;
	conf->target_mbit_rate = (conf->arg2 & 0x1f00) >> 8;

	conf->encode_buffer_count = RP_ENCODE_BUFFER_COUNT - conf->low_latency -
		(RP_ENCODE_MULTITHREAD && !conf->multicore_encode);

	conf->min_send_interval_ticks =
		(u64)SYSTICK_PER_SEC * NWM_PACKET_SIZE * 8 /
		((u16)conf->target_mbit_rate + 1) / 1024 / 1024;

	conf->max_capture_interval_ticks =
		(u64)SYSTICK_PER_SEC / ((u16)conf->target_frame_rate + 1);

	int ret = 0;
	if (conf->multicore_encode != multicore_encode)
		ret = 1;

	return ret;
}

int rp_check_params(struct rp_conf_t *conf, u8 *exit_thread) {
	if (__atomic_load_n(&g_nsConfig->remotePlayUpdate, __ATOMIC_ACQUIRE)) {
		__atomic_store_n(&g_nsConfig->remotePlayUpdate, 0, __ATOMIC_RELEASE);
		conf->updated = 1;
		*exit_thread = 1;
	}

	return 0;
}
