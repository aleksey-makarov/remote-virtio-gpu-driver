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

#define DEVICE_NAME "virtio-lo-test"

static const struct class port_class = {
	.name = DEVICE_NAME,
};

/* Major number for this device.  Ports will be created as minors. */
static int major;

/*
 * This is a per-device struct that stores data common to all the
 * ports for that device (vdev->priv).
 */
struct ports_device {

	/* The virtio device we're associated with */
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

#if 0
	/* Next portdev in the list, head is in the pdrvdata struct */
	struct list_head list;

	/*
	 * Workqueue handlers where we process deferred work after
	 * notification
	 */
	struct work_struct control_work;
	struct work_struct config_work;

	struct list_head ports;

	/* To protect the list of ports */
	spinlock_t ports_lock;

	/* To protect the vq operations for the control channel */
	spinlock_t c_ivq_lock;
	spinlock_t c_ovq_lock;

	/* max. number of ports this device can hold */
	u32 max_nr_ports;


	/*
	 * A couple of virtqueues for the control channel: one for
	 * guest->host transfers, one for host->guest transfers
	 */
	struct virtqueue *c_ivq, *c_ovq;

	/*
	 * A control packet buffer for guest->host requests, protected
	 * by c_ovq_lock.
	 */
	struct virtio_test_control cpkt;

	/* Array of per-port IO virtqueues */
	struct virtqueue **in_vqs, **out_vqs;
#endif
};

static inline struct ports_device *work_to_ports_device(struct work_struct *w)
{
	return container_of(w, struct ports_device, work);
}

static void notify_start(struct ports_device *d)
{
	(void)d;
	MTRACE();
}

static void notify_stop(struct ports_device *d)
{
	(void)d;
	MTRACE();
}

static void receive(struct ports_device *d, void *data, unsigned int len)
{
	(void)d;
	int lenx = min(16, (int)len);
	MTRACE("%*pE (%u)", lenx, data, len);
}

