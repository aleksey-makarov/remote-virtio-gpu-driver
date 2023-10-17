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

#define VIRTIO_TEST_QUEUE_RX     0
#define VIRTIO_TEST_QUEUE_TX     1
#define VIRTIO_TEST_QUEUE_NOTIFY 2
#define VIRTIO_TEST_QUEUE_CTRL   3
#define VIRTIO_TEST_QUEUE_MAX    4

#define VIRTIO_TEST_QUEUE_NOTIFY_START 0
#define VIRTIO_TEST_QUEUE_NOTIFY_STOP  1

struct virtio_test_notify {
	__virtio32 id;
};

#endif
