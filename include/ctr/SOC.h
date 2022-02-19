#ifndef SOC_H
#define SOC_H

extern Handle SOCU_handle;

Result SOC_Initialize(u32 *context_addr, u32 context_size);//Example context_size: 0x48000. The specified context buffer can no longer be accessed by the process which called this function, since the userland permissions for this block are set to no-access.
Result SOC_Shutdown();
int SOC_GetErrno();

typedef u32 nfds_t;

struct pollfd
{
	int	fd;
	int	events;
	int	revents;
};

int poll2(struct pollfd *fds, nfds_t nfds, int timeout);
int select2(struct pollfd *pollinfo, int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);

#define POLLIN		0x01
#define POLLPRI		0x02
#define POLLHUP		0x04 // unknown ???
#define POLLERR		0x08 // probably
#define POLLOUT		0x10

#endif
