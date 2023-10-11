#ifndef __test_h__
#define __test_h__

#include <linux/virtio_types.h>

#define VIRTIO_ID_TEST 111

#define VIRTIO_TEST_F_SIZE	0
#define VIRTIO_TEST_F_MULTIPORT 1
#define VIRTIO_TEST_F_EMERG_WRITE 2

struct virtio_test_config {
	__virtio64 something;
};

#define VIRTIO_TEST_DEVICE_READY	0
#define VIRTIO_TEST_PORT_ADD		1
#define VIRTIO_TEST_PORT_REMOVE	2
#define VIRTIO_TEST_PORT_READY	3
#define VIRTIO_TEST_CONSOLE_PORT	4
#define VIRTIO_TEST_RESIZE		5
#define VIRTIO_TEST_PORT_OPEN	6
#define VIRTIO_TEST_PORT_NAME	7

struct virtio_test_control {
	__virtio32 id;
	__virtio16 event;
	__virtio16 value;
};

#endif
