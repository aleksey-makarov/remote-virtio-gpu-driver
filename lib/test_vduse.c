#include <stdlib.h>
#include <unistd.h>

#include <linux/virtio_config.h>

#include "libvduse.h"

#include "test.h"

#define TRACE_FILE "test_vduse.c"
#include "trace.h"

#define DRIVER_NAME "test"
#define QUEUE_SIZE 16
static const uint64_t driver_features = 1ULL << VIRTIO_F_IOMMU_PLATFORM
				      | 1ULL << VIRTIO_F_VERSION_1
				      ;

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
	int err;

	(void)argc;
	(void)argv;

	trace("hello");

	struct virtio_test_config dev_cfg = {
		.something = 0xdeadbeef12345678,
	};

	VduseDev *dev = vduse_dev_create(DRIVER_NAME, VIRTIO_ID_TEST, VIRTIO_TEST_VENDOR_ID,
					 driver_features, VIRTIO_TEST_QUEUE_MAX,
					 sizeof(struct virtio_test_config), (char *)&dev_cfg, &ops, NULL);
	if (!dev) {
		trace_err("vduse_dev_create()");
		goto error;
	}

	err = vduse_set_reconnect_log_file(dev, "/tmp/vduse-" DRIVER_NAME ".log");
	if (err) {
		trace_err("vduse_set_reconnect_log_file()");
		goto error_dev_destroy;
	}

	for (int i = 0; i < VIRTIO_TEST_QUEUE_MAX; i++) {
		vduse_dev_setup_queue(dev, i, QUEUE_SIZE);
	}

	sleep(20);

	vduse_dev_destroy(dev);

	trace("exit");
	exit(EXIT_SUCCESS);

error_dev_destroy:
	vduse_dev_destroy(dev);
error:
	trace_err("exit failure");
	exit(EXIT_FAILURE);
}
