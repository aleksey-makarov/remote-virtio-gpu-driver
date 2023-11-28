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
#include <linux/minmax.h>

#include "virtio_test.h"

#define MTRACE_FILE "virtio_test.c"
#include "mtrace.h"

#include "stream_gen.h"

enum {
	STATE_DOWN,
	STATE_ECHO,
};

struct virtio_lo_test_device {
	struct virtio_device *vdev;

	struct workqueue_struct *wq;
	struct work_struct work;
	int state;
	int state_requested;

	/*
	 * RX
	 */
	struct virtqueue *rx_vq;
	struct stream_gen rx_stream;
	struct randbuffer *rx_buf;
	unsigned long rx_count;
	int rx_inflight;

	/*
	 * TX
	 */
	struct virtqueue *tx_vq;
	struct stream_gen tx_stream;
	struct randbuffer *tx_buf;
	unsigned long int tx_count;
	int tx_inflight;

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

static inline struct virtio_lo_test_device *work_to_virtio_lo_test_device(struct work_struct *w)
{
	return container_of(w, struct virtio_lo_test_device, work);
}

static inline struct virtio_lo_test_device *device_to_virtio_lo_test_device(struct device *w)
{
	struct virtio_device *vdev = dev_to_virtio(w);
	return vdev->priv;
}

static const char *state_string(struct virtio_lo_test_device *lo)
{
	if (lo->state == STATE_DOWN) {
		if (lo->state_requested == STATE_DOWN)
			return "DOWN";
		else
			return "DOWN+";
	} else if (lo->state == STATE_ECHO) {
		if (lo->state_requested == STATE_ECHO)
			return "ECHO";
		else
			return "ECHO-";
	}
	return "???";
}

static void print_stats(struct virtio_lo_test_device *lo)
{
	MTRACE("tx=0x%016lx (%d), rx=0x%016lx (%d)", lo->tx_count, lo->tx_inflight, lo->rx_count, lo->rx_inflight);
}

#define RANDBUFFER_MAX_LENGTH 8
struct randbuffer {
	unsigned int sgn;
	unsigned int capacity;
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
	rb->sgn = (unsigned int)(len % (RANDBUFFER_MAX_LENGTH - 1)) + 1;
	rb->capacity = 0;

	sg_init_table(rb->sg, rb->sgn);

	for (i = 0; i < rb->sgn; i++) {

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
		rb->capacity += length;
	}

