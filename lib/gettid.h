#ifndef __gettid_h__
#define __gettid_h__

#include <unistd.h>
#include <sys/syscall.h>

static inline int gettid(void)
{
	return syscall(SYS_gettid);
}

#endif
