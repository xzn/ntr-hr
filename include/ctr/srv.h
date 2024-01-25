#ifndef SRV_H
#define SRV_H

Result initSrv();
Result exitSrv();
Result srv_RegisterClient(Handle* handleptr);
Result srv_getServiceHandle(Handle* handleptr, Handle* out, char* server);
Result srvGetServiceHandle(Handle* out, char* server);

extern Handle srvHandle;
#endif
