#include "global.h"

int main(void) {
	if (startupInit(0) != 0)
		return 0;

	// TODO
	if (ntrConfig->ex.nsUseDbg)
		nsStartup();

	return 0;
}
