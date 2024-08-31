#include "rp_res.h"

#include "3ds/svc.h"
#include "global.h"

void rp_svc_increase_limits(void) {
	Handle resLim;
	Result res;
	if ((res = svcGetResourceLimit(&resLim, CUR_PROCESS_HANDLE))) {
		nsDbgPrint("svcGetResourceLimit failed\n");
		return;
	}
	ResourceLimitType types[] = {RESLIMIT_MUTEX, RESLIMIT_SEMAPHORE};
	int count = sizeof(types) / sizeof(types[0]);
	s64 values[] = {64, 128};

	if ((res = svcSetResourceLimitValues(resLim, types, values, count))) {
		nsDbgPrint("svcSetResourceLimitValues failed\n");
	}

	svcCloseHandle(resLim);
}

void rp_svc_print_limits(void) {
	Handle resLim;
	Result res;
	if ((res = svcGetResourceLimit(&resLim, CUR_PROCESS_HANDLE))) {
		nsDbgPrint("svcGetResourceLimit failed\n");
		return;
	}
	ResourceLimitType types[] = {RESLIMIT_MUTEX, RESLIMIT_SEMAPHORE, RESLIMIT_EVENT, RESLIMIT_THREAD};
	int count = sizeof(types) / sizeof(types[0]);
	const char *names[] = {"mutex", "sem", "event", "thread"};
	s64 values[count];

	if ((res = svcGetResourceLimitCurrentValues(values, resLim, types, count))) {
		nsDbgPrint("svcGetResourceLimitCurrentValues failed\n");
		goto final;
	}

	for (int i = 0; i < count; ++i) {
		nsDbgPrint("%s res current %d\n", names[i], (int)values[i]);
	}

	if ((res = svcGetResourceLimitLimitValues(values, resLim, types, count))) {
		nsDbgPrint("svcGetResourceLimitLimitValues failed\n");
		goto final;
	}

	for (int i = 0; i < count; ++i) {
		nsDbgPrint("%s res limit %d\n", names[i], (int)values[i]);
	}

final:
	svcCloseHandle(resLim);
}
