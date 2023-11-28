#include <stdlib.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>

#include <linux/virtio_config.h>

#include "libvduse.h"
#include "epoll_scheduler.h"

#include <linux/virtio_test.h>

#define TRACE_FILE "test_vduse.c"
#include "trace.h"

#define DRIVER_NAME "test"
#define QUEUE_SIZE 16

static struct es *es;

static int readfd_uint64(int fd)
{
	uint64_t ev;
	int ret;

	ret = read(fd, &ev, sizeof(ev));
	if (ret >= 0 && ret != sizeof(ev)) {
		trace_err("read(): wrong size");
		return -1;
	} else if (ret < 0) {
		if (errno == EAGAIN)
			return 0;
		trace_err_p("read() (errno=%d)", errno);
		return -1;
	}

	return ev;
}

static enum es_test_result dev_test(void *vctxt)
{
	(void)vctxt;

	trace();

	return ES_WAIT;
}

static int dev_go(uint32_t events, void *vctxt)
{
	(void)vctxt;
	(void)events;

	trace();

	int err = vduse_dev_handler(vctxt);
	if (err < 0) {
		trace_err("vduse_dev_handler()");

	}

	return 0;
}

static void dev_done(void *vctxt) {
	(void)vctxt;
}

static enum es_test_result rx_test(void *vctxt)
{
	(void)vctxt;

	trace();

	return ES_WAIT;
}

static int rx_go(uint32_t events, void *vctxt)
{
	(void)vctxt;
	(void)events;

	trace();

	return 0;
}

static void rx_done(void *vctxt) {
	(void)vctxt;
}

static enum es_test_result tx_test(void *vctxt)
{
	(void)vctxt;

	trace();

	return ES_WAIT;
}

static int tx_go(uint32_t events, void *vctxt)
{
	(void)vctxt;
	(void)events;

	trace();

	return 0;
}

static void tx_done(void *vctxt) {
	(void)vctxt;
}

static enum es_test_result notify_test(void *vctxt)
{
	(void)vctxt;

	trace();

	return ES_WAIT;
}

static int notify_go(uint32_t events, void *vctxt)
{
	(void)vctxt;
	(void)events;

	trace();

	return 0;
}

static void notify_done(void *vctxt) {
	(void)vctxt;
}

static enum es_test_result ctrl_test(void *vctxt)
{
	(void)vctxt;

	trace();

	return ES_WAIT;
}

static int ctrl_go(uint32_t events, void *vctxt)
{
	(void)vctxt;
	(void)events;

	trace();

	return 0;
}

static void ctrl_done(void *vctxt) {
	(void)vctxt;
}

static struct es_thread timer_thread;

static enum es_test_result timer_test(void *vctxt)
{
	(void)vctxt;
	return ES_WAIT;
}
static struct es_thread dev_thread;
static int timer_go(uint32_t events, void *vctxt)
{
	(void)vctxt;
	(void)events;

	static unsigned int n = 0;
	int ret;

	ret = readfd_uint64(timer_thread.fd);
	if (ret < 0) {
		trace_err("readfd_uint64()");
		return -1;
	}

	n++;
	trace("%d", n);

	return 0;
}

static void timer_done(void *ctxt)
{
	(void)ctxt;
}

static struct es_thread dev_thread = {
	.name   = "dev",
	.ctxt   = NULL,
	.events = EPOLLIN,
	.test   = dev_test,
	.go     = dev_go,
	.done   = dev_done,
};

struct queue {
	struct es_thread thread;
	bool enabled;
	int fd;
	VduseVirtq *vq;
};

static struct queue queues[VIRTIO_TEST_QUEUE_MAX] = {
	[VIRTIO_TEST_QUEUE_RX] = {
		.thread = {
		.name   = "queue_rx",
		.ctxt   = NULL,
		.events = EPOLLIN,
		.test   = rx_test,
		.go     = rx_go,
		.done   = rx_done,
		},
	},
	[VIRTIO_TEST_QUEUE_TX] = {
		.thread = {
		.name   = "queue_tx",
		.ctxt   = NULL,
		.events = EPOLLIN,
		.test   = tx_test,
		.go     = tx_go,
		.done   = tx_done,
		},
	},
	[VIRTIO_TEST_QUEUE_NOTIFY] = {
		.thread = {
		.name   = "queue_notify",
		.ctxt   = NULL,
		.events = EPOLLIN,
		.test   = notify_test,
		.go     = notify_go,
		.done   = notify_done,
		},
	},
	[VIRTIO_TEST_QUEUE_CTRL] = {
		.thread = {
		.name   = "queue_ctl",
		.ctxt   = NULL,
		.events = EPOLLIN,
		.test   = ctrl_test,
		.go     = ctrl_go,
		.done   = ctrl_done,
		},
	},
};
static struct es_thread timer_thread = {
	.name   = "timer",
	.ctxt   = NULL,
	.events = EPOLLIN,
	.test   = timer_test,
	.go     = timer_go,
	.done   = timer_done,
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

	trace("%s", q->thread.name);

	q->fd = vduse_queue_get_fd(vq);
	q->vq = vq;
	q->enabled = true;

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

	VduseDev *dev = vduse_dev_create(DRIVER_NAME, VIRTIO_ID_TEST, VIRTIO_TEST_VENDOR_ID,
					 driver_features, VIRTIO_TEST_QUEUE_MAX,
					 sizeof(struct virtio_test_config), (char *)&dev_cfg, &ops, NULL);
	if (!dev) {
		trace_err("vduse_dev_create()");
		goto error;
	}
	dev_thread.ctxt = dev;
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

	for (int i = 0; i < VIRTIO_TEST_QUEUE_MAX; i++) {
		VduseVirtq *vq = vduse_dev_get_queue(dev, i); \
		if (!vq) {
			trace_err("vduse_dev_get_queue(%d)", i);
			goto error_dev_destroy;
		}
	}

#define F(i) do { \
		VduseVirtq *vq = vduse_dev_get_queue(dev, i); \
		int fd = vduse_queue_get_fd(vq); \
		trace("vq=%p, q=%d, fd=%d", vq, i, fd); \
		queue_threads[i].fd = fd; \
		eventfd_write(fd, 1); \
	} while (0);

	// F(VIRTIO_TEST_QUEUE_RX);
	// F(VIRTIO_TEST_QUEUE_TX);
	// F(VIRTIO_TEST_QUEUE_NOTIFY);
	// F(VIRTIO_TEST_QUEUE_CTRL);

#undef F

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
