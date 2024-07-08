#include "global.h"

#include "3ds/ipc.h"

#include <string.h>

static Handle menuSessionClient;
Handle menuGetPortHandle(void) {
	Handle hClient = menuSessionClient;
	s32 ret;
	if (hClient == 0) {
		ret = svcConnectToPort(&hClient, SVC_PORT_MENU);
		if (ret != 0) {
			return 0;
		}
		menuSessionClient = hClient;
	}
	return hClient;
}

int showMsgVerbose(const char *file_name, int line_number, const char *func_name, const char *fmt, ...) {
	va_list arp;
	va_start(arp, fmt);
	s32 ret = showMsgVA(file_name, line_number, func_name, fmt, arp);
	va_end(arp);
	return ret;
}

int showMsgRaw(const char *fmt, ...) {
	va_list arp;
	va_start(arp, fmt);
	s32 ret = showMsgVA(NULL, 0, NULL, fmt, arp);
	va_end(arp);
	return ret;
}

void __attribute__((weak)) showMsgRaw2(const char *title, const char *msg) {
	u32* cmdbuf = getThreadCommandBuffer();
	cmdbuf[0] = IPC_MakeHeader(SVC_MENU_CMD_SHOW_MSG, 0, 4);
	cmdbuf[1] = IPC_Desc_StaticBuffer(strlen(title) + 1, 0);
	cmdbuf[2] = (u32)title;
	cmdbuf[3] = IPC_Desc_StaticBuffer(strlen(msg) + 1, 1);
	cmdbuf[4] = (u32)msg;

	s32 ret = svcSendSyncRequest(menuGetPortHandle());
	if (ret != 0) {
		disp(100, DBG_CL_MSG);
		svcSleepThread(1000000000);
		return;
	}
	return;
}

int __attribute__((weak)) showMsgVAPre() {
	Handle hClient = menuGetPortHandle();
	if (!hClient) {
		disp(100, DBG_CL_MSG);
		svcSleepThread(1000000000);
		return -1;
	}
	return 0;
}

void printTitleAndMsg(char title[LOCAL_TITLE_BUF_SIZE], const char *file_name, int line_number, const char *func_name, char msg[LOCAL_MSG_BUF_SIZE], const char* fmt, va_list va) {
	if (file_name && func_name) {
		u64 ticks = svcGetSystemTick();
		u64 mono_us = ticks * 1000 / (SYSCLOCK_ARM11 / 1000000);
		u32 pid = getCurrentProcessId();
		xsnprintf(title, LOCAL_TITLE_BUF_SIZE, DBG_VERBOSE_TITLE, (u32)(mono_us / 1000000), (u32)(mono_us % 1000000), pid, file_name, line_number, func_name);
	} else {
		*title = 0;
	}
	xvsnprintf(msg, LOCAL_MSG_BUF_SIZE, fmt, va);
}

int __attribute__((weak)) showMsgVA(const char *file_name, int line_number, const char *func_name, const char* fmt, va_list va) {
	s32 ret = showMsgVAPre();
	if (ret != 0)
		return 0;

	char title[LOCAL_TITLE_BUF_SIZE];
	char msg[LOCAL_MSG_BUF_SIZE];
	printTitleAndMsg(title, file_name, line_number, func_name, msg, fmt, va);

	showMsgRaw2(title, msg);

	return 0;
}

void disp(u32 t, u32 cl) {
	u32 i;

	for (i = 0; i < t; ++i){
		REG(LCD_TOP_FILLCOLOR) = cl;
		svcSleepThread(5000000);
	}
	REG(LCD_TOP_FILLCOLOR) = 0;
}

void panicHandle(const char *file, int file_len, int line, int column) {
	char file_c[LOCAL_MSG_BUF_SIZE];
	file_len = file_len > LOCAL_MSG_BUF_SIZE - 1 ? LOCAL_MSG_BUF_SIZE - 1 : file_len;
	memcpy(file_c, file, file_len);
	file_c[file_len] = 0;
	showMsgRaw("Panic: %s:%d:%d", file_c, line, column);
}
