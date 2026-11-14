#include "global.h"

static RT_HOOK csndHook;
static u32 csndHookFunc(u32 *param_1, u32 param_2)
{
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

	regs->r[0] = csndHookFunc((u32 *)regs->r[0], regs->r[1]);

	rtInitHookThumb(&csndHook, nsConfig->startupInfo[11], (u32)csndHookFunc);
	rtEnableHook(&csndHook);
}