	// MTRACE("new buffer @%p, sgn=%u, capacity=0x%x", rb, rb->sgn, rb->capacity);
	// for (i = 0; i < rb->sgn; i++) {
	// 	MTRACE("0x%04x@[0x%04x..0x%04x]",
	// 		rb->sg[i].length, rb->sg[i].offset,
	// 		rb->sg[i].offset + rb->sg[i].length - 1);
	// }

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
	for (unsigned int i = 0; i < rb->sgn; i++) {
		struct page *p = sg_page(rb->sg + i);
		__free_page(p);
	}
	kfree(rb);
}

static void work_func_rx(struct virtio_lo_test_device *lo)
{
	bool kick;
	int err;

	kick = false;
	while (1) {
		struct scatterlist *sg;
		unsigned int len;
		unsigned int i;

		struct randbuffer *rb = virtqueue_get_buf(lo->rx_vq, &len);
		if (!rb)
			break;

		lo->rx_inflight--;
		for_each_sg(rb->sg, sg, rb->sgn, i) {
			if (!len)
				break;
			unsigned int l = min(len, sg->length);
			err = stream_gen_test(&lo->rx_stream, sg_virt(sg), l);
			if (err) {
				MTRACE("*** error");
				// FIXME: switch to error state
				break;
			}
			len -= l;
			lo->rx_count += l;
		}

		randbuffer_free(rb);
	}

	if (lo->state_requested == STATE_DOWN) {
		if (lo->rx_count == lo->tx_count) {
			// There could be buffers inflight in rx ring
			// if (d->tx_inflight || d->rx_inflight) {
			// 	MTRACE("* tx_inflight=%d, rx_inflight=%d", d->tx_inflight, d->rx_inflight);
			// }
			lo->state = STATE_DOWN;
			return;
		}
	}

	while (1) {
		struct randbuffer *rb;
		if (lo->rx_buf) {
			rb = lo->rx_buf;
			lo->rx_buf = NULL;
		} else {
			rb = randbuffer_alloc();
			if (!rb) {
				MTRACE("* randbuffer_alloc()");
				// FIXME: switch to error state
				break;
			}
		}

		err = virtqueue_add_inbuf(lo->rx_vq, rb->sg, rb->sgn, rb, GFP_KERNEL);
		if (err < 0) {
			lo->rx_buf = rb;
			break;
		}
		lo->rx_inflight++;
		kick = true;
	}

	if (kick)
		virtqueue_kick(lo->rx_vq);
}

static void work_func_tx(struct virtio_lo_test_device *lo)
{
	bool kick;
	int err;

	kick = false;
	while (1) {
		unsigned int len;
		struct randbuffer *rb = virtqueue_get_buf(lo->tx_vq, &len);
		if (!rb)
			break;

		lo->tx_inflight--;
		randbuffer_free(rb);
	}

	if (lo->state_requested != STATE_ECHO)
		return;

	while (1) {
		struct scatterlist *sg;
		unsigned int i;
		struct randbuffer *rb;

		if (lo->tx_buf) {
			rb = lo->tx_buf;
			lo->tx_buf = NULL;
		} else {
			rb = randbuffer_alloc();
			if (!rb) {
				MTRACE("* randbuffer_alloc()");
				// FIXME: switch to error state
				break;
			}
			for_each_sg(rb->sg, sg, rb->sgn, i) {
				stream_gen(&lo->tx_stream, sg_virt(sg), sg->length);
			}
		}

		err = virtqueue_add_outbuf(lo->tx_vq, rb->sg, rb->sgn, rb, GFP_KERNEL);
		if (err < 0) {
			lo->tx_buf = rb;
			break;
		}
		lo->tx_inflight++;
		lo->tx_count += rb->capacity;
		kick = true;
	}

	if (kick)
		virtqueue_kick(lo->tx_vq);
}

static void work_func_notify(struct virtio_lo_test_device *lo)
{
	int err;
	bool kick;

	kick = false;
	while (1) {
		unsigned int len;
		struct scatterlist *sg = virtqueue_get_buf(lo->notify_vq, &len);
		if (!sg)
			break;

		struct virtio_test_notify *notify = sg_virt(sg);

		print_stats(lo);
		MTRACE("notify=%d", notify->id);

		err = virtqueue_add_inbuf(lo->notify_vq, sg, 1, sg, GFP_KERNEL);
		if (err < 0) {
			MTRACE("* notify virtqueue_add_inbuf(): %d", err);
			__free_page(sg_page(sg));
			if (--lo->notify_buffers_n < NOTIFY_BUFFERS_MIN) {
				// FIXME: should stop
				MTRACE("* critical number of notify buffers: %u", lo->notify_buffers_n);
			}
			continue;
		}
		kick = true;
	}

	if (kick)
		virtqueue_kick(lo->notify_vq);
}

static void work_func(struct work_struct *work)
{
	struct virtio_lo_test_device *lo = work_to_virtio_lo_test_device(work);

	// FIXME: incorrect static
	static unsigned long last_tx = 0;
	if (lo->tx_count - last_tx > 0x1000000) {
		print_stats(lo);
		last_tx = lo->tx_count;
	}

	work_func_notify(lo);
	work_func_rx(lo);
	work_func_tx(lo);
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

static void queue_fini_randbuffer(struct virtqueue *q)
{
	while (1) {
		unsigned int len;
		struct randbuffer *rb = virtqueue_get_buf(q, &len);
		if (!rb)
			break;
		randbuffer_free(rb);
	}

	while (1) {
		struct randbuffer *rb = virtqueue_detach_unused_buf(q);
		if (!rb)
			break;
		randbuffer_free(rb);
	}
}

static void notify_queue_fini(struct virtio_lo_test_device *lo)
{
	MTRACE();
	queue_fini(lo->notify_vq);
}

static void rx_queue_fini(struct virtio_lo_test_device *lo)
{
	MTRACE();
	queue_fini_randbuffer(lo->rx_vq);
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

static int notify_queue_init(struct virtio_lo_test_device *lo)
{
	MTRACE();
	int ret = queue_init(lo->notify_vq, lo->notify_buffers, NOTIFY_BUFFERS_LEN);
	if (ret < 0)
		return ret;
	if (ret < NOTIFY_BUFFERS_MIN) {
		dev_err(&lo->vdev->dev, "virtqueue_add_inbuf(): %d", ret);
		MTRACE("");
		queue_fini(lo->notify_vq);
		return -ENODEV;
	}
	lo->notify_buffers_n = ret;
	return 0;
}

static inline void lo_intr(struct virtio_lo_test_device *lo)
{
	// bool ret = queue_work(lo->wq, &lo->work);
	// if (!ret)
	// 	MTRACE("already in queue");
	queue_work(lo->wq, &lo->work);
}

static inline void intr(struct virtqueue *vq)
{
	struct virtio_lo_test_device *lo = vq->vdev->priv;
	lo_intr(lo);
}

static void rx_intr(struct virtqueue *vq)
{
	// MTRACE();
	intr(vq);
}

static void tx_intr(struct virtqueue *vq)
{
	// MTRACE();
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

static ssize_t start_show(struct device *dev, struct device_attribute *attr,
                        char *buf)
{
	struct virtio_lo_test_device *lo = device_to_virtio_lo_test_device(dev);
	return sysfs_emit(buf, "%s\n", state_string(lo));
}

static ssize_t start_store(struct device *dev, struct device_attribute *attr,
                        const char *buf, size_t count)
{
	struct virtio_lo_test_device *lo = device_to_virtio_lo_test_device(dev);
	bool v;

	if (kstrtobool(buf, &v) < 0)
		return -EINVAL;

	if (v) {
		lo->state_requested = STATE_ECHO;
		lo_intr(lo);
	} else {
		lo->state_requested = STATE_DOWN;
	}

	return count;
}

static struct device_attribute dev_attr_start = __ATTR_RW(start);

static void device_remove(struct virtio_device *vdev)
{
	struct virtio_lo_test_device *lo = vdev->priv;

	MTRACE("lo=%p", lo);

	device_remove_file(&vdev->dev, &dev_attr_start);

	/* Device is going away, exit any polling for buffers */
	virtio_break_device(vdev);

	/* Disable interrupts for vqs */
	virtio_reset_device(vdev);

	destroy_workqueue(lo->wq);

	MTRACE("notify_queue_fini()");
	notify_queue_fini(lo);
	rx_queue_fini(lo);

	kfree(lo);
	vdev->priv = NULL;

	MTRACE("ok");
}

static int device_probe(struct virtio_device *vdev)
{
	static unsigned int serial = 0;
	struct virtio_lo_test_device *lo;
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

	lo = kmalloc(sizeof(*lo), GFP_KERNEL);
	if (!lo)
		return -ENOMEM;

	/* Attach this dev to this virtio_device, and vice-versa. */
	lo->vdev = vdev;
	vdev->priv = lo;

	unsigned int seed;
	do
		get_random_bytes(&seed, sizeof(seed));
	while(seed == 0);

	stream_gen_init(&lo->rx_stream, seed);
	stream_gen_init(&lo->tx_stream, seed);

	lo->rx_buf =      lo->tx_buf =      NULL;
	lo->rx_inflight = lo->tx_inflight = 0;
	lo->rx_count =    lo->tx_count =    0;

	lo->state = STATE_DOWN;
	lo->state_requested = STATE_DOWN;

	/* Find the queues. */
	err = virtio_find_vqs(lo->vdev, VIRTIO_TEST_QUEUE_MAX, vqs,
			      io_callbacks,
			      io_names, NULL);
	if (err) {
		MTRACE("* virtio_find_vqs(): %d", err);
		goto error_free;
	}

	lo->rx_vq     = vqs[VIRTIO_TEST_QUEUE_RX];
	lo->tx_vq     = vqs[VIRTIO_TEST_QUEUE_TX];
	lo->notify_vq = vqs[VIRTIO_TEST_QUEUE_NOTIFY];
	lo->ctrl_vq   = vqs[VIRTIO_TEST_QUEUE_CTRL];

	err = notify_queue_init(lo);
	if (err) {
		MTRACE("notify_queue_init()");
		goto error_free;
	}

	// MTRACE("virtio-lo-test-%p (should be 0x%lx)", dev, (long unsigned int)dev);
	lo->wq = alloc_ordered_workqueue("virtio-lo-test-%u", 0, serial++);
	if (!lo->wq) {
		MTRACE("alloc_ordered_workqueue()");
		goto error_notify_queue_fini;

	}

	INIT_WORK(&lo->work, work_func);

	virtio_device_ready(lo->vdev);

	err = device_create_file(&vdev->dev, &dev_attr_start);
	if (err < 0) {
		MTRACE("device_create_file()");
		goto error_stop_device;
	}

	MTRACE("ok");
	return 0;

error_stop_device:
	virtio_break_device(vdev);
	virtio_reset_device(vdev);

	destroy_workqueue(lo->wq);

error_notify_queue_fini:
	notify_queue_fini(lo);
error_free:
	kfree(lo);
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

	// // to test this and silence warnings
	// MTRACE("PAGE_SIZE: 0x%08lx", PAGE_SIZE);
	// for (unsigned int i = 0; i < 20; i++)
	// 	randbuffer_free(randbuffer_alloc());

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
