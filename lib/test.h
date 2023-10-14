#ifndef __test_h__
#define __test_h__

#include <linux/types.h>

#define VIRTIO_ID_TEST 111

struct virtio_test_config {
	__le64 something;
};

#endif
