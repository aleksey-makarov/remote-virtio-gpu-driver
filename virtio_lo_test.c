// SPDX-License-Identifier: GPL-2.0-or-later

#include <linux/cdev.h>
#include <linux/debugfs.h>
#include <linux/completion.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/freezer.h>
#include <linux/fs.h>
#include <linux/splice.h>
#include <linux/pagemap.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/poll.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/virtio.h>
#include <linux/wait.h>
#include <linux/workqueue.h>
#include <linux/module.h>
#include <linux/dma-mapping.h>
#include <linux/virtio_config.h>

#include "lib/test.h"

#define MTRACE_FILE "virtio_lo_test.c"
#include "mtrace.h"

struct virtio_lo_test_device {
	struct virtio_device *vdev;

	struct workqueue_struct *wq;
	struct work_struct work;

	/*
	 * RX
	 */
#define RX_BUFFERS_LEN 8
#define RX_BUFFERS_MIN 2
	struct scatterlist rx_buffers[RX_BUFFERS_LEN];
	unsigned int rx_buffers_n;
	struct virtqueue *rx_vq;

	/*
	 * TX
	 */
	struct virtqueue *tx_vq;

	/*
	 * NOTIFY
	 */
#define NOTIFY_BUFFERS_LEN 24
#define NOTIFY_BUFFERS_MIN 4
	struct scatterlist notify_buffers[NOTIFY_BUFFERS_LEN];
	unsigned int notify_buffers_n;
	struct virtqueue *notify_vq;

	/*
	 * CTRL
	 */
	struct virtqueue *ctrl_vq;
};

#define RANDBUFFER_MAX_LENGTH 8
struct randbuffer {
	unsigned int len;
	struct scatterlist sg[RANDBUFFER_MAX_LENGTH];
};

static struct randbuffer *randbuffer_alloc(void)
{
	struct randbuffer *rb = kmalloc(sizeof(struct randbuffer), GFP_KERNEL);
	if (!rb) {
		MTRACE("kmalloc()");
		return NULL;
	}

	unsigned int i;

	unsigned char len;
	get_random_bytes(&len, sizeof(len));
	rb->len = (unsigned int)(len % (RANDBUFFER_MAX_LENGTH - 1)) + 1;

	sg_init_table(rb->sg, rb->len);

	for (i = 0; i < rb->len; i++) {

		struct scatterlist *sg = rb->sg + i;

		struct page *p = alloc_page(GFP_KERNEL);
		if (!p) {
			MTRACE("* alloc_page()");
			goto error;
		}

		unsigned int length;
		get_random_bytes(&length, sizeof(length));
		length = (length % (PAGE_SIZE - 1)) + 1;

		unsigned int offset;
		get_random_bytes(&offset, sizeof(offset));
		offset = offset % (PAGE_SIZE - length);

		sg_set_page(sg, p, length, offset);
	}

	MTRACE("new buffer @%p, len=%u", rb, rb->len);
	for (i = 0; i < rb->len; i++) {
		MTRACE("0x%04x@[0x%04x..0x%04x]",
			rb->sg[i].length, rb->sg[i].offset,
			rb->sg[i].offset + rb->sg[i].length - 1);
	}

	return rb;

error:
	for (unsigned int j = 0; j < i; j++) {
		struct page *p = sg_page(rb->sg + j);
		__free_page(p);
	}
	kfree(rb);
	return NULL;
}

static void randbuffer_free(struct randbuffer *rb)
{
	for (unsigned int i = 0; i < rb->len; i++) {
		struct page *p = sg_page(rb->sg + i);
		__free_page(p);
	}
	kfree(rb);
}

static inline struct virtio_lo_test_device *work_to_virtio_lo_test_device(struct work_struct *w)
{
	return container_of(w, struct virtio_lo_test_device, work);
}

static void notify_start(struct virtio_lo_test_device *d)
{
	(void)d;
	MTRACE();
}

