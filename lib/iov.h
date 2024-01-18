#ifndef __iov_h__
#define __iov_h__

#include <sys/uio.h>

// FIXME: name with iov_

size_t get_iovec_size(const struct iovec *iov, int iovlen);

size_t read_from_iovec(const struct iovec *iov, int iovlen,
		       size_t offset,
		       void *buf, size_t count);

size_t write_to_iovec(const struct iovec *iov, int iovlen,
		      size_t offset,
		      const void *buf, size_t count);

#endif
