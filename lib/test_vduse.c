#include <stdlib.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>
#include <stddef.h>

#include <linux/virtio_config.h>

#include "libvduse.h"
#include "epoll_scheduler.h"

#include <linux/virtio_test.h>

#define TRACE_FILE "test_vduse.c"
#include "trace.h"

#define DRIVER_NAME "test"
#define QUEUE_SIZE 16

struct queue {
	struct es_thread thread;
	bool enabled;
	VduseVirtq *vq;
};

#define container_of(ptr, type, member) ({                    \
	const typeof( ((type *)0)->member ) *__mptr = (ptr);  \
	(type *)( (char *)__mptr - offsetof(type,member) );})

static inline struct queue *thread_to_queue(struct es_thread *th)
{
	return container_of(th, struct queue, thread);
}

static struct es *es;
VduseDev *dev;

static enum es_test_result dev_test(struct es_thread *self)
{
	(void)self;

	trace();

	return ES_WAIT;
}

static int dev_go(struct es_thread *self, uint32_t events)
{
	(void)self;
	(void)events;

	trace();

	int err = vduse_dev_handler(dev);
	if (err < 0) {
		trace_err("vduse_dev_handler()");
		return err;
	}

	return 0;
}

static enum es_test_result rx_test(struct es_thread *self)
{
	struct queue *q = thread_to_queue(self);
	if (!q->enabled)
		return ES_EXIT_THREAD;

	trace();

	return ES_WAIT;
}

static int rx_go(struct es_thread *self, uint32_t events)
{
	(void)events;
	trace();

	struct queue *q = thread_to_queue(self);
	if (!q->enabled)
		return 0;

	eventfd_t ev;
	int ret = eventfd_read(self->fd, &ev);
	if (ret < 0) {
		trace_err("eventfd_read()");
		return -1;
	}

	return 0;
}

static enum es_test_result tx_test(struct es_thread *self)
{
	struct queue *q = thread_to_queue(self);
	if (!q->enabled)
		return ES_EXIT_THREAD;

	trace();

	return ES_WAIT;
}

static int tx_go(struct es_thread *self, uint32_t events)
{
	(void)events;
	trace();

	struct queue *q = thread_to_queue(self);
	if (!q->enabled)
		return 0;

	eventfd_t ev;
	int ret = eventfd_read(self->fd, &ev);
	if (ret < 0) {
		trace_err("eventfd_read()");
		return -1;
	}

	return 0;
}

static enum es_test_result notify_test(struct es_thread *self)
{
	struct queue *q = thread_to_queue(self);
	if (!q->enabled)
		return ES_EXIT_THREAD;

	trace();

	return ES_WAIT;
}

static int notify_go(struct es_thread *self, uint32_t events)
{
	(void)events;
	trace();

	struct queue *q = thread_to_queue(self);
	if (!q->enabled)
		return 0;

	eventfd_t ev;
	int ret = eventfd_read(self->fd, &ev);
	if (ret < 0) {
		trace_err("eventfd_read()");
		return -1;
	}

	return 0;
}

static enum es_test_result ctrl_test(struct es_thread *self)
{
	struct queue *q = thread_to_queue(self);
	if (!q->enabled)
		return ES_EXIT_THREAD;

	trace();

	return ES_WAIT;
}

static int ctrl_go(struct es_thread *self, uint32_t events)
{
	(void)events;
	trace();

	struct queue *q = thread_to_queue(self);
	if (!q->enabled)
		return 0;

	eventfd_t ev;
	int ret = eventfd_read(self->fd, &ev);
	if (ret < 0) {
		trace_err("eventfd_read()");
		return -1;
	}

	return 0;
}

static struct es_thread timer_thread;

static enum es_test_result timer_test(struct es_thread *self)
{
	(void)self;
	return ES_WAIT;
}
static struct es_thread dev_thread;
static int timer_go(struct es_thread *self, uint32_t events)
{
	(void)self;
	(void)events;

	static unsigned int n = 0;

	eventfd_t ev;
	int ret = eventfd_read(self->fd, &ev);
	if (ret < 0) {
		trace_err("eventfd_read()");
		return -1;
	}

	n++;
	trace("%d", n);

	return 0;
}

static struct es_thread dev_thread = {
	.name   = "dev",
	.events = EPOLLIN,
	.test   = dev_test,
	.go     = dev_go,
	.done   = NULL,
};

