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
			disp(100, DBG_CL_USE_DBG_FAIL);
		} else {
			disp(100, DBG_CL_USE_DBG);
		}
	}
	disp(100, DBG_CL_INFO);

final:
	svcExitThread();
}
