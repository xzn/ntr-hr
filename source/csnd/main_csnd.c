#include "global.h"

#include "3ds/ipc.h"
#include "3ds/services/csnd.h"

#include <memory.h>

static bool csnd_capture_enabled;

#define CSND_CAP_REG ((void *)0x1EC03800)

struct csnd_cap_regs_t {
	volatile u32 ctl;
	volatile u32 rate;
	volatile u32 len;
	volatile u32 addr;
};

static struct csnd_cap_regs_t cap_reg_saved[CSND_CAP_BUF_CHNS];

#define CSND_CAP_ADDR_COUNT_MAX (8)
#define CSND_CNT_REG (*(volatile u32 *)(0x10103000 | 0x80000000))

static int csnd_cap_addr_count;
static u32 *csnd_cap_addrs[CSND_CAP_ADDR_COUNT_MAX];

static u32 (*csnd_cap_sync_enter)(u32 *);
static u32 (*csnd_cap_sync_exit)(u32 *);
static u32 *csnd_cap_sync;

static void populate_csnd_cap_sync(void) {
	csnd_cap_sync_enter = (void *)0x0010378e;
	csnd_cap_sync_exit = (void *)0x0010377c;
	csnd_cap_sync = (void *)0x001097dc;

#define RP_CSND_HDR_SIZE (18)
	u8 csnd_cap_sync_enter_hdr[RP_CSND_HDR_SIZE] = {
		0x10, 0xb5, 0x04, 0x46, 0xfe, 0xf7, 0xb4, 0xfe,
		0x00, 0x28, 0x05, 0xd1, 0x20, 0x46, 0xff, 0xf7,
		0xc1, 0xfe,
	};
	if (memcmp(csnd_cap_sync_enter, csnd_cap_sync_enter_hdr, RP_CSND_HDR_SIZE)) {
		nsDbgPrint("csnd_cap_sync_enter hdr unexpected\n");
		goto error;
	}

	u8 csnd_cap_sync_exit_hdr[RP_CSND_HDR_SIZE] = {
		0x10, 0xb5, 0x81, 0x68, 0x49, 0x1e, 0x81, 0x60,
		0x02, 0xd1, 0x41, 0x60, 0xff, 0xf7, 0xbc, 0xfe,
		0x10, 0xbd,
	};
	if (memcmp(csnd_cap_sync_exit, csnd_cap_sync_exit_hdr, RP_CSND_HDR_SIZE)) {
		nsDbgPrint("csnd_cap_sync_exit hdr unexpected\n");
		goto error;
	}

error:
	csnd_cap_sync_enter = 0;
	csnd_cap_sync_exit = 0;
	csnd_cap_sync = 0;
}

static void populate_cap_addrs(void)
{
	if (ensureCsndCapPa())
		return;

	populate_csnd_cap_sync();

	u32 codeStartAddress;
	u32 codeEndAddress;
	u32 codeTotalSize;

	s64 textStartAddress, textTotalRoundedSize;
	Handle processHandle = CUR_PROCESS_HANDLE;

	svcGetProcessInfo(&textTotalRoundedSize, processHandle, 0x10002);

	svcGetProcessInfo(&textStartAddress, processHandle, 0x10005);

	codeTotalSize = (u32)textTotalRoundedSize;
	codeStartAddress = (u32)textStartAddress;
	codeEndAddress = codeStartAddress + codeTotalSize;

	while (codeStartAddress < codeEndAddress) {
		if (*(u32 *)codeStartAddress == (u32)CSND_CAP_REG) {
			if (csnd_cap_addr_count < CSND_CAP_ADDR_COUNT_MAX) {
				int ret = rtCheckMemory(codeStartAddress, sizeof(u32), MEMPERM_READWRITE | MEMPERM_EXECUTE);

				if (ret) {
					nsDbgPrint("Check memory failed for cap addr at %"PRIx32"\n", codeStartAddress);
					continue;
				}

				csnd_cap_addrs[csnd_cap_addr_count] = (u32 *)codeStartAddress;
				++csnd_cap_addr_count;
				nsDbgPrint("Cap addr at %"PRIx32"\n", codeStartAddress);
			} else {
				nsDbgPrint("Cap addr count exceeded at %"PRIx32"\n", codeStartAddress);
			}
		}
		codeStartAddress += sizeof(u32);
	}

	nsDbgPrint("Cap addr count: %d\n", csnd_cap_addr_count);
}

