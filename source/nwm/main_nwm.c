#include "global.h"

int main(void) {
	startupInit();

	// TODO
	if (ntrConfig->ex.nsUseDbg) {
		nsStartup();
		disp(100, 0x17f7f7f);
	}
	disp(100, 0x1ff0000);

	return 0;
}
