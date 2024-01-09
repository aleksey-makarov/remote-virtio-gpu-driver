#ifndef __container_of__
#define __container_of__

#include <stddef.h>

#define container_of(ptr, type, member) ({                    \
	const typeof( ((type *)0)->member ) *__mptr = (ptr);  \
	(type *)( (char *)__mptr - offsetof(type,member) );})

#endif
