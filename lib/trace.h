#ifndef TRACE_H
#define TRACE_H

#include <stdio.h>
#include <string.h>
#include <errno.h>

#ifndef TRACE_FILE
#define TRACE_FILE __BASE_FILE__
#endif

#define trace( _format, ...) \
	fprintf(stdout, "- %s:%d %s() : " _format "\n", TRACE_FILE, __LINE__, __func__, ##__VA_ARGS__)

#define trace_err( _format, ...) \
	fprintf(stdout, "* %s:%d %s() : " _format "\n", TRACE_FILE, __LINE__, __func__, ##__VA_ARGS__)

#define trace_err_p( _format, ...) \
	fprintf(stdout, "* %s:%d %s() : (%s) " _format "\n", TRACE_FILE, __LINE__, __func__, strerror(errno), ##__VA_ARGS__)

#endif /* TRACE_H */
