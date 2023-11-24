#include <stdlib.h>
#include <unistd.h>
#include <sys/timerfd.h>
#include <sys/eventfd.h>

#include <linux/virtio_config.h>

#include "libvduse.h"
#include "epoll_scheduler.h"

#include "test.h"

#define TRACE_FILE "test_vduse.c"
#include "trace.h"

#define DRIVER_NAME "test"
#define QUEUE_SIZE 16

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

static int dev_test(void *vctxt)
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

	return 0;
}

static void dev_done(void *vctxt) {
	(void)vctxt;
}
/*
static int rx_test(void *vctxt)
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

static int tx_test(void *vctxt)
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

static int notify_test(void *vctxt)
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

static int ctrl_test(void *vctxt)
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
*/

static struct es_thread timer_thread;

static int timer_test(void *vctxt)
{
	(void)vctxt;

	trace();

	struct itimerspec t;
	int err = timerfd_gettime(timer_thread.fd, &t);
	if (err < 0) {
		trace_err_p("timerfd_gettime()");
		return err;
	}

	if (t.it_value.tv_nsec == 0 && t.it_value.tv_sec == 0)
		return ES_DONE;

	return ES_WAIT;
}

static int timer_go(uint32_t events, void *vctxt)
{
	(void)vctxt;
	(void)events;

	trace();

	return 0;
}

static void timer_done(void *ctxt) {
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
/*
static struct es_thread queue_threads[VIRTIO_TEST_QUEUE_MAX] = {
	[VIRTIO_TEST_QUEUE_RX] = {
		.name   = "queue_rx",
		.ctxt   = NULL,
		.events = EPOLLIN,
		.test   = rx_test,
		.go     = rx_go,
		.done   = rx_done,
	},
	[VIRTIO_TEST_QUEUE_TX] = {
		.name   = "queue_tx",
		.ctxt   = NULL,
		.events = EPOLLIN,
		.test   = tx_test,
		.go     = tx_go,
		.done   = tx_done,
	},
	[VIRTIO_TEST_QUEUE_NOTIFY] = {
		.name   = "queue_notify",
		.ctxt   = NULL,
		.events = EPOLLIN,
		.test   = notify_test,
		.go     = notify_go,
		.done   = notify_done,
	},
	[VIRTIO_TEST_QUEUE_CTRL] = {
		.name   = "queue_ctl",
		.ctxt   = NULL,
		.events = EPOLLIN,
		.test   = ctrl_test,
		.go     = ctrl_go,
		.done   = ctrl_done,
	},
};
*/
static struct es_thread timer_thread = {
	.name   = "timer",
	.ctxt   = NULL,
	.events = EPOLLIN,
	.test   = timer_test,
	.go     = timer_go,
	.done   = timer_done,
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

	err = vduse_set_reconnect_log_file(dev, "/tmp/vduse-" DRIVER_NAME ".log");
	if (err) {
		trace_err("vduse_set_reconnect_log_file()");
		goto error_dev_destroy;
	}

	for (int i = 0; i < VIRTIO_TEST_QUEUE_MAX; i++) {
		err = vduse_dev_setup_queue(dev, i, QUEUE_SIZE);
		if (err) {
			trace("vduse_dev_setup_queue(%d, %d)", i, QUEUE_SIZE);
			goto error_dev_destroy;
		}
	}
	dev_thread.fd = vduse_dev_get_fd(dev);
	trace("dev fd=%d", dev_thread.fd);

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
			.tv_sec = 0,
			.tv_nsec = 0,
		},
		.it_value = {
			.tv_sec = 20,
			.tv_nsec = 0,
		},
	};

	err = timerfd_settime(timer_thread.fd, 0, &t, NULL);
	if (err < 0) {
		trace_err_p("timerfd_settime()");
		goto error_close_timer_fd;
	}

	/* schedule */

	struct es *es = es_init(
		&dev_thread,

		// queue_threads + VIRTIO_TEST_QUEUE_RX,
		// queue_threads + VIRTIO_TEST_QUEUE_TX,
		// queue_threads + VIRTIO_TEST_QUEUE_NOTIFY,
		// queue_threads + VIRTIO_TEST_QUEUE_CTRL,

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