static void notify_stop(struct virtio_lo_test_device *d)
{
	(void)d;
	MTRACE();
}

static void receive(struct virtio_lo_test_device *d, void *data, unsigned int len)
{
	(void)d;
	int lenx = min(32, (int)len);
	MTRACE("%*pE (%u)", lenx, data, len);
}

static void work_func_rx(struct virtio_lo_test_device *d)
{
	int err;
	bool kick;

	kick = false;
	while (1) {
		unsigned int len;
		struct scatterlist *sg = virtqueue_get_buf(d->rx_vq, &len);
		if (!sg)
			break;

		void *rx = sg_virt(sg);
		receive(d, rx, len);

		err = virtqueue_add_inbuf(d->rx_vq, sg, 1, sg, GFP_KERNEL);
		if (err < 0) {
			MTRACE("* rx virtqueue_add_inbuf(): %d", err);
			__free_page(sg_page(sg));
			if (--d->rx_buffers_n < RX_BUFFERS_MIN) {
				// FIXME: should stop
				MTRACE("* critical number of rx buffers: %u", d->rx_buffers_n);
			}
			continue;
		}
		kick = true;
	}

	if (kick)
		virtqueue_kick(d->rx_vq);
}

static void work_func_notify(struct virtio_lo_test_device *d)
{
	int err;
	bool kick;

	kick = false;
	while (1) {
		unsigned int len;
		struct scatterlist *sg = virtqueue_get_buf(d->notify_vq, &len);
		if (!sg)
			break;

		struct virtio_test_notify *notify = sg_virt(sg);
		switch(notify->id) {
		case VIRTIO_TEST_QUEUE_NOTIFY_START:
			notify_start(d);
			break;
		case VIRTIO_TEST_QUEUE_NOTIFY_STOP:
			notify_stop(d);
			break;
		default:
			MTRACE("???");
			break;
		}

		err = virtqueue_add_inbuf(d->notify_vq, sg, 1, sg, GFP_KERNEL);
		if (err < 0) {
			MTRACE("* notify virtqueue_add_inbuf(): %d", err);
			__free_page(sg_page(sg));
			if (--d->notify_buffers_n < NOTIFY_BUFFERS_MIN) {
				// FIXME: should stop
				MTRACE("* critical number of notify buffers: %u", d->notify_buffers_n);
			}
			continue;
		}
		kick = true;
	}

	if (kick)
		virtqueue_kick(d->notify_vq);
}

static void work_func(struct work_struct *work)
{
	struct virtio_lo_test_device *d = work_to_virtio_lo_test_device(work);
	work_func_notify(d);
	work_func_rx(d);
}

static void sg_init_one_page(struct scatterlist *sg, struct page *p)
{
	sg_init_table(sg, 1);
	sg_set_page(sg, p, PAGE_SIZE, 0);
}

static void queue_fini(struct virtqueue *q)
{
	while (1) {
		unsigned int len;
		struct scatterlist *sg = virtqueue_get_buf(q, &len);
		if (!sg)
			break;
		struct page *p = sg_page(sg);
		__free_page(p);
	}

	while (1) {
		struct scatterlist *sg = virtqueue_detach_unused_buf(q);
		if (!sg)
			break;
		struct page *p = sg_page(sg);
		__free_page(p);
	}
}

static void notify_queue_fini(struct virtio_lo_test_device *d)
{
	MTRACE();
	queue_fini(d->notify_vq);
}

static void rx_queue_fini(struct virtio_lo_test_device *d)
{
	MTRACE();
	queue_fini(d->rx_vq);
}

