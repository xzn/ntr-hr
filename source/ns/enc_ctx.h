// buffer
#define I_B_(P_, N_, w, h) u8 dp ## P_ ## N_[(w) * (h)] I_A

struct I_C(ENC_FD_CTX_, I_N) {
	u8 flags I_A;

	I_B_(_, y, I_WIDTH, I_HEIGHT);
	I_B_(_ds2_, u, I_WIDTH / 2, I_HEIGHT / 2);
	I_B_(_ds2_, v, I_WIDTH / 2, I_HEIGHT / 2);

	I_B_(_ds_, y, I_WIDTH, I_HEIGHT / 2);
	I_B_(_ds2_, y, I_WIDTH / 2, I_HEIGHT / 2);
	I_B_(_ds_ds2_, u, I_WIDTH / 2, I_HEIGHT / 4);
	I_B_(_ds_ds2_, v, I_WIDTH / 2, I_HEIGHT / 4);
	I_B_(_ds2_ds2_, u, I_WIDTH / 4, I_HEIGHT / 4);
	I_B_(_ds2_ds2_, v, I_WIDTH / 4, I_HEIGHT / 4);
};

struct I_C(ENC_CTX_, I_N) {
	I_B_(_, u, I_WIDTH, I_HEIGHT);
	I_B_(_, v, I_WIDTH, I_HEIGHT);

	I_B_(_, p_y, I_WIDTH, I_HEIGHT);
	I_B_(_ds2_, p_u, I_WIDTH / 2, I_HEIGHT / 2);
	I_B_(_ds2_, p_v, I_WIDTH / 2, I_HEIGHT / 2);

	I_B_(_, fd_y, I_WIDTH, I_HEIGHT);
	I_B_(_ds2_, fd_u, I_WIDTH / 2, I_HEIGHT / 2);
	I_B_(_ds2_, fd_v, I_WIDTH / 2, I_HEIGHT / 2);

	I_B_(_, s_y, I_WIDTH, I_HEIGHT);
	I_B_(_ds2_, s_u, I_WIDTH / 2, I_HEIGHT / 2);
	I_B_(_ds2_, s_v, I_WIDTH / 2, I_HEIGHT / 2);

	u8 dp_m_y[ENCODE_SELECT_MASK_SIZE(I_WIDTH, I_HEIGHT)] I_A;
	u8 dp_ds2_m_u[ENCODE_SELECT_MASK_SIZE(I_WIDTH / 2, I_HEIGHT / 2)] I_A;
	u8 dp_ds2_m_v[ENCODE_SELECT_MASK_SIZE(I_WIDTH / 2, I_HEIGHT / 2)] I_A;
};

struct I_C(ENC_ds_CTX_, I_N) {
	I_B_(_ds_, p_y, I_WIDTH, I_HEIGHT / 2);
	I_B_(_ds_ds2_, p_u, I_WIDTH / 2, I_HEIGHT / 4);
	I_B_(_ds_ds2_, p_v, I_WIDTH / 2, I_HEIGHT / 4);

	I_B_(_ds_, fd_y, I_WIDTH, I_HEIGHT / 2);
	I_B_(_ds_ds2_, fd_u, I_WIDTH / 2, I_HEIGHT / 4);
	I_B_(_ds_ds2_, fd_v, I_WIDTH / 2, I_HEIGHT / 4);

	I_B_(_ds_, s_y, I_WIDTH, I_HEIGHT / 2);
	I_B_(_ds_ds2_, s_u, I_WIDTH / 2, I_HEIGHT / 4);
	I_B_(_ds_ds2_, s_v, I_WIDTH / 2, I_HEIGHT / 4);

	u8 dp_ds_m_y[ENCODE_SELECT_MASK_SIZE(I_WIDTH, I_HEIGHT / 2)] I_A;
	u8 dp_ds_ds2_m_u[ENCODE_SELECT_MASK_SIZE(I_WIDTH / 2, I_HEIGHT / 4)] I_A;
	u8 dp_ds_ds2_m_v[ENCODE_SELECT_MASK_SIZE(I_WIDTH / 2, I_HEIGHT / 4)] I_A;
};

struct I_C(ENC_ds2_CTX_, I_N) {
	I_B_(_ds2_, p_y, I_WIDTH / 2, I_HEIGHT / 2);
	I_B_(_ds2_ds2_, p_u, I_WIDTH / 4, I_HEIGHT / 4);
	I_B_(_ds2_ds2_, p_v, I_WIDTH / 4, I_HEIGHT / 4);

	I_B_(_ds2_, fd_y, I_WIDTH / 2, I_HEIGHT / 2);
	I_B_(_ds2_ds2_, fd_u, I_WIDTH / 4, I_HEIGHT / 4);
	I_B_(_ds2_ds2_, fd_v, I_WIDTH / 4, I_HEIGHT / 4);

	I_B_(_ds2_, s_y, I_WIDTH / 2, I_HEIGHT / 2);
	I_B_(_ds2_ds2_, s_u, I_WIDTH / 4, I_HEIGHT / 4);
	I_B_(_ds2_ds2_, s_v, I_WIDTH / 4, I_HEIGHT / 4);

	u8 dp_ds2_m_y[ENCODE_SELECT_MASK_SIZE(I_WIDTH / 2, I_HEIGHT / 2)] I_A;
	u8 dp_ds2_ds2_m_u[ENCODE_SELECT_MASK_SIZE(I_WIDTH / 4, I_HEIGHT / 4)] I_A;
	u8 dp_ds2_ds2_m_v[ENCODE_SELECT_MASK_SIZE(I_WIDTH / 4, I_HEIGHT / 4)] I_A;
};

struct I_C(ENC_c_CTX_, I_N) {
	u8 dp_ds_c_y[ENCODE_UPSAMPLE_CARRY_SIZE(I_WIDTH, I_HEIGHT / 2)] I_A;
	u8 dp_ds_ds2_c_u[ENCODE_UPSAMPLE_CARRY_SIZE(I_WIDTH / 2, I_HEIGHT / 4)] I_A;
	u8 dp_ds_ds2_c_v[ENCODE_UPSAMPLE_CARRY_SIZE(I_WIDTH / 2, I_HEIGHT / 4)] I_A;

	u8 dp_ds2_c_y[ENCODE_UPSAMPLE_CARRY_SIZE(I_WIDTH / 2, I_HEIGHT / 2)] I_A;
	u8 dp_ds2_ds2_c_u[ENCODE_UPSAMPLE_CARRY_SIZE(I_WIDTH / 4, I_HEIGHT / 4)] I_A;
	u8 dp_ds2_ds2_c_v[ENCODE_UPSAMPLE_CARRY_SIZE(I_WIDTH / 4, I_HEIGHT / 4)] I_A;
};

#undef I_B_

struct I_C(ENC_, I_N) {
	struct I_C(ENC_CTX_, I_N) im;
	struct I_C(ENC_ds_CTX_, I_N) ds_im, rs_im;
	struct I_C(ENC_ds2_CTX_, I_N) ds2_im, rs2_im;
	struct I_C(ENC_c_CTX_, I_N) c_im;
};
