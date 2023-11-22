#include <stdlib.h>

#include <linux/virtio_config.h>

#include "libvduse.h"

#include "test.h"

#define TRACE_FILE "test_vduse.c"
#include "trace.h"

#define DRIVER_NAME "test"

static void test_enable_queue(VduseDev *dev, VduseVirtq *vq)
{
	(void)dev;
	(void)vq;
	trace();
}

static void test_disable_queue(VduseDev *dev, VduseVirtq *vq)
{
	(void)dev;
	(void)vq;
	trace();
}

static const VduseOps ops = {
    .enable_queue  = test_enable_queue,
    .disable_queue = test_disable_queue,
};

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	trace("hello");

	struct virtio_test_config dev_cfg = {
		.something = 0xdeadbeef12345678,
	};

	VduseDev *dev = vduse_dev_create(DRIVER_NAME, VIRTIO_ID_TEST, VIRTIO_TEST_VENDOR_ID,
					 1ULL << VIRTIO_F_IOMMU_PLATFORM, VIRTIO_TEST_QUEUE_MAX,
					 sizeof(struct virtio_test_config), (char *)&dev_cfg, &ops, NULL);
	if (!dev) {
		trace_err("vduse_dev_create()");
		goto error;
	}

	trace("exit");
	exit(EXIT_SUCCESS);

error:
	trace_err("exit failure");
	exit(EXIT_FAILURE);
}
