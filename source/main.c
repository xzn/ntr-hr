#include "global.h"
#include "3ds/services/fs.h"

extern int _BootArgs[];

NTR_CONFIG *ntrConfig;
showDbgFunc_t showDbgFunc;

static Handle fsUserHandle;
static char *binDir;

int main(void) {
	ntrConfig = (void *)_BootArgs[0];
	showDbgFunc = (void *)ntrConfig->showDbgFunc;
	fsUserHandle = ntrConfig->fsUserHandle;
	binDir = (void *)_BootArgs[1];

	fsUseSession(fsUserHandle);

	return 0;
}
