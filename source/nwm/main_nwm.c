#include "global.h"

#include "3ds/srv.h"

void mainThread(void *) {
	s32 ret = srvInit();
	if (ret != 0) {
		showDbg("srvInit failed: %08"PRIx32, ret);
		goto final;
	}

	// TODO
	if (ntrConfig->ex.nsUseDbg) {
		ret = nsStartup();
		if (ret != 0) {
			disp(100, 0x1ff00ff);
		} else {
			disp(100, 0x17f7f7f);
		}
	}
	disp(100, 0x1ff0000);

final:
	svcExitThread();
}