static int queue_init(struct virtqueue *q, struct scatterlist *sgs, unsigned int sgs_len)
{
	unsigned int i;
	int ret;

	for (i = 0; i < sgs_len; i++) {
		struct scatterlist *sg = sgs + i;

		struct page *p = alloc_page(GFP_KERNEL);
		if (!p) {
			ret = -ENOMEM;
			MTRACE("* alloc_page()");
			goto error_free;
		}

		sg_init_one_page(sg, p);

		ret = virtqueue_add_inbuf(q, sg, 1, sg, GFP_KERNEL);
		if (ret < 0) {
			__free_page(p);
			MTRACE("* virtqueue_add_inbuf(): %d, buffer %u", ret, i);
			if (i == 0)
				goto error;
			break;
		}
	}

	ret = i;
	MTRACE("created %u buffers", i);

	virtqueue_kick(q);

	MTRACE("ok");
	return ret;

error_free:
	queue_fini(q);
error:
	MTRACE("ret: %d", ret);
	return ret;
}

static int notify_queue_init(struct virtio_lo_test_device *d)
{
	MTRACE();
	int ret = queue_init(d->notify_vq, d->notify_buffers, NOTIFY_BUFFERS_LEN);
	if (ret < 0)
		return ret;
	if (ret < NOTIFY_BUFFERS_MIN) {
		dev_err(&d->vdev->dev, "virtqueue_add_inbuf(): %d", ret);
		MTRACE("");
		queue_fini(d->notify_vq);
		return -ENODEV;
	}
	d->notify_buffers_n = ret;
	return 0;
}

static int rx_queue_init(struct virtio_lo_test_device *d)
{
	MTRACE();
	int ret = queue_init(d->rx_vq, d->rx_buffers, RX_BUFFERS_LEN);
	if (ret < 0)
		return ret;
	if (ret < RX_BUFFERS_MIN) {
		dev_err(&d->vdev->dev, "virtqueue_add_inbuf(): %d", ret);
		MTRACE("");
		queue_fini(d->rx_vq);
		return -ENODEV;
	}
	d->rx_buffers_n = ret;
	return 0;
}

static inline void intr(struct virtqueue *vq)
{
	struct virtio_lo_test_device *dev = vq->vdev->priv;
	bool ret = queue_work(dev->wq, &dev->work);
	if (!ret)
		MTRACE("already in queue");
}

static void rx_intr(struct virtqueue *vq)
{
	MTRACE();
	intr(vq);
}

static void tx_intr(struct virtqueue *vq)
{
	MTRACE();
	intr(vq);
}

static void notify_intr(struct virtqueue *vq)
{
	// MTRACE();
	intr(vq);
}

static void ctrl_intr(struct virtqueue *vq)
{
	MTRACE();
	intr(vq);
}

static void device_config_changed(struct virtio_device *vdev)
{
	MTRACE();
}

static void device_remove(struct virtio_device *vdev)
{
	struct virtio_lo_test_device *dev;

	dev = vdev->priv;

	MTRACE("dev=%p", dev);

	/* Device is going away, exit any polling for buffers */
	virtio_break_device(vdev);

	/* Disable interrupts for vqs */
	virtio_reset_device(vdev);

	destroy_workqueue(dev->wq);

	MTRACE("notify_queue_fini()");
	notify_queue_fini(dev);
	rx_queue_fini(dev);

	kfree(dev);
	vdev->priv = NULL;

	MTRACE("ok");
}