static struct queue queues[VIRTIO_TEST_QUEUE_MAX] = {
	[VIRTIO_TEST_QUEUE_RX] = {
		.thread = {
		.name   = "queue_rx",
		.events = EPOLLIN,
		.test   = rx_test,
		.go     = rx_go,
		.done   = NULL,
		},
	},
	[VIRTIO_TEST_QUEUE_TX] = {
		.thread = {
		.name   = "queue_tx",
		.events = EPOLLIN,
		.test   = tx_test,
		.go     = tx_go,
		.done   = NULL,
		},
	},
	[VIRTIO_TEST_QUEUE_NOTIFY] = {
		.thread = {
		.name   = "queue_notify",
		.events = EPOLLIN,
		.test   = notify_test,
		.go     = notify_go,
		.done   = NULL,
		},
	},
	[VIRTIO_TEST_QUEUE_CTRL] = {
		.thread = {
		.name   = "queue_ctl",
		.events = EPOLLIN,
		.test   = ctrl_test,
		.go     = ctrl_go,
		.done   = NULL,
		},
	},
};
static struct es_thread timer_thread = {
	.name   = "timer",
	.events = EPOLLIN,
	.test   = timer_test,
	.go     = timer_go,
	.done   = NULL,
};

static void test_enable_queue(VduseDev *dev, VduseVirtq *vq)
{
	(void)dev;
	(void)vq;

	int index = vduse_queue_get_index(vq);

	if (index < 0 || VIRTIO_TEST_QUEUE_MAX <= index) {
		trace_err("index == %d", index);
		return;
	}

	struct queue *q = queues + index;

	q->thread.fd = vduse_queue_get_fd(vq);
	q->vq = vq;
	q->enabled = true;

	trace("%s, fd=%d", q->thread.name, q->thread.fd);

	int err = es_add(es, &q->thread);
	if (err)
		trace_err("es_add(\"%s\")", q->thread.name);
}

static void test_disable_queue(VduseDev *dev, VduseVirtq *vq)
{
	(void)dev;

	int index = vduse_queue_get_index(vq);

	if (index < 0 || VIRTIO_TEST_QUEUE_MAX <= index) {
		trace_err("index == %d", index);
		return;
	}

	struct queue *q = queues + index;

	trace("%s", q->thread.name);

	q->enabled = false;
}

static const VduseOps ops = {
	.enable_queue  = test_enable_queue,
	.disable_queue = test_disable_queue,
};

int main(int argc, char **argv)
{
	int err;
	uint64_t driver_features = vduse_get_virtio_features();

	(void)argc;
	(void)argv;

	trace("hello");

	/* vduse */

	struct virtio_test_config dev_cfg = {
		.something = 0xdeadbeef12345678,
	};

	dev = vduse_dev_create(DRIVER_NAME, VIRTIO_ID_TEST, VIRTIO_TEST_VENDOR_ID,
					 driver_features, VIRTIO_TEST_QUEUE_MAX,
					 sizeof(struct virtio_test_config), (char *)&dev_cfg, &ops, NULL);
	if (!dev) {
		trace_err("vduse_dev_create()");
		goto error;
	}
	dev_thread.fd = vduse_dev_get_fd(dev);
	trace("dev fd=%d", dev_thread.fd);

	err = vduse_set_reconnect_log_file(dev, "/tmp/vduse-" DRIVER_NAME ".log");
	if (err) {
		trace_err("vduse_set_reconnect_log_file()");
		goto error_dev_destroy;
	}

	for (int i = 0; i < VIRTIO_TEST_QUEUE_MAX; i++) {
		err = vduse_dev_setup_queue(dev, i, QUEUE_SIZE);
		if (err) {
			trace_err("vduse_dev_setup_queue(%d, %d)", i, QUEUE_SIZE);
			goto error_dev_destroy;
		}
	}

	/* timer */

	err = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (err < 0) {
		trace_err_p("timerfd_create()");
		goto error_dev_destroy;
	}
	timer_thread.fd = err;

	struct itimerspec t = {
		.it_interval = {
			.tv_sec = 5,
			.tv_nsec = 0,
		},
		.it_value = {
			.tv_sec = 5,
			.tv_nsec = 0,
		},
	};

	err = timerfd_settime(timer_thread.fd, 0, &t, NULL);
	if (err < 0) {
		trace_err_p("timerfd_settime()");
		goto error_close_timer_fd;
	}

	/* schedule */

	es = es_init(
		&dev_thread,
		&timer_thread,
		NULL);
	if (!es) {
		trace_err("es_init()");
		goto error_close_timer_fd;
	}

	err = es_schedule(es);
	if (err < 0) {
		trace_err("es_schedule()");
		goto error_close_timer_fd;
	}

	trace("done");

	close(timer_thread.fd);
	vduse_dev_destroy(dev);

	trace("exit");
	exit(EXIT_SUCCESS);

error_close_timer_fd:
	close(timer_thread.fd);
error_dev_destroy:
	vduse_dev_destroy(dev);
error:
	trace_err("exit failure");
	exit(EXIT_FAILURE);
}