static void work_func(struct work_struct *work)
{
	struct ports_device *d = work_to_ports_device(work);
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

static void notify_queue_fini(struct ports_device *d)
{
	MTRACE();
	queue_fini(d->notify_vq);
}

static void rx_queue_fini(struct ports_device *d)
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

static int notify_queue_init(struct ports_device *d)
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

static int rx_queue_init(struct ports_device *d)
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

#if 0

/*
 * This is a global struct for storing common data for all the devices
 * this driver handles.
 *
 * Mainly, it has a linked list for all the consoles in one place so
 * that callbacks from hvc for get_chars(), put_chars() work properly
 * across multiple devices and multiple ports per device.
 */
struct ports_driver_data {
	/* Used for exporting per-port information to debugfs */
	struct dentry *debugfs_dir;

	/* List of all the devices we're handling */
	struct list_head portdevs;

	/* All the console devices handled by this driver */
	struct list_head consoles;
};

static struct ports_driver_data pdrvdata;


static DEFINE_SPINLOCK(pdrvdata_lock);
static DECLARE_COMPLETION(early_console_added);

/* This struct holds information that's relevant only for console ports */
struct console {
	/* We'll place all consoles in a list in the pdrvdata struct */
	struct list_head list;

	/* The hvc device associated with this console port */
	struct hvc_struct *hvc;

	/* The size of the console */
	struct winsize ws;

	/*
	 * This number identifies the number that we used to register
	 * with hvc in hvc_instantiate() and hvc_alloc(); this is the
	 * number passed on by the hvc callbacks to us to
	 * differentiate between the other console ports handled by
	 * this driver
	 */
	u32 vtermno;
};

// FIXME: console
// static DEFINE_IDA(vtermno_ida);


struct port_stats {
	unsigned long bytes_sent, bytes_received, bytes_discarded;
};

/* This struct holds the per-port data */
struct port {
	/* Next port in the list, head is in the ports_device */
	struct list_head list;

	/* Pointer to the parent virtio_console device */
	struct ports_device *portdev;

	/* The current buffer from which data has to be fed to readers */
	struct port_buffer *inbuf;

	/*
	 * To protect the operations on the in_vq associated with this
	 * port.  Has to be a spinlock because it can be called from
	 * interrupt context (get_char()).
	 */
	spinlock_t inbuf_lock;

	/* Protect the operations on the out_vq. */
	spinlock_t outvq_lock;

	/* The IO vqs for this port */
	struct virtqueue *in_vq, *out_vq;

	/* File in the debugfs directory that exposes this port's information */
	struct dentry *debugfs_file;

	/*
	 * Keep count of the bytes sent, received and discarded for
	 * this port for accounting and debugging purposes.  These
	 * counts are not reset across port open / close events.
	 */
	struct port_stats stats;

	/*
	 * The entries in this struct will be valid if this port is
	 * hooked up to an hvc console
	 */
	struct console cons;

	/* Each port associates with a separate char device */
	struct cdev *cdev;
	struct device *dev;

	/* Reference-counting to handle port hot-unplugs and file operations */
	struct kref kref;

	/* A waitqueue for poll() or blocking read operations */
	wait_queue_head_t waitqueue;

	/* The 'name' of the port that we expose via sysfs properties */
	char *name;

	/* We can notify apps of host connect / disconnect events via SIGIO */
	struct fasync_struct *async_queue;

	/* The 'id' to identify the port with the Host */
	u32 id;

	bool outvq_full;

	/* Is the host device open */
	bool host_connected;

	/* We should allow only one process to open a port */
	bool guest_connected;
};

/* This is the very early arch-specified put chars function. */
static int (*early_put_chars)(u32, const char *, int);

// FIXME: console
//static struct port *find_port_by_vtermno(u32 vtermno)
//{
//	struct port *port;
//	struct console *cons;
//	unsigned long flags;
//
//	spin_lock_irqsave(&pdrvdata_lock, flags);
//	list_for_each_entry(cons, &pdrvdata.consoles, list) {
//		if (cons->vtermno == vtermno) {
//			port = container_of(cons, struct port, cons);
//			goto out;
//		}
//	}
//	port = NULL;
//out:
//	spin_unlock_irqrestore(&pdrvdata_lock, flags);
//	return port;
//}

static struct port *find_port_by_devt_in_portdev(struct ports_device *portdev,
						 dev_t dev)
{
	struct port *port;
	unsigned long flags;

	spin_lock_irqsave(&portdev->ports_lock, flags);
	list_for_each_entry(port, &portdev->ports, list) {
		if (port->cdev->dev == dev) {
			kref_get(&port->kref);
			goto out;
		}
	}
	port = NULL;
out:
	spin_unlock_irqrestore(&portdev->ports_lock, flags);

	return port;
}

static struct port *find_port_by_devt(dev_t dev)
{
	struct ports_device *portdev;
	struct port *port;
	unsigned long flags;

	spin_lock_irqsave(&pdrvdata_lock, flags);
	list_for_each_entry(portdev, &pdrvdata.portdevs, list) {
		port = find_port_by_devt_in_portdev(portdev, dev);
		if (port)
			goto out;
	}
	port = NULL;
out:
	spin_unlock_irqrestore(&pdrvdata_lock, flags);
	return port;
}

static struct port *find_port_by_id(struct ports_device *portdev, u32 id)
{
	struct port *port;
	unsigned long flags;

	spin_lock_irqsave(&portdev->ports_lock, flags);
	list_for_each_entry(port, &portdev->ports, list)
		if (port->id == id)
			goto out;
	port = NULL;
out:
	spin_unlock_irqrestore(&portdev->ports_lock, flags);

	return port;
}

static struct port *find_port_by_vq(struct ports_device *portdev,
				    struct virtqueue *vq)
{
	struct port *port;
	unsigned long flags;

	spin_lock_irqsave(&portdev->ports_lock, flags);
	list_for_each_entry(port, &portdev->ports, list)
		if (port->in_vq == vq || port->out_vq == vq)
			goto out;
	port = NULL;
out:
	spin_unlock_irqrestore(&portdev->ports_lock, flags);
	return port;
}

// FIXME: console
//static bool is_console_port(struct port *port)
//{
//	if (port->cons.hvc)
//		return true;
//	return false;
//}

static inline bool use_multiport(struct ports_device *portdev)
{
	/*
	 * This condition can be true when put_chars is called from
	 * early_init
	 */
	if (!portdev->vdev)
		return false;
	return __virtio_test_bit(portdev->vdev, VIRTIO_TEST_F_MULTIPORT);
}

static DEFINE_SPINLOCK(dma_bufs_lock);
static LIST_HEAD(pending_free_dma_bufs);

static void free_buf(struct port_buffer *buf, bool can_sleep)
{
	unsigned int i;

	for (i = 0; i < buf->sgpages; i++) {
		struct page *page = sg_page(&buf->sg[i]);
		if (!page)
			break;
		put_page(page);
	}

	if (!buf->dev) {
		kfree(buf->buf);
	} else if (is_rproc_enabled) {
		unsigned long flags;

		/* dma_free_coherent requires interrupts to be enabled. */
		if (!can_sleep) {
			/* queue up dma-buffers to be freed later */
			spin_lock_irqsave(&dma_bufs_lock, flags);
			list_add_tail(&buf->list, &pending_free_dma_bufs);
			spin_unlock_irqrestore(&dma_bufs_lock, flags);
			return;
		}
		dma_free_coherent(buf->dev, buf->size, buf->buf, buf->dma);

		/* Release device refcnt and allow it to be freed */
		put_device(buf->dev);
	}

	kfree(buf);
}

static void reclaim_dma_bufs(void)
{
	unsigned long flags;
	struct port_buffer *buf, *tmp;
	LIST_HEAD(tmp_list);

	if (list_empty(&pending_free_dma_bufs))
		return;

	/* Create a copy of the pending_free_dma_bufs while holding the lock */
	spin_lock_irqsave(&dma_bufs_lock, flags);
	list_cut_position(&tmp_list, &pending_free_dma_bufs,
			  pending_free_dma_bufs.prev);
	spin_unlock_irqrestore(&dma_bufs_lock, flags);

	/* Release the dma buffers, without irqs enabled */
	list_for_each_entry_safe(buf, tmp, &tmp_list, list) {
		list_del(&buf->list);
		free_buf(buf, true);
	}
}

static struct port_buffer *alloc_buf(struct virtio_device *vdev, size_t buf_size,
				     int pages)
{
	struct port_buffer *buf;

	reclaim_dma_bufs();

	/*
	 * Allocate buffer and the sg list. The sg list array is allocated
	 * directly after the port_buffer struct.
	 */
	buf = kmalloc(struct_size(buf, sg, pages), GFP_KERNEL);
	if (!buf)
		goto fail;

	buf->sgpages = pages;
	if (pages > 0) {
		buf->dev = NULL;
		buf->buf = NULL;
		return buf;
	}

//	if (is_rproc_serial(vdev)) {
//		/*
//		 * Allocate DMA memory from ancestor. When a virtio
//		 * device is created by remoteproc, the DMA memory is
//		 * associated with the parent device:
//		 * virtioY => remoteprocX#vdevYbuffer.
//		 */
//		buf->dev = vdev->dev.parent;
//		if (!buf->dev)
//			goto free_buf;
//
//		/* Increase device refcnt to avoid freeing it */
//		get_device(buf->dev);
//		buf->buf = dma_alloc_coherent(buf->dev, buf_size, &buf->dma,
//					      GFP_KERNEL);
//	} else {
		buf->dev = NULL;
		buf->buf = kmalloc(buf_size, GFP_KERNEL);
//	}

	if (!buf->buf)
		goto free_buf;
	buf->len = 0;
	buf->offset = 0;
	buf->size = buf_size;
	return buf;

free_buf:
	kfree(buf);
fail:
	return NULL;
}

/* Callers should take appropriate locks */
static struct port_buffer *get_inbuf(struct port *port)
{
	struct port_buffer *buf;
	unsigned int len;

	if (port->inbuf)
		return port->inbuf;

	buf = virtqueue_get_buf(port->in_vq, &len);
	if (buf) {
		buf->len = min_t(size_t, len, buf->size);
		buf->offset = 0;
		port->stats.bytes_received += len;
	}
	return buf;
}

/*
 * Create a scatter-gather list representing our input buffer and put
 * it in the queue.
 *
 * Callers should take appropriate locks.
 */
static int add_inbuf(struct virtqueue *vq, struct port_buffer *buf)
{
	struct scatterlist sg[1];
	int ret;

	sg_init_one(sg, buf->buf, buf->size);

	ret = virtqueue_add_inbuf(vq, sg, 1, buf, GFP_ATOMIC);
	virtqueue_kick(vq);
	if (!ret)
		ret = vq->num_free;
	return ret;
}

/* Discard any unread data this port has. Callers lockers. */
static void discard_port_data(struct port *port)
{
	struct port_buffer *buf;
	unsigned int err;

	if (!port->portdev) {
		/* Device has been unplugged.  vqs are already gone. */
		return;
	}
	buf = get_inbuf(port);

	err = 0;
	while (buf) {
		port->stats.bytes_discarded += buf->len - buf->offset;
		if (add_inbuf(port->in_vq, buf) < 0) {
			err++;
			free_buf(buf, false);
		}
		port->inbuf = NULL;
		buf = get_inbuf(port);
	}
	if (err)
		dev_warn(port->dev, "Errors adding %d buffers back to vq\n",
			 err);
}

static bool port_has_data(struct port *port)
{
	unsigned long flags;
	bool ret;

	ret = false;
	spin_lock_irqsave(&port->inbuf_lock, flags);
	port->inbuf = get_inbuf(port);
	if (port->inbuf)
		ret = true;

	spin_unlock_irqrestore(&port->inbuf_lock, flags);
	return ret;
}

static ssize_t __send_control_msg(struct ports_device *portdev, u32 port_id,
				  unsigned int event, unsigned int value)
{
	struct scatterlist sg[1];
	struct virtqueue *vq;
	unsigned int len;

	if (!use_multiport(portdev))
		return 0;

	vq = portdev->c_ovq;

	spin_lock(&portdev->c_ovq_lock);

	portdev->cpkt.id = cpu_to_virtio32(portdev->vdev, port_id);
	portdev->cpkt.event = cpu_to_virtio16(portdev->vdev, event);
	portdev->cpkt.value = cpu_to_virtio16(portdev->vdev, value);

	sg_init_one(sg, &portdev->cpkt, sizeof(struct virtio_test_control));

	if (virtqueue_add_outbuf(vq, sg, 1, &portdev->cpkt, GFP_ATOMIC) == 0) {
		virtqueue_kick(vq);
		while (!virtqueue_get_buf(vq, &len)
			&& !virtqueue_is_broken(vq))
			cpu_relax();
	}

	spin_unlock(&portdev->c_ovq_lock);
	return 0;
}

static ssize_t send_control_msg(struct port *port, unsigned int event,
				unsigned int value)
{
	/* Did the port get unplugged before userspace closed it? */
	if (port->portdev)
		return __send_control_msg(port->portdev, port->id, event, value);
	return 0;
}


/* Callers must take the port->outvq_lock */
static void reclaim_consumed_buffers(struct port *port)
{
	struct port_buffer *buf;
	unsigned int len;

	if (!port->portdev) {
		/* Device has been unplugged.  vqs are already gone. */
		return;
	}
	while ((buf = virtqueue_get_buf(port->out_vq, &len))) {
		free_buf(buf, false);
		port->outvq_full = false;
	}
}

static ssize_t __send_to_port(struct port *port, struct scatterlist *sg,
			      int nents, size_t in_count,
			      void *data, bool nonblock)
{
	struct virtqueue *out_vq;
	int err;
	unsigned long flags;
	unsigned int len;

	out_vq = port->out_vq;

	spin_lock_irqsave(&port->outvq_lock, flags);

	reclaim_consumed_buffers(port);

	err = virtqueue_add_outbuf(out_vq, sg, nents, data, GFP_ATOMIC);

	/* Tell Host to go! */
	virtqueue_kick(out_vq);

	if (err) {
		in_count = 0;
		goto done;
	}

	if (out_vq->num_free == 0)
		port->outvq_full = true;

	if (nonblock)
		goto done;

	/*
	 * Wait till the host acknowledges it pushed out the data we
	 * sent.  This is done for data from the hvc_console; the tty
	 * operations are performed with spinlocks held so we can't
	 * sleep here.  An alternative would be to copy the data to a
	 * buffer and relax the spinning requirement.  The downside is
	 * we need to kmalloc a GFP_ATOMIC buffer each time the
	 * console driver writes something out.
	 */
	while (!virtqueue_get_buf(out_vq, &len)
		&& !virtqueue_is_broken(out_vq))
		cpu_relax();
done:
	spin_unlock_irqrestore(&port->outvq_lock, flags);

	port->stats.bytes_sent += in_count;
	/*
	 * We're expected to return the amount of data we wrote -- all
	 * of it
	 */
	return in_count;
}

/*
 * Give out the data that's requested from the buffer that we have
 * queued up.
 */
static ssize_t fill_readbuf(struct port *port, char __user *out_buf,
			    size_t out_count, bool to_user)
{
	struct port_buffer *buf;
	unsigned long flags;

	if (!out_count || !port_has_data(port))
		return 0;

	buf = port->inbuf;
	out_count = min(out_count, buf->len - buf->offset);

	if (to_user) {
		ssize_t ret;

		ret = copy_to_user(out_buf, buf->buf + buf->offset, out_count);
		if (ret)
			return -EFAULT;
	} else {
		memcpy((__force char *)out_buf, buf->buf + buf->offset,
		       out_count);
	}

	buf->offset += out_count;

	if (buf->offset == buf->len) {
		/*
		 * We're done using all the data in this buffer.
		 * Re-queue so that the Host can send us more data.
		 */
		spin_lock_irqsave(&port->inbuf_lock, flags);
		port->inbuf = NULL;

		if (add_inbuf(port->in_vq, buf) < 0)
			dev_warn(port->dev, "failed add_buf\n");

		spin_unlock_irqrestore(&port->inbuf_lock, flags);
	}
	/* Return the number of bytes actually copied */
	return out_count;
}

/* The condition that must be true for polling to end */
static bool will_read_block(struct port *port)
{
	if (!port->guest_connected) {
		/* Port got hot-unplugged. Let's exit. */
		return false;
	}
	return !port_has_data(port) && port->host_connected;
}

static bool will_write_block(struct port *port)
{
	bool ret;

	if (!port->guest_connected) {
		/* Port got hot-unplugged. Let's exit. */
		return false;
	}
	if (!port->host_connected)
		return true;

	spin_lock_irq(&port->outvq_lock);
	/*
	 * Check if the Host has consumed any buffers since we last
	 * sent data (this is only applicable for nonblocking ports).
	 */
	reclaim_consumed_buffers(port);
	ret = port->outvq_full;
	spin_unlock_irq(&port->outvq_lock);

	return ret;
}

#endif

static ssize_t port_fops_read(struct file *filp, char __user *ubuf,
			      size_t count, loff_t *offp)
{
#if 0
	struct port *port;
	ssize_t ret;

	port = filp->private_data;

	/* Port is hot-unplugged. */
	if (!port->guest_connected)
		return -ENODEV;

	if (!port_has_data(port)) {
		/*
		 * If nothing's connected on the host just return 0 in
		 * case of list_empty; this tells the userspace app
		 * that there's no connection
		 */
		if (!port->host_connected)
			return 0;
		if (filp->f_flags & O_NONBLOCK)
			return -EAGAIN;

		ret = wait_event_freezable(port->waitqueue,
					   !will_read_block(port));
		if (ret < 0)
			return ret;
	}
	/* Port got hot-unplugged while we were waiting above. */
	if (!port->guest_connected)
		return -ENODEV;
	/*
	 * We could've received a disconnection message while we were
	 * waiting for more data.
	 *
	 * This check is not clubbed in the if() statement above as we
	 * might receive some data as well as the host could get
	 * disconnected after we got woken up from our wait.  So we
	 * really want to give off whatever data we have and only then
	 * check for host_connected.
	 */
	if (!port_has_data(port) && !port->host_connected)
		return 0;

	return fill_readbuf(port, ubuf, count, true);
#endif
	return 0;
}

// static int wait_port_writable(struct port *port, bool nonblock)
// {
// 	int ret;
// 
// 	if (will_write_block(port)) {
// 		if (nonblock)
// 			return -EAGAIN;
// 
// 		ret = wait_event_freezable(port->waitqueue,
// 					   !will_write_block(port));
// 		if (ret < 0)
// 			return ret;
// 	}
// 	/* Port got hot-unplugged. */
// 	if (!port->guest_connected)
// 		return -ENODEV;
// 
// 	return 0;
// }

static ssize_t port_fops_write(struct file *filp, const char __user *ubuf,
			       size_t count, loff_t *offp)
{
#if 0
	struct port *port;
	struct port_buffer *buf;
	ssize_t ret;
	bool nonblock;
	struct scatterlist sg[1];

	/* Userspace could be out to fool us */
	if (!count)
		return 0;

	port = filp->private_data;

	nonblock = filp->f_flags & O_NONBLOCK;

	ret = wait_port_writable(port, nonblock);
	if (ret < 0)
		return ret;

	count = min((size_t)(32 * 1024), count);

	buf = alloc_buf(port->portdev->vdev, count, 0);
	if (!buf)
		return -ENOMEM;

	ret = copy_from_user(buf->buf, ubuf, count);
	if (ret) {
		ret = -EFAULT;
		goto free_buf;
	}

	/*
	 * We now ask send_buf() to not spin for generic ports -- we
	 * can re-use the same code path that non-blocking file
	 * descriptors take for blocking file descriptors since the
	 * wait is already done and we're certain the write will go
	 * through to the host.
	 */
	nonblock = true;
	sg_init_one(sg, buf->buf, count);
	ret = __send_to_port(port, sg, 1, count, buf, nonblock);

	if (nonblock && ret > 0)
		goto out;

free_buf:
	free_buf(buf, true);
out:
	return ret;
#endif
	return 0;
}

#if 0

struct sg_list {
	unsigned int n;
	unsigned int size;
	size_t len;
	struct scatterlist *sg;
};

static int pipe_to_sg(struct pipe_inode_info *pipe, struct pipe_buffer *buf,
			struct splice_desc *sd)
{
	struct sg_list *sgl = sd->u.data;
	unsigned int offset, len;

	if (sgl->n == sgl->size)
		return 0;

	/* Try lock this page */
	if (pipe_buf_try_steal(pipe, buf)) {
		/* Get reference and unlock page for moving */
		get_page(buf->page);
		unlock_page(buf->page);

		len = min(buf->len, sd->len);
		sg_set_page(&(sgl->sg[sgl->n]), buf->page, len, buf->offset);
	} else {
		/* Failback to copying a page */
		struct page *page = alloc_page(GFP_KERNEL);
		char *src;

		if (!page)
			return -ENOMEM;

		offset = sd->pos & ~PAGE_MASK;

		len = sd->len;
		if (len + offset > PAGE_SIZE)
			len = PAGE_SIZE - offset;

		src = kmap_atomic(buf->page);
		memcpy(page_address(page) + offset, src + buf->offset, len);
		kunmap_atomic(src);

		sg_set_page(&(sgl->sg[sgl->n]), page, len, offset);
	}
	sgl->n++;
	sgl->len += len;

	return len;
}

/* Faster zero-copy write by splicing */
static ssize_t port_fops_splice_write(struct pipe_inode_info *pipe,
				      struct file *filp, loff_t *ppos,
				      size_t len, unsigned int flags)
{
	struct port *port = filp->private_data;
	struct sg_list sgl;
	ssize_t ret;
	struct port_buffer *buf;
	struct splice_desc sd = {
		.total_len = len,
		.flags = flags,
		.pos = *ppos,
		.u.data = &sgl,
	};
	unsigned int occupancy;

//	/*
//	 * Rproc_serial does not yet support splice. To support splice
//	 * pipe_to_sg() must allocate dma-buffers and copy content from
//	 * regular pages to dma pages. And alloc_buf and free_buf must
//	 * support allocating and freeing such a list of dma-buffers.
//	 */
//	if (is_rproc_serial(port->out_vq->vdev))
//		return -EINVAL;

	pipe_lock(pipe);
	ret = 0;
	if (pipe_empty(pipe->head, pipe->tail))
		goto error_out;

	ret = wait_port_writable(port, filp->f_flags & O_NONBLOCK);
	if (ret < 0)
		goto error_out;

	occupancy = pipe_occupancy(pipe->head, pipe->tail);
	buf = alloc_buf(port->portdev->vdev, 0, occupancy);

	if (!buf) {
		ret = -ENOMEM;
		goto error_out;
	}

	sgl.n = 0;
	sgl.len = 0;
	sgl.size = occupancy;
	sgl.sg = buf->sg;
	sg_init_table(sgl.sg, sgl.size);
	ret = __splice_from_pipe(pipe, &sd, pipe_to_sg);
	pipe_unlock(pipe);
	if (likely(ret > 0))
		ret = __send_to_port(port, buf->sg, sgl.n, sgl.len, buf, true);

	if (unlikely(ret <= 0))
		free_buf(buf, true);
	return ret;

error_out:
	pipe_unlock(pipe);
	return ret;
}

static __poll_t port_fops_poll(struct file *filp, poll_table *wait)
{
	struct port *port;
	__poll_t ret;

	port = filp->private_data;
	poll_wait(filp, &port->waitqueue, wait);

	if (!port->guest_connected) {
		/* Port got unplugged */
		return EPOLLHUP;
	}
	ret = 0;
	if (!will_read_block(port))
		ret |= EPOLLIN | EPOLLRDNORM;
	if (!will_write_block(port))
		ret |= EPOLLOUT;
	if (!port->host_connected)
		ret |= EPOLLHUP;

	return ret;
}

static void remove_port(struct kref *kref);

#endif

static int port_fops_release(struct inode *inode, struct file *filp)
{
#if 0
	struct port *port;

	port = filp->private_data;

	/* Notify host of port being closed */
	send_control_msg(port, VIRTIO_TEST_PORT_OPEN, 0);

	spin_lock_irq(&port->inbuf_lock);
	port->guest_connected = false;

	discard_port_data(port);

	spin_unlock_irq(&port->inbuf_lock);

	spin_lock_irq(&port->outvq_lock);
	reclaim_consumed_buffers(port);
	spin_unlock_irq(&port->outvq_lock);

	reclaim_dma_bufs();
	/*
	 * Locks aren't necessary here as a port can't be opened after
	 * unplug, and if a port isn't unplugged, a kref would already
	 * exist for the port.  Plus, taking ports_lock here would
	 * create a dependency on other locks taken by functions
	 * inside remove_port if we're the last holder of the port,
	 * creating many problems.
	 */
	kref_put(&port->kref, remove_port);
#endif
	return 0;
}

static int port_fops_open(struct inode *inode, struct file *filp)
{
#if 0
	struct cdev *cdev = inode->i_cdev;
	struct port *port;
	int ret;

	/* We get the port with a kref here */
	port = find_port_by_devt(cdev->dev);
	if (!port) {
		/* Port was unplugged before we could proceed */
		return -ENXIO;
	}
	filp->private_data = port;

// FIXME: console
//	/*
//	 * Don't allow opening of console port devices -- that's done
//	 * via /dev/hvc
//	 */
//	if (is_console_port(port)) {
//		ret = -ENXIO;
//		goto out;
//	}

	/* Allow only one process to open a particular port at a time */
	spin_lock_irq(&port->inbuf_lock);
	if (port->guest_connected) {
		spin_unlock_irq(&port->inbuf_lock);
		ret = -EBUSY;
		goto out;
	}

	port->guest_connected = true;
	spin_unlock_irq(&port->inbuf_lock);

	spin_lock_irq(&port->outvq_lock);
	/*
	 * There might be a chance that we missed reclaiming a few
	 * buffers in the window of the port getting previously closed
	 * and opening now.
	 */
	reclaim_consumed_buffers(port);
	spin_unlock_irq(&port->outvq_lock);

	nonseekable_open(inode, filp);

	/* Notify host of port being opened */
	send_control_msg(filp->private_data, VIRTIO_TEST_PORT_OPEN, 1);

	return 0;
out:
	kref_put(&port->kref, remove_port);
	return ret;
#endif
	return 0;
}

#if 0

static int port_fops_fasync(int fd, struct file *filp, int mode)
{
	struct port *port;

	port = filp->private_data;
	return fasync_helper(fd, filp, mode, &port->async_queue);
}

/*
 * The put_chars() callback is pretty straightforward.
 *
 * We turn the characters into a scatter-gather list, add it to the
 * output queue and then kick the Host.  Then we sit here waiting for
 * it to finish: inefficient in theory, but in practice
 * implementations will do it immediately.
 */
// FIXME: console
//static int put_chars(u32 vtermno, const char *buf, int count)
//{
//	struct port *port;
//	struct scatterlist sg[1];
//	void *data;
//	int ret;
//
//	if (unlikely(early_put_chars))
//		return early_put_chars(vtermno, buf, count);
//
//	port = find_port_by_vtermno(vtermno);
//	if (!port)
//		return -EPIPE;
//
//	data = kmemdup(buf, count, GFP_ATOMIC);
//	if (!data)
//		return -ENOMEM;
//
//	sg_init_one(sg, data, count);
//	ret = __send_to_port(port, sg, 1, count, data, false);
//	kfree(data);
//	return ret;
//}

/*
 * get_chars() is the callback from the hvc_console infrastructure
 * when an interrupt is received.
 *
 * We call out to fill_readbuf that gets us the required data from the
 * buffers that are queued up.
 */
// FIXME: console
//static int get_chars(u32 vtermno, char *buf, int count)
//{
//	struct port *port;
//
//	/* If we've not set up the port yet, we have no input to give. */
//	if (unlikely(early_put_chars))
//		return 0;
//
//	port = find_port_by_vtermno(vtermno);
//	if (!port)
//		return -EPIPE;
//
//	/* If we don't have an input queue yet, we can't get input. */
//	BUG_ON(!port->in_vq);
//
//	return fill_readbuf(port, (__force char __user *)buf, count, false);
//}

// FIXME: console
// static void resize_console(struct port *port)
//{
//	struct virtio_device *vdev;
//
//	/* The port could have been hot-unplugged */
//	if (!port || !is_console_port(port))
//		return;
//
//	vdev = port->portdev->vdev;
//
//	/* Don't test F_SIZE at all if we're rproc: not a valid feature! */
//	if (!is_rproc_serial(vdev) &&
//	    virtio_has_feature(vdev, VIRTIO_CONSOLE_F_SIZE))
//		hvc_resize(port->cons.hvc, port->cons.ws);
//}

// FIXME: console
///* We set the configuration at this point, since we now have a tty */
//static int notifier_add_vio(struct hvc_struct *hp, int data)
//{
//	struct port *port;
//
//	port = find_port_by_vtermno(hp->vtermno);
//	if (!port)
//		return -EINVAL;
//
//	hp->irq_requested = 1;
//	resize_console(port);
//
//	return 0;
//}

// FIXME: console
//static void notifier_del_vio(struct hvc_struct *hp, int data)
//{
//	hp->irq_requested = 0;
//}

// FIXME: console
///* The operations for console ports. */
//static const struct hv_ops hv_ops = {
//	.get_chars = get_chars,
//	.put_chars = put_chars,
//	.notifier_add = notifier_add_vio,
//	.notifier_del = notifier_del_vio,
//	.notifier_hangup = notifier_del_vio,
//};

// FIXME: console
///*
// * Console drivers are initialized very early so boot messages can go
// * out, so we do things slightly differently from the generic virtio
// * initialization of the net and block drivers.
// *
// * At this stage, the console is output-only.  It's too early to set
// * up a virtqueue, so we let the drivers do some boutique early-output
// * thing.
// */
//int __init virtio_cons_early_init(int (*put_chars)(u32, const char *, int))
//{
//	early_put_chars = put_chars;
//	return hvc_instantiate(0, 0, &hv_ops);
//}

// FIXME: console
//static int init_port_console(struct port *port)
//{
//	int ret;
//
//	/*
//	 * The Host's telling us this port is a console port.  Hook it
//	 * up with an hvc console.
//	 *
//	 * To set up and manage our virtual console, we call
//	 * hvc_alloc().
//	 *
//	 * The first argument of hvc_alloc() is the virtual console
//	 * number.  The second argument is the parameter for the
//	 * notification mechanism (like irq number).  We currently
//	 * leave this as zero, virtqueues have implicit notifications.
//	 *
//	 * The third argument is a "struct hv_ops" containing the
//	 * put_chars() get_chars(), notifier_add() and notifier_del()
//	 * pointers.  The final argument is the output buffer size: we
//	 * can do any size, so we put PAGE_SIZE here.
//	 */
//	ret = ida_alloc_min(&vtermno_ida, 1, GFP_KERNEL);
//	if (ret < 0)
//		return ret;
//
//	port->cons.vtermno = ret;
//	port->cons.hvc = hvc_alloc(port->cons.vtermno, 0, &hv_ops, PAGE_SIZE);
//	if (IS_ERR(port->cons.hvc)) {
//		ret = PTR_ERR(port->cons.hvc);
//		dev_err(port->dev,
//			"error %d allocating hvc for port\n", ret);
//		port->cons.hvc = NULL;
//		ida_free(&vtermno_ida, port->cons.vtermno);
//		return ret;
//	}
//	spin_lock_irq(&pdrvdata_lock);
//	list_add_tail(&port->cons.list, &pdrvdata.consoles);
//	spin_unlock_irq(&pdrvdata_lock);
//	port->guest_connected = true;
//
//	/*
//	 * Start using the new console output if this is the first
//	 * console to come up.
//	 */
//	if (early_put_chars)
//		early_put_chars = NULL;
//
//	/* Notify host of port being opened */
//	send_control_msg(port, VIRTIO_TEST_PORT_OPEN, 1);
//
//	return 0;
//}

static ssize_t show_port_name(struct device *dev,
			      struct device_attribute *attr, char *buffer)
{
	struct port *port;

	port = dev_get_drvdata(dev);

	return sprintf(buffer, "%s\n", port->name);
}

static DEVICE_ATTR(name, S_IRUGO, show_port_name, NULL);

static struct attribute *port_sysfs_entries[] = {
	&dev_attr_name.attr,
	NULL
};

static const struct attribute_group port_attribute_group = {
	.name = NULL,		/* put in device directory */
	.attrs = port_sysfs_entries,
};

static int port_debugfs_show(struct seq_file *s, void *data)
{
	struct port *port = s->private;

	seq_printf(s, "name: %s\n", port->name ? port->name : "");
	seq_printf(s, "guest_connected: %d\n", port->guest_connected);
	seq_printf(s, "host_connected: %d\n", port->host_connected);
	seq_printf(s, "outvq_full: %d\n", port->outvq_full);
	seq_printf(s, "bytes_sent: %lu\n", port->stats.bytes_sent);
	seq_printf(s, "bytes_received: %lu\n", port->stats.bytes_received);
	seq_printf(s, "bytes_discarded: %lu\n", port->stats.bytes_discarded);
// FIXME: console
//	seq_printf(s, "is_console: %s\n",
//		   is_console_port(port) ? "yes" : "no");
	seq_printf(s, "console_vtermno: %u\n", port->cons.vtermno);

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(port_debugfs);

// FIXME: console
//static void set_console_size(struct port *port, u16 rows, u16 cols)
//{
//	if (!port || !is_console_port(port))
//		return;
//
//	port->cons.ws.ws_row = rows;
//	port->cons.ws.ws_col = cols;
//}

static int fill_queue(struct virtqueue *vq, spinlock_t *lock)
{
	struct port_buffer *buf;
	int nr_added_bufs;
	int ret;

	nr_added_bufs = 0;
	do {
		buf = alloc_buf(vq->vdev, PAGE_SIZE, 0);
		if (!buf)
			return -ENOMEM;

		spin_lock_irq(lock);
		ret = add_inbuf(vq, buf);
		if (ret < 0) {
			spin_unlock_irq(lock);
			free_buf(buf, true);
			return ret;
		}
		nr_added_bufs++;
		spin_unlock_irq(lock);
	} while (ret > 0);

	return nr_added_bufs;
}

static void send_sigio_to_port(struct port *port)
{
	if (port->async_queue && port->guest_connected)
		kill_fasync(&port->async_queue, SIGIO, POLL_OUT);
}

static int add_port(struct ports_device *portdev, u32 id)
{
	char debugfs_name[16];
	struct port *port;
	dev_t devt;
	int err;

	port = kmalloc(sizeof(*port), GFP_KERNEL);
	if (!port) {
		err = -ENOMEM;
		goto fail;
	}
	kref_init(&port->kref);

	port->portdev = portdev;
	port->id = id;

	port->name = NULL;
	port->inbuf = NULL;
	port->cons.hvc = NULL;
	port->async_queue = NULL;

	port->cons.ws.ws_row = port->cons.ws.ws_col = 0;
	port->cons.vtermno = 0;

	port->host_connected = port->guest_connected = false;
	port->stats = (struct port_stats) { 0 };

	port->outvq_full = false;

	port->in_vq = portdev->in_vqs[port->id];
	port->out_vq = portdev->out_vqs[port->id];

	port->cdev = cdev_alloc();
	if (!port->cdev) {
		dev_err(&port->portdev->vdev->dev, "Error allocating cdev\n");
		err = -ENOMEM;
		goto free_port;
	}
	port->cdev->ops = &port_fops;

	devt = MKDEV(major, id);
	err = cdev_add(port->cdev, devt, 1);
	if (err < 0) {
		dev_err(&port->portdev->vdev->dev,
			"Error %d adding cdev for port %u\n", err, id);
		goto free_cdev;
	}
	port->dev = device_create(&port_class, &port->portdev->vdev->dev,
				  devt, port, "vport%up%u",
				  port->portdev->vdev->index, id);
	if (IS_ERR(port->dev)) {
		err = PTR_ERR(port->dev);
		dev_err(&port->portdev->vdev->dev,
			"Error %d creating device for port %u\n",
			err, id);
		goto free_cdev;
	}

	spin_lock_init(&port->inbuf_lock);
	spin_lock_init(&port->outvq_lock);
	init_waitqueue_head(&port->waitqueue);

	/* We can safely ignore ENOSPC because it means
	 * the queue already has buffers. Buffers are removed
	 * only by virtcons_remove(), not by unplug_port()
	 */
	err = fill_queue(port->in_vq, &port->inbuf_lock);
	if (err < 0 && err != -ENOSPC) {
		dev_err(port->dev, "Error allocating inbufs\n");
		goto free_device;
	}

//	if (is_rproc_serial(port->portdev->vdev))
//		/*
//		 * For rproc_serial assume remote processor is connected.
//		 * rproc_serial does not want the console port, only
//		 * the generic port implementation.
//		 */
//		port->host_connected = true;
//	else if (!use_multiport(port->portdev)) {
//		/*
//		 * If we're not using multiport support,
//		 * this has to be a console port.
//		 */
// FIXME: console
//		err = init_port_console(port);
//		if (err)
//			goto free_inbufs;
	}

	spin_lock_irq(&portdev->ports_lock);
	list_add_tail(&port->list, &port->portdev->ports);
	spin_unlock_irq(&portdev->ports_lock);

	/*
	 * Tell the Host we're set so that it can send us various
	 * configuration parameters for this port (eg, port name,
	 * caching, whether this is a console port, etc.)
	 */
	send_control_msg(port, VIRTIO_TEST_PORT_READY, 1);

	/*
	 * Finally, create the debugfs file that we can use to
	 * inspect a port's state at any time
	 */
	snprintf(debugfs_name, sizeof(debugfs_name), "vport%up%u",
		 port->portdev->vdev->index, id);
	port->debugfs_file = debugfs_create_file(debugfs_name, 0444,
						 pdrvdata.debugfs_dir,
						 port, &port_debugfs_fops);
	return 0;

// FIXME: console
//free_inbufs:
free_device:
	device_destroy(&port_class, port->dev->devt);
free_cdev:
	cdev_del(port->cdev);
free_port:
	kfree(port);
fail:
	/* The host might want to notify management sw about port add failure */
	__send_control_msg(portdev, id, VIRTIO_TEST_PORT_READY, 0);
	return err;
}

/* No users remain, remove all port-specific data. */
static void remove_port(struct kref *kref)
{
	struct port *port;

	port = container_of(kref, struct port, kref);

	kfree(port);
}

static void remove_port_data(struct port *port)
{
	spin_lock_irq(&port->inbuf_lock);
	/* Remove unused data this port might have received. */
	discard_port_data(port);
	spin_unlock_irq(&port->inbuf_lock);

	spin_lock_irq(&port->outvq_lock);
	reclaim_consumed_buffers(port);
	spin_unlock_irq(&port->outvq_lock);
}

/*
 * Port got unplugged.  Remove port from portdev's list and drop the
 * kref reference.  If no userspace has this port opened, it will
 * result in immediate removal the port.
 */
static void unplug_port(struct port *port)
{
	spin_lock_irq(&port->portdev->ports_lock);
	list_del(&port->list);
	spin_unlock_irq(&port->portdev->ports_lock);

	spin_lock_irq(&port->inbuf_lock);
	if (port->guest_connected) {
		/* Let the app know the port is going down. */
		send_sigio_to_port(port);

		/* Do this after sigio is actually sent */
		port->guest_connected = false;
		port->host_connected = false;

		wake_up_interruptible(&port->waitqueue);
	}
	spin_unlock_irq(&port->inbuf_lock);

// FIXME: console
//	if (is_console_port(port)) {
//		spin_lock_irq(&pdrvdata_lock);
//		list_del(&port->cons.list);
//		spin_unlock_irq(&pdrvdata_lock);
//		hvc_remove(port->cons.hvc);
//		ida_free(&vtermno_ida, port->cons.vtermno);
//	}

	remove_port_data(port);

	/*
	 * We should just assume the device itself has gone off --
	 * else a close on an open port later will try to send out a
	 * control message.
	 */
	port->portdev = NULL;

	sysfs_remove_group(&port->dev->kobj, &port_attribute_group);
	device_destroy(&port_class, port->dev->devt);
	cdev_del(port->cdev);

	debugfs_remove(port->debugfs_file);
	kfree(port->name);

	/*
	 * Locks around here are not necessary - a port can't be
	 * opened after we removed the port struct from ports_list
	 * above.
	 */
	kref_put(&port->kref, remove_port);
}

/* Any private messages that the Host and Guest want to share */
static void handle_control_message(struct virtio_device *vdev,
				   struct ports_device *portdev,
				   struct port_buffer *buf)
{
	struct virtio_console_control *cpkt;
	struct port *port;
	size_t name_size;
	int err;

	cpkt = (struct virtio_console_control *)(buf->buf + buf->offset);

	port = find_port_by_id(portdev, virtio32_to_cpu(vdev, cpkt->id));
	if (!port &&
	    cpkt->event != cpu_to_virtio16(vdev, VIRTIO_TEST_PORT_ADD)) {
		/* No valid header at start of buffer.  Drop it. */
		dev_dbg(&portdev->vdev->dev,
			"Invalid index %u in control packet\n", cpkt->id);
		return;
	}

	switch (virtio16_to_cpu(vdev, cpkt->event)) {
	case VIRTIO_TEST_PORT_ADD:
		if (port) {
			dev_dbg(&portdev->vdev->dev,
				"Port %u already added\n", port->id);
			send_control_msg(port, VIRTIO_TEST_PORT_READY, 1);
			break;
		}
		if (virtio32_to_cpu(vdev, cpkt->id) >=
		    portdev->max_nr_ports) {
			dev_warn(&portdev->vdev->dev,
				"Request for adding port with "
				"out-of-bound id %u, max. supported id: %u\n",
				cpkt->id, portdev->max_nr_ports - 1);
			break;
		}
		add_port(portdev, virtio32_to_cpu(vdev, cpkt->id));
		break;
	case VIRTIO_TEST_PORT_REMOVE:
		unplug_port(port);
		break;
	case VIRTIO_TEST_CONSOLE_PORT:
// FIXME: console
//		if (!cpkt->value)
//			break;
//		if (is_console_port(port))
//			break;
//
//		init_port_console(port);
//		complete(&early_console_added);
//		/*
//		 * Could remove the port here in case init fails - but
//		 * have to notify the host first.
//		 */
		break;
	case VIRTIO_TEST_RESIZE: {
// FIXME: console
//		struct {
//			__u16 rows;
//			__u16 cols;
//		} size;
//
//		if (!is_console_port(port))
//			break;
//
//		memcpy(&size, buf->buf + buf->offset + sizeof(*cpkt),
//		       sizeof(size));
//		set_console_size(port, size.rows, size.cols);
//
//		port->cons.hvc->irq_requested = 1;
//		resize_console(port);
		break;
	}
	case VIRTIO_TEST_PORT_OPEN:
		port->host_connected = virtio16_to_cpu(vdev, cpkt->value);
		wake_up_interruptible(&port->waitqueue);
		/*
		 * If the host port got closed and the host had any
		 * unconsumed buffers, we'll be able to reclaim them
		 * now.
		 */
		spin_lock_irq(&port->outvq_lock);
		reclaim_consumed_buffers(port);
		spin_unlock_irq(&port->outvq_lock);

		/*
		 * If the guest is connected, it'll be interested in
		 * knowing the host connection state changed.
		 */
		spin_lock_irq(&port->inbuf_lock);
		send_sigio_to_port(port);
		spin_unlock_irq(&port->inbuf_lock);
		break;
	case VIRTIO_TEST_PORT_NAME:
		/*
		 * If we woke up after hibernation, we can get this
		 * again.  Skip it in that case.
		 */
		if (port->name)
			break;

		/*
		 * Skip the size of the header and the cpkt to get the size
		 * of the name that was sent
		 */
		name_size = buf->len - buf->offset - sizeof(*cpkt) + 1;

		port->name = kmalloc(name_size, GFP_KERNEL);
		if (!port->name) {
			dev_err(port->dev,
				"Not enough space to store port name\n");
			break;
		}
		strscpy(port->name, buf->buf + buf->offset + sizeof(*cpkt),
			name_size);

		/*
		 * Since we only have one sysfs attribute, 'name',
		 * create it only if we have a name for the port.
		 */
		err = sysfs_create_group(&port->dev->kobj,
					 &port_attribute_group);
		if (err) {
			dev_err(port->dev,
				"Error %d creating sysfs device attributes\n",
				err);
		} else {
			/*
			 * Generate a udev event so that appropriate
			 * symlinks can be created based on udev
			 * rules.
			 */
			kobject_uevent(&port->dev->kobj, KOBJ_CHANGE);
		}
		break;
	}
}

static void control_work_handler(struct work_struct *work)
{
	struct ports_device *portdev;
	struct virtqueue *vq;
	struct port_buffer *buf;
	unsigned int len;

	portdev = container_of(work, struct ports_device, control_work);
	vq = portdev->c_ivq;

	spin_lock(&portdev->c_ivq_lock);
	while ((buf = virtqueue_get_buf(vq, &len))) {
		spin_unlock(&portdev->c_ivq_lock);

		buf->len = min_t(size_t, len, buf->size);
		buf->offset = 0;

		handle_control_message(vq->vdev, portdev, buf);

		spin_lock(&portdev->c_ivq_lock);
		if (add_inbuf(portdev->c_ivq, buf) < 0) {
			dev_warn(&portdev->vdev->dev,
				 "Error adding buffer to queue\n");
			free_buf(buf, false);
		}
	}
	spin_unlock(&portdev->c_ivq_lock);
}

static void flush_bufs(struct virtqueue *vq, bool can_sleep)
{
	struct port_buffer *buf;
	unsigned int len;

	while ((buf = virtqueue_get_buf(vq, &len)))
		free_buf(buf, can_sleep);
}

static void config_work_handler(struct work_struct *work)
{
// FIXME: console
//	struct ports_device *portdev;
//
//	portdev = container_of(work, struct ports_device, config_work);
//	if (!use_multiport(portdev)) {
//		struct virtio_device *vdev;
//		struct port *port;
//		u16 rows, cols;
//
//		vdev = portdev->vdev;
//		virtio_cread(vdev, struct virtio_console_config, cols, &cols);
//		virtio_cread(vdev, struct virtio_console_config, rows, &rows);
//
//		port = find_port_by_id(portdev, 0);
//		set_console_size(port, rows, cols);
//
//		/*
//		 * We'll use this way of resizing only for legacy
//		 * support.  For newer userspace
//		 * (VIRTIO_CONSOLE_F_MULTPORT+), use control messages
//		 * to indicate console size changes so that it can be
//		 * done per-port.
//		 */
//		resize_console(port);
//	}
}

#endif

#if 0
static void rx_intr(struct virtqueue *vq)
{
	MTRACE();

	struct port *port;
	unsigned long flags;

	port = find_port_by_vq(vq->vdev->priv, vq);
	if (!port) {
		flush_bufs(vq, false);
		return;
	}

	spin_lock_irqsave(&port->inbuf_lock, flags);
	port->inbuf = get_inbuf(port);

	/*
	 * Normally the port should not accept data when the port is
	 * closed. For generic serial ports, the host won't (shouldn't)
	 * send data till the guest is connected. But this condition
	 * can be reached when a console port is not yet connected (no
	 * tty is spawned) and the other side sends out data over the
	 * vring, or when a remote devices start sending data before
	 * the ports are opened.
	 *
	 * A generic serial port will discard data if not connected,
	 * while console ports and rproc-serial ports accepts data at
	 * any time. rproc-serial is initiated with guest_connected to
	 * false because port_fops_open expects this. Console ports are
	 * hooked up with an HVC console and is initialized with
	 * guest_connected to true.
	 */

	if (!port->guest_connected && !is_rproc_serial(port->portdev->vdev))
		discard_port_data(port);

	/* Send a SIGIO indicating new data in case the process asked for it */
	send_sigio_to_port(port);

	spin_unlock_irqrestore(&port->inbuf_lock, flags);

	wake_up_interruptible(&port->waitqueue);

// FIXME: console
//	if (is_console_port(port) && hvc_poll(port->cons.hvc))
//		hvc_kick();

}
#endif

static inline void intr(struct virtqueue *vq)
{
	struct ports_device *portdev = vq->vdev->priv;
	bool ret = queue_work(portdev->wq, &portdev->work);
	MTRACE("%s", ret ? "true" : "false");
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
	MTRACE();
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
	// struct ports_device *portdev;
	// portdev = vdev->priv;
	// if (!use_multiport(portdev))
	// 	schedule_work(&portdev->config_work);
}

#if 0

static const struct file_operations portdev_fops = {
	.owner = THIS_MODULE,
};

static void remove_vqs(struct ports_device *portdev)
{
	struct virtqueue *vq;

	virtio_device_for_each_vq(portdev->vdev, vq) {
		struct port_buffer *buf;

		flush_bufs(vq, true);
		while ((buf = virtqueue_detach_unused_buf(vq)))
			free_buf(buf, true);
		cond_resched();
	}
	portdev->vdev->config->del_vqs(portdev->vdev);
	kfree(portdev->in_vqs);
	kfree(portdev->out_vqs);
}

#endif

static void device_remove(struct virtio_device *vdev)
{
	struct ports_device *portdev;
	// struct port *port, *port2;

	portdev = vdev->priv;

	MTRACE("portdev=%p", portdev);

	// spin_lock_irq(&pdrvdata_lock);
	// list_del(&portdev->list);
	// spin_unlock_irq(&pdrvdata_lock);

	/* Device is going away, exit any polling for buffers */
	virtio_break_device(vdev);

	// if (use_multiport(portdev))
	// 	flush_work(&portdev->control_work);
	// else
	// 	flush_work(&portdev->config_work);

	/* Disable interrupts for vqs */
	virtio_reset_device(vdev);

	destroy_workqueue(portdev->wq);

	MTRACE("notify_queue_fini()");
	notify_queue_fini(portdev);
	rx_queue_fini(portdev);


	// /* Finish up work that's lined up */
	// if (use_multiport(portdev))
	// 	cancel_work_sync(&portdev->control_work);
	// else
	// 	cancel_work_sync(&portdev->config_work);

	// list_for_each_entry_safe(port, port2, &portdev->ports, list)
	// 	unplug_port(port);

	// /*
	//  * When yanking out a device, we immediately lose the
	//  * (device-side) queues.  So there's no point in keeping the
	//  * guest side around till we drop our final reference.  This
	//  * also means that any ports which are in an open state will
	//  * have to just stop using the port, as the vqs are going
	//  * away.
	//  */
	// remove_vqs(portdev);

	kfree(portdev);
	vdev->priv = NULL;

	MTRACE("ok");
}

/*
 * Once we're further in boot, we get probed like any other virtio
 * device.
 *
 * If the host also supports multiple console ports, we check the
 * config space to see how many ports the host has spawned.  We
 * initialize each port found.
 */
static int device_probe(struct virtio_device *vdev)
{
	static unsigned int serial = 0;
	struct ports_device *portdev;
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

	portdev = kmalloc(sizeof(*portdev), GFP_KERNEL);
	if (!portdev)
		return -ENOMEM;

	/* Attach this portdev to this virtio_device, and vice-versa. */
	portdev->vdev = vdev;
	vdev->priv = portdev;

	/* Find the queues. */
	err = virtio_find_vqs(portdev->vdev, 3, vqs,
			      io_callbacks,
			      io_names, NULL);
	if (err) {
		MTRACE("* virtio_find_vqs(): %d", err);
		goto error_free;
	}

	portdev->notify_vq = vqs[VIRTIO_TEST_QUEUE_NOTIFY];
	portdev->ctrl_vq   = vqs[VIRTIO_TEST_QUEUE_CTRL];
	portdev->rx_vq     = vqs[VIRTIO_TEST_QUEUE_RX];
	portdev->tx_vq     = vqs[VIRTIO_TEST_QUEUE_TX];

	err = notify_queue_init(portdev);
	if (err) {
		MTRACE("notify_queue_init()");
		goto error_free;
	}

	err = rx_queue_init(portdev);
	if (err) {
		MTRACE("rx_queue_init()");
		goto error_notify_queue_fini;
	}

	// MTRACE("virtio-lo-test-%p (should be 0x%lx)", portdev, (long unsigned int)portdev);
	portdev->wq = alloc_ordered_workqueue("virtio-lo-test-%u", 0, serial++);
	if (!portdev->wq) {
		MTRACE("alloc_ordered_workqueue()");
		goto error_rx_queue_fini;

	}

	INIT_WORK(&portdev->work, work_func);

	// spin_lock_init(&portdev->ports_lock);
	// INIT_LIST_HEAD(&portdev->ports);
	// INIT_LIST_HEAD(&portdev->list);

	virtio_device_ready(portdev->vdev);

	// INIT_WORK(&portdev->config_work, &config_work_handler);
	// INIT_WORK(&portdev->control_work, &control_work_handler);

	// if (multiport) {
	// 	spin_lock_init(&portdev->c_ivq_lock);
	// 	spin_lock_init(&portdev->c_ovq_lock);
	// 	err = fill_queue(portdev->c_ivq, &portdev->c_ivq_lock);
	// 	if (err < 0) {
	// 		dev_err(&vdev->dev,
	// 			"Error allocating buffers for control queue\n");
	// 		/*
	// 		 * The host might want to notify mgmt sw about device
	// 		 * add failure.
	// 		 */
	// 		__send_control_msg(portdev, 0xff,
	// 				   VIRTIO_TEST_DEVICE_READY, 0);
	// 		/* Device was functional: we need full cleanup. */
	// 		virtcons_remove(vdev);
	// 		return err;
	// 	}
	// } else {
	// 	/*
	// 	 * For backward compatibility: Create a console port
	// 	 * if we're running on older host.
	// 	 */
	// 	add_port(portdev, 0);
	// }

	// spin_lock_irq(&pdrvdata_lock);
	// list_add_tail(&portdev->list, &pdrvdata.portdevs);
	// spin_unlock_irq(&pdrvdata_lock);

	// __send_control_msg(portdev, 0xff,
	// 		   VIRTIO_TEST_DEVICE_READY, 1);

	MTRACE("ok");
	return 0;

// free_chrdev:
// 	// unregister_chrdev(portdev->chr_major, "virtio-portsdev");
error_rx_queue_fini:
	rx_queue_fini(portdev);
error_notify_queue_fini:
	notify_queue_fini(portdev);
error_free:
	kfree(portdev);
	vdev->priv = NULL;
	MTRACE("error: %d", err);
	return err;
}

#if 0
static int virtcons_freeze(struct virtio_device *vdev)
{
	struct ports_device *portdev;
	struct port *port;

	portdev = vdev->priv;

	virtio_reset_device(vdev);

	if (use_multiport(portdev))
		virtqueue_disable_cb(portdev->c_ivq);
	cancel_work_sync(&portdev->control_work);
	cancel_work_sync(&portdev->config_work);
	/*
	 * Once more: if control_work_handler() was running, it would
	 * enable the cb as the last step.
	 */
	if (use_multiport(portdev))
		virtqueue_disable_cb(portdev->c_ivq);

	list_for_each_entry(port, &portdev->ports, list) {
		virtqueue_disable_cb(port->in_vq);
		virtqueue_disable_cb(port->out_vq);
		/*
		 * We'll ask the host later if the new invocation has
		 * the port opened or closed.
		 */
		port->host_connected = false;
		remove_port_data(port);
	}
	remove_vqs(portdev);

	return 0;
}

static int virtcons_restore(struct virtio_device *vdev)
{
	struct ports_device *portdev;
	struct port *port;
	int ret;

	portdev = vdev->priv;

	ret = init_vqs(portdev);
	if (ret)
		return ret;

	virtio_device_ready(portdev->vdev);

	if (use_multiport(portdev))
		fill_queue(portdev->c_ivq, &portdev->c_ivq_lock);

	list_for_each_entry(port, &portdev->ports, list) {
		port->in_vq = portdev->in_vqs[port->id];
		port->out_vq = portdev->out_vqs[port->id];

		fill_queue(port->in_vq, &port->inbuf_lock);

		/* Get port open/close status on the host */
		send_control_msg(port, VIRTIO_TEST_PORT_READY, 1);

		/*
		 * If a port was open at the time of suspending, we
		 * have to let the host know that it's still open.
		 */
		if (port->guest_connected)
			send_control_msg(port, VIRTIO_TEST_PORT_OPEN, 1);
	}
	return 0;
}
#endif

/*
 * The file operations that we support: programs in the guest can open
 * a console device, read from it, write to it, poll for data and
 * close it.  The devices are at
 *   /dev/vport<device number>p<port number>
 */
static const struct file_operations port_fops = {
	.owner = THIS_MODULE,
	.open  = port_fops_open,
	.release = port_fops_release,
	.read  = port_fops_read,
	.write = port_fops_write,
	// .splice_write = port_fops_splice_write,
	// .poll  = port_fops_poll,
	// .fasync = port_fops_fasync,
	// .llseek = no_llseek,
};

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

	err = class_register(&port_class);
	if (err) {
		pr_err("Error %d registering class \"%s\"\n", err, port_class.name);
		return err;
	}

	err = register_chrdev(0, DEVICE_NAME, &port_fops);
	if (major < 0) {
		pr_err("Error %d registering chrdev\n", major);
		goto error_class_unregister;
	}

	major = err;

	// pdrvdata.debugfs_dir = debugfs_create_dir(DEVICE_NAME, NULL);
	// INIT_LIST_HEAD(&pdrvdata.consoles);
	// INIT_LIST_HEAD(&pdrvdata.portdevs);

	err = register_virtio_driver(&virtio_lo_test);
	if (err < 0) {
		pr_err("Error %d registering virtio driver\n", err);
		goto error_unregister_chrdev;
	}

	MTRACE("ok");

	return 0;

// error_debugfs_remove:
// 	debugfs_remove_recursive(pdrvdata.debugfs_dir);
error_unregister_chrdev:
	unregister_chrdev(major, DEVICE_NAME);
error_class_unregister:
	class_unregister(&port_class);

	MTRACE("error: %d", err);

	return err;
}

static void __exit virtio_lo_test_fini(void)
{
	MTRACE();
//	reclaim_dma_bufs();

	unregister_virtio_driver(&virtio_lo_test);

//	debugfs_remove_recursive(pdrvdata.debugfs_dir);
	unregister_chrdev(major, DEVICE_NAME);
	class_unregister(&port_class);
}
module_init(virtio_lo_test_init);
module_exit(virtio_lo_test_fini);

MODULE_DESCRIPTION("Virtio test driver");
MODULE_LICENSE("GPL");
