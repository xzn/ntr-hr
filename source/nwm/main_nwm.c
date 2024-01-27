#include "global.h"

int main(void) {
	if (startupInit(1) != 0)
		return 0;

	// TODO
	if (ntrConfig->ex.nsUseDbg)
		nsStartup();
	disp(100, 0x1ff0000);

	return 0;
}
