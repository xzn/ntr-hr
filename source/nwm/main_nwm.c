#include "global.h"

#include "3ds/srv.h"

int main(void) {
	startupInit();

	s32 ret = srvInit();
	if (ret != 0) {
		showDbg("srvInit failed: %08"PRIx32, ret);
		return 0;
	}

	// TODO
	if (ntrConfig->ex.nsUseDbg) {
		nsStartup();
		disp(100, 0x17f7f7f);
	}
	disp(100, 0x1ff0000);

	return 0;
}
