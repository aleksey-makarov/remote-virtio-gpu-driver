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

#define VIRTIO_TEST_QUEUE_RX      0
#define VIRTIO_TEST_QUEUE_TX      1
#define VIRTIO_TEST_QUEUE_CTRL_RX 2
#define VIRTIO_TEST_QUEUE_CTRL_TX 3
#define VIRTIO_TEST_QUEUE_MAX     4

#define VIRTIO_TEST_QUEUE_RX_START 0
#define VIRTIO_TEST_QUEUE_RX_STOP  1

struct virtio_test_ctrl_rx {
	__virtio32 id;
};

struct virtio_test_ctrl_reply {
	__virtio32 id;
};

#endif
