#ifndef __mtrace_h__
#define __mtrace_h__

#include <linux/stringify.h>
#include <linux/kernel.h>

#ifndef MTRACE_FILE
#define MTRACE_FILE __FILE__
#endif

#define MTRACE_WHERE KERN_INFO "-- " MTRACE_FILE ":" __stringify(__LINE__) " (%s) : "

#define MTRACE( format, ... ) printk( MTRACE_WHERE format "\n" , __FUNCTION__, ##__VA_ARGS__ )

#endif
