#include <sys/uio.h>
#include <assert.h>

#define TRACE_FILE "device.c"
#include "trace.h"

#include "device.h"

#define K 1024
#define M (K * K)
#define BUF_LEN (64 * M)

static char buf[BUF_LEN];
static unsigned long int buf_data;  // first occupied by data
static unsigned long int buf_empty; // first empty: always buf_data <= buf_empty

void device_reset(void)
{
	buf_data = buf_empty = 0;
}

unsigned long int device_get_buffer_size(void)
{
	return BUF_LEN;
}

unsigned long int device_get_data_length(void)
{
	return buf_empty - buf_data;
}

static unsigned int min_ui(unsigned int a, unsigned int b)
{
	return a < b ? a : b;
}

static int device_put1(char *data, unsigned int length)
{
	if (device_get_free_space() < length) {
		trace_err("free=%lu, length=%u", device_get_free_space(), length);
		return -1;
	}

	unsigned int buf_empty_index = buf_empty % BUF_LEN;
	unsigned int length1 = min_ui(length, BUF_LEN - buf_empty_index);

	memcpy(buf + buf_empty_index, data, length1);

	if (length1 != length)
		memcpy(buf, data + length1, length - length1);

	buf_empty += length;

	return 0;
}

int device_put(struct iovec *v, unsigned int n)
{
	unsigned int i;
	int err;

	for (i = 0; i < n; i++) {
		err = device_put1(v[i].iov_base, v[i].iov_len);
		if (err) {
			trace_err("error @%u", i);
			return err;
		}
	}

	return 0;
}

static void device_get1(char *data, unsigned int length)
{
	assert(length <= device_get_data_length());

	unsigned int buf_data_index = buf_data % BUF_LEN;
	unsigned int length1 = min_ui(length, BUF_LEN - buf_data_index);

	memcpy(data, buf + buf_data_index, length1);

	if (length1 != length)
		memcpy(data + length1, buf, length - length1);

	buf_data += length;
}

int device_get(struct iovec *v, unsigned int n)
{
	int ret = 0;
	unsigned int i;

	unsigned int available = device_get_data_length();

	assert(available > 0);

	for (i = 0; i < n && available; i++) {
		unsigned int length1 = min_ui(available, v[i].iov_len);
		device_get1(v[i].iov_base, length1);
		ret += length1;
		available -= length1;
	}

	return ret;
}