static int device_probe(struct virtio_device *vdev)
{
	static unsigned int serial = 0;
	struct virtio_lo_test_device *dev;
	vq_callback_t *io_callbacks[] = {
		[VIRTIO_TEST_QUEUE_RX]     = rx_intr,
		[VIRTIO_TEST_QUEUE_TX]     = tx_intr,
		[VIRTIO_TEST_QUEUE_NOTIFY] = notify_intr,
		[VIRTIO_TEST_QUEUE_CTRL]   = ctrl_intr,
	};
	const char *io_names[] = {
		[VIRTIO_TEST_QUEUE_RX]     = "rx",
		[VIRTIO_TEST_QUEUE_TX]     = "tx",
		[VIRTIO_TEST_QUEUE_NOTIFY] = "notify",
		[VIRTIO_TEST_QUEUE_CTRL]   = "ctrl",
	};
	struct virtqueue *vqs[VIRTIO_TEST_QUEUE_MAX];
	int err;

	MTRACE();

	/* Ensure to read early_put_chars now */
	barrier();

	dev = kmalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return -ENOMEM;

	/* Attach this dev to this virtio_device, and vice-versa. */
	dev->vdev = vdev;
	vdev->priv = dev;

	/* Find the queues. */
	err = virtio_find_vqs(dev->vdev, VIRTIO_TEST_QUEUE_MAX, vqs,
			      io_callbacks,
			      io_names, NULL);
	if (err) {
		MTRACE("* virtio_find_vqs(): %d", err);
		goto error_free;
	}

	dev->notify_vq = vqs[VIRTIO_TEST_QUEUE_NOTIFY];
	dev->ctrl_vq   = vqs[VIRTIO_TEST_QUEUE_CTRL];
	dev->rx_vq     = vqs[VIRTIO_TEST_QUEUE_RX];
	dev->tx_vq     = vqs[VIRTIO_TEST_QUEUE_TX];

	err = notify_queue_init(dev);
	if (err) {
		MTRACE("notify_queue_init()");
		goto error_free;
	}

	err = rx_queue_init(dev);
	if (err) {
		MTRACE("rx_queue_init()");
		goto error_notify_queue_fini;
	}

	// MTRACE("virtio-lo-test-%p (should be 0x%lx)", dev, (long unsigned int)dev);
	dev->wq = alloc_ordered_workqueue("virtio-lo-test-%u", 0, serial++);
	if (!dev->wq) {
		MTRACE("alloc_ordered_workqueue()");
		goto error_rx_queue_fini;

	}

	INIT_WORK(&dev->work, work_func);

	virtio_device_ready(dev->vdev);

	MTRACE("ok");
	return 0;

error_rx_queue_fini:
	rx_queue_fini(dev);
error_notify_queue_fini:
	notify_queue_fini(dev);
error_free:
	kfree(dev);
	vdev->priv = NULL;
	MTRACE("error: %d", err);
	return err;
}

static const struct virtio_device_id id_table[] = {
	{ VIRTIO_ID_TEST, VIRTIO_DEV_ANY_ID },
	{ 0 },
};
MODULE_DEVICE_TABLE(virtio, id_table);

static const unsigned int features[] = {
	VIRTIO_TEST_F_SIZE,
	VIRTIO_TEST_F_MULTIPORT,
};

static struct virtio_driver virtio_lo_test = {
	.feature_table      = features,
	.feature_table_size = ARRAY_SIZE(features),
	.driver.name        = KBUILD_MODNAME,
	.driver.owner       = THIS_MODULE,
	.id_table           = id_table,
	.probe              = device_probe,
	.remove             = device_remove,
	.config_changed     = device_config_changed,
#if 0
	.freeze =	virtcons_freeze,
	.restore =	virtcons_restore,
#endif
};

static int __init virtio_lo_test_init(void)
{
	int err;

	MTRACE();

	// to test this and silence warnings
	MTRACE("PAGE_SIZE: 0x%08lx", PAGE_SIZE);
	for (unsigned int i = 0; i < 20; i++)
		randbuffer_free(randbuffer_alloc());

	err = register_virtio_driver(&virtio_lo_test);
	if (err < 0) {
		pr_err("Error %d registering virtio driver\n", err);
		goto error;
	}

	MTRACE("ok");
	return 0;

error:
	MTRACE("error: %d", err);
	return err;
}

static void __exit virtio_lo_test_fini(void)
{
	MTRACE();

	unregister_virtio_driver(&virtio_lo_test);
}
module_init(virtio_lo_test_init);
module_exit(virtio_lo_test_fini);

MODULE_DESCRIPTION("Virtio test driver");
MODULE_LICENSE("GPL");