static void csnd_capture_enable(void)
{
	if (csnd_cap_sync_enter)
		csnd_cap_sync_enter(csnd_cap_sync);

	u32 cnt_reg = CSND_CNT_REG;
	CSND_CNT_REG |= (1 << 31);

	memcpy(cap_reg_saved, CSND_CAP_REG, sizeof(cap_reg_saved));

	for (int i = 0; i < csnd_cap_addr_count; ++i)
		*csnd_cap_addrs[i] = (u32)cap_reg_saved;

	memset((void *)(csndCapPa | 0x80000000), 0, CSND_CAP_BUF_SIZE);
	struct csnd_cap_regs_t *regs = CSND_CAP_REG;
	for (int i = 0; i < CSND_CAP_BUF_CHNS; ++i) {
		struct csnd_cap_regs_t *r = &regs[i];
		r->ctl = 0;
		r->rate = (u16)-CSND_CAP_TIMER;
		r->len = 0;
		r->addr = csndCapPa + (CSND_CAP_BUF_SIZE / CSND_CAP_BUF_CHNS) * i;
		r->len = CSND_CAP_BUF_SIZE / CSND_CAP_BUF_CHNS;
		r->ctl = 1 << 15;
	}

	CSND_CNT_REG = cnt_reg;

	if (csnd_cap_sync_exit)
		csnd_cap_sync_exit(csnd_cap_sync);

	nsDbgPrint("Csnd capture enabled\n");
}

static void csnd_capture_disable(void)
{
	if (csnd_cap_sync_enter)
		csnd_cap_sync_enter(csnd_cap_sync);

	u32 cnt_reg = CSND_CNT_REG;
	CSND_CNT_REG |= (1 << 31);

	for (int i = 0; i < csnd_cap_addr_count; ++i)
		*csnd_cap_addrs[i] = (u32)CSND_CAP_REG;

	memcpy(CSND_CAP_REG, cap_reg_saved, sizeof(cap_reg_saved));

	memset(cap_reg_saved, 0, sizeof(cap_reg_saved));

	CSND_CNT_REG = cnt_reg;

	if (csnd_cap_sync_exit)
		csnd_cap_sync_exit(csnd_cap_sync);

	nsDbgPrint("Csnd capture disabled\n");
}

static void rpSetCsndCap(bool enabled) {
	Handle hClient = rpGetPortHandle();
	if (!hClient)
		return;

	u32* cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = IPC_MakeHeader(SVC_NWM_CMD_SET_CSND_CAP, 1, 0);
	cmdbuf[1] = enabled;

	s32 ret = svcSendSyncRequest(hClient);
	if (ret != 0) {
		nsDbgPrint("Send port request failed: %08"PRIx32"\n", ret);
	}
}

static void check_csnd_capture(void)
{
	u32 enabled = rpConfig->audioEnable;
	bool enabled_changed;
	u32 enabled_new;
	do {
		enabled_changed = enabled & (1 << 31);
		if (!enabled_changed)
			return;
		enabled_new = enabled & ~(1 << 31);
	} while (!__atomic_compare_exchange_n(&rpConfig->audioEnable, &enabled, enabled_new, 1, __ATOMIC_RELEASE, __ATOMIC_RELAXED));

	if (enabled_changed) {
		if (csnd_capture_enabled) {
			csnd_capture_disable();
			csnd_capture_enabled = false;

			rpSetCsndCap(csnd_capture_enabled);
		}

		if (enabled_new) {
			csnd_capture_enable();
			csnd_capture_enabled = true;

			rpSetCsndCap(csnd_capture_enabled);
		}
	}
}

static RT_HOOK csndHook;
static u32 csndHookFunc(u32 *param_1, u32 param_2)
{
	check_csnd_capture();

	if (param_2 == 0x0000FFFF)
		return 0;
	return param_1[2] + param_2;
}

int setUpReturn(void) { return 0; }

struct boot_hook_regs {
	u32 cpsr;
	u32 _pad;
	u32 r[13]; // r0 - r12
	u32 lr;
};

void c_entry(struct boot_hook_regs *regs) {
	startupInit();
	populate_cap_addrs();

	regs->r[0] = csndHookFunc((u32 *)regs->r[0], regs->r[1]);

	rtInitHookThumb(&csndHook, nsConfig->startupInfo[11], (u32)csndHookFunc);
	rtEnableHook(&csndHook);
}
