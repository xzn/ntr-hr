#include "global.h"

#include "3ds/ipc.h"
#include "3ds/services/csnd.h"

#include <memory.h>

static bool csnd_capture_enabled;

#define CSND_CAP_REG ((void *)0x1EC03800)

#define CSND_CAP_COUNT (CSND_CAP_BUF_CHNS)

struct csnd_cap_regs_t {
	u32 ctl;
	u32 rate;
	u32 len;
	u32 addr;
};

static struct csnd_cap_regs_t cap_reg_saved[CSND_CAP_COUNT];

#define CSND_CAP_ADDR_COUNT_MAX (8)

static int csnd_cap_addr_count;
static u32 *csnd_cap_addrs[CSND_CAP_ADDR_COUNT_MAX];

static void populate_cap_addrs(void)
{
	if (ensureCsndCapPa())
		return;

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
	memcpy(cap_reg_saved, CSND_CAP_REG, sizeof(cap_reg_saved));

	for (int i = 0; i < csnd_cap_addr_count; ++i)
		*csnd_cap_addrs[i] = (u32)cap_reg_saved;

	memset((void *)(csndCapPa | 0x80000000), 0, CSND_CAP_BUF_SIZE);
	struct csnd_cap_regs_t *regs = CSND_CAP_REG;
	for (int i = 0; i < CSND_CAP_COUNT; ++i) {
		struct csnd_cap_regs_t *r = &regs[i];
		r->ctl = 0;
		r->rate = CSND_CAP_TIMER;
		r->len = 0;
		r->addr = csndCapPa + (CSND_CAP_BUF_SIZE / CSND_CAP_COUNT) * i;
		r->len = CSND_CAP_BUF_SIZE / CSND_CAP_COUNT;
		r->ctl = 1 << 15;
	}

	nsDbgPrint("Csnd capture enabled\n");
}

static void csnd_capture_disable(void)
{
	for (int i = 0; i < csnd_cap_addr_count; ++i)
		*csnd_cap_addrs[i] = (u32)CSND_CAP_REG;

	memcpy(CSND_CAP_REG, cap_reg_saved, sizeof(cap_reg_saved));

	memset(cap_reg_saved, 0, sizeof(cap_reg_saved));

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
