#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <assert.h>

#include <linux/virtio_test.h>

#include "libvirtiolo.h"
#include "epoll_scheduler.h"

#define TRACE_FILE "test.c"
#include "trace.h"

#if 0

static const char *q_name(int q)
{
	switch (q) {
	case VIRTIO_TEST_QUEUE_RX:     return "RX";
	case VIRTIO_TEST_QUEUE_TX:     return "TX";
	case VIRTIO_TEST_QUEUE_NOTIFY: return "NOTIFY";
	case VIRTIO_TEST_QUEUE_CTRL:   return "CTRL";
	default: return "???";
	}
}

#endif

#define K 1024
#define M (K * K)
#define BUF_LEN (64 * M)

static char buf[BUF_LEN];
static unsigned long int buf_data;  // first occupied by data
static unsigned long int buf_empty; // first empty: always buf_data <= buf_empty

static void serv_reset(void)
{
	buf_data = buf_empty = 0;
}

static unsigned long int serv_data_length(void)
{
	return buf_empty - buf_data;
}

static unsigned long int serv_free_space(void)
{
	return BUF_LEN - serv_data_length();
}

static unsigned int min_ui(unsigned int a, unsigned int b)
{
	return a < b ? a : b;
}

static int serv_put(char *data, unsigned int length)
{
	if (serv_free_space() < length) {
		trace_err("free=%lu, length=%u", serv_free_space(), length);
		return -1;
	}

	unsigned int buf_empty_index = buf_empty % BUF_LEN;
	unsigned int length1 = min_ui(length, BUF_LEN - buf_empty_index);

	memcpy(buf + buf_empty_index, data, length1);

	if (length1 != length)
		memcpy(buf, data + length1, length - length1);

	buf_empty += length;

	return 0;
}

static int serv_put_buf(struct vlo_buf *req)
{
	unsigned int i;
	int err;

	for (i = 0; i < req->ion; i++) {
		err = serv_put(req->io[i].iov_base, req->io[i].iov_len);
		if (err) {
			trace_err("error @%u", i);
			return err;
		}
	}

	return 0;
}

static void serv_get(char *data, unsigned int length)
{
	assert(length <= serv_data_length());

	unsigned int buf_data_index = buf_data % BUF_LEN;
	unsigned int length1 = min_ui(length, BUF_LEN - buf_data_index);

	memcpy(data, buf + buf_data_index, length1);

	if (length1 != length)
		memcpy(data + length1, buf, length - length1);

	buf_data += length;
}

static int serv_get_buf(struct vlo_buf *req)
{
	int ret = 0;
	unsigned int i;

	unsigned int available = serv_data_length();

	assert(available > 0);

	for (i = 0; i < req->ion && available; i++) {
		unsigned int length1 = min_ui(available, req->io[i].iov_len);
		serv_get(req->io[i].iov_base, length1);
		ret += length1;
		available -= length1;
	}

	return ret;
}

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

static struct vlo *vl;
static struct es_thread config_thread;
static struct es_thread rx_thread;
static struct es_thread tx_thread;
static struct es_thread notify_thread;
static struct es_thread ctrl_thread;
static struct es_thread timer_thread;

/*
 * Config
 */
static enum es_test_result config_test(struct es_thread *self)
{
	(void)self;
	return ES_WAIT;
}

static int config_go(struct es_thread *self, uint32_t events)
{
	(void)self;
	(void)events;
	trace();
	return 0;
}

/*
 * RX
 */
static enum es_test_result rx_test(struct es_thread *self)
{
	(void)self;

	int ret;

	if (serv_data_length() == 0) {
		rx_thread.events = 0;
		return ES_WAIT;
	}

	ret = readfd_uint64(rx_thread.fd);
	if (ret < 0) {
		trace_err("readfd_uint64()");
		return -1;
	}

	if (vlo_buf_is_available(vl, VIRTIO_TEST_QUEUE_RX)) {
		rx_thread.events = 0;
		// trace("buffer is available");
		return ES_READY;
	} else {
		rx_thread.events = EPOLLIN;
		// trace("waiting for buffer");
		return ES_WAIT;
	}
}

static int rx_go(struct es_thread *self, uint32_t events)
{
	(void)self;
	(void)events;

	int ret;

	// trace_err("events=0x%x", events);

	// assert(vlo_buf_is_available(ctxt.vl, VIRTIO_TEST_QUEUE_RX));

	static unsigned int bad0 = 0;
	static unsigned int bad1 = 0;

	if (!vlo_buf_is_available(vl, VIRTIO_TEST_QUEUE_RX)) {
		if (!rx_thread.events)
			bad0++;
		else
			bad1++;
		return 0;
	}

	if (bad0 || bad1) {
		trace_err("vlo_buf_is_available(RX), bad0=%u, bad1=%u", bad0, bad1);
		bad0 = bad1 = 0;
	}

	struct vlo_buf *req = vlo_buf_get(vl, VIRTIO_TEST_QUEUE_RX);
	if (!req) {
		trace_err("vlo_buf_get()");
		return -1;
	}

	// trace("req: ion=%u, ion_trasmit=%u", req->ion, req->ion_transmit);

	if (req->ion_transmit != 0) {
		trace_err("not all buffers are readable");
		ret = -1;
		goto done;
	}

	ret = serv_get_buf(req);
	if (ret < 0)
		trace_err("serv_put_buf()");

done:
	vlo_buf_put(req, ret);
	vlo_kick(vl, VIRTIO_TEST_QUEUE_RX);

	// ret = ret < 0 ? -1 : 0;
	// trace_err("ret=%d", ret);
	// return ret;

	return ret < 0 ? -1 : 0;
}

/*
 * TX
 */
static enum es_test_result tx_test(struct es_thread *self)
{
	(void)self;
	int ret;

	ret = readfd_uint64(tx_thread.fd);
	if (ret < 0) {
		trace_err("readfd_uint64()");
		return -1;
	}

	if (vlo_buf_is_available(vl, VIRTIO_TEST_QUEUE_TX)) {
		// trace("buffer is available");
		tx_thread.events = 0;
		return ES_READY;
	} else {
		tx_thread.events = EPOLLIN;
		// trace("waiting for buffer");
		return ES_WAIT;
	}
}

static int tx_go(struct es_thread *self, uint32_t events)
{
	(void)self;
	(void)events;

	int ret;

	// trace();

	static unsigned int bad0 = 0;
	static unsigned int bad1 = 0;

	if (!vlo_buf_is_available(vl, VIRTIO_TEST_QUEUE_TX)) {
		if (!tx_thread.events)
			bad0++;
		else
			bad1++;

		return 0;
	}

	if (bad0 || bad1) {
		trace_err("vlo_buf_is_available(TX), bad0=%u, bad1=%u", bad0, bad1);
		bad0 = bad1 = 0;
	}

	struct vlo_buf *req = vlo_buf_get(vl, VIRTIO_TEST_QUEUE_TX);
	if (!req) {
		trace_err("vlo_buf_get()");
		return -1;
	}

	// trace("req: ion=%u, ion_trasmit=%u", req->ion, req->ion_transmit);

	if (req->ion_transmit != req->ion) {
		trace_err("not all buffers are writable");
		ret = -1;
		goto done;
	}

	ret = serv_put_buf(req);
	if (ret < 0)
		trace_err("serv_put_buf()");

done:
	vlo_buf_put(req, 0);
	vlo_kick(vl, VIRTIO_TEST_QUEUE_TX);

	return ret < 0 ? -1 : 0;
}

/*
 * Notify
 */
static enum es_test_result notify_test(struct es_thread *self)
{
	(void)self;
	/*
	 * FIXME: don't use this thread for now.
	 * In a full fledged app, there should be a queue
	 * where messages would wait till this thread discovers
	 * new buffers available from virtio.
	 * Now we just bail out if there is no virtio buffers available.
	 */
	return ES_EXIT_THREAD;
}

static int notify_go(struct es_thread *self, uint32_t events)
{
	(void)self;
	(void)events;
	trace();
	return 0;
}

static int send_notification(struct vlo *vl, unsigned int id)
{
	struct vlo_buf *req;
	struct virtio_test_notify *notify;
	int err;

	if (!vlo_buf_is_available(vl, VIRTIO_TEST_QUEUE_NOTIFY)) {
		trace_err("vlo_buf_is_available()");
		return -1;
	}

	req = vlo_buf_get(vl, VIRTIO_TEST_QUEUE_NOTIFY);
	if (!req) {
		trace_err("vlo_buf_get()");
		return -1;
	}

	// trace("req: ion=%u, ion_trasmit=%u", req->ion, req->ion_transmit);

	if (req->ion_transmit > 0) {
		trace_err("not all buffers are writable");
		err = -1;
		goto done;
	}

	if (req->io[0].iov_len < sizeof(struct virtio_test_notify)) {
		trace_err("first buffer is too small");
		err = -1;
		goto done;
	}

	notify = req->io[0].iov_base;
	notify->id = id;

	vlo_buf_put(req, 0);

	err = vlo_kick(vl, VIRTIO_TEST_QUEUE_NOTIFY);
	if (err)
		trace_err("vlo_kick()");

	return err;

done:
	vlo_buf_put(req, 0);
	vlo_kick(vl, VIRTIO_TEST_QUEUE_NOTIFY);
	return err;
}

/*
 * Ctrl
 */
static enum es_test_result ctrl_test(struct es_thread *self)
{
	(void)self;
	return ES_WAIT;
}

static int ctrl_go(struct es_thread *self, uint32_t events)
{
	(void)self;
	(void)events;
	trace();
	return 0;
}

/*
 * Timer
 */
static enum es_test_result timer_test(struct es_thread *self)
{
	(void)self;
	return ES_WAIT;
}

static int timer_go(struct es_thread *self, uint32_t events)
{
	(void)self;
	(void)events;

	static unsigned int n = 0;
	int ret;

	ret = readfd_uint64(timer_thread.fd);
	if (ret < 0) {
		trace_err("readfd_uint64()");
		return -1;
	}

	if (n % 5 == 0) {
		ret = send_notification(vl, n);
		if (ret < 0) {
			trace_err("send_notification(STOP)");
			return -1;
		}
	}

	n++;

	return 0;
}

static struct es_thread config_thread = {
	.name   = "config",
	.events = EPOLLIN,
	.test   = config_test,
	.go     = config_go,
	.done   = NULL,
};
static struct es_thread rx_thread ={
	.name   = "rx",
	.events = 0,
	.test   = rx_test,
	.go     = rx_go,
	.done   = NULL,
};
static struct es_thread tx_thread = {
	.name   = "tx",
	.events = EPOLLIN,
	.test   = tx_test,
	.go     = tx_go,
	.done   = NULL,
};
static struct es_thread notify_thread = {
	.name   = "notify",
	.events = 0,
	.test   = notify_test,
	.go     = notify_go,
	.done   = NULL,
};
static struct es_thread ctrl_thread = {
	.name   = "ctrl",
	.events = EPOLLIN,
	.test   = ctrl_test,
	.go     = ctrl_go,
	.done   = NULL,
};
static struct es_thread timer_thread = {
	.name   = "timer",
	.events = EPOLLIN,
	.test   = timer_test,
	.go     = timer_go,
	.done   = NULL,
};

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	int err;

	struct virtio_lo_qinfo qinfos[VIRTIO_TEST_QUEUE_MAX] = { { .size = 16, }, { .size = 16, }, { .size = 16, }, { .size = 16, } };

	struct virtio_test_config config = {
		.something = 3,
	};

	serv_reset();

	vl = vlo_init(VIRTIO_ID_TEST, VIRTIO_TEST_VENDOR_ID, qinfos, VIRTIO_TEST_QUEUE_MAX, &config, sizeof(config));
	if (!vl) {
		trace_err("vlo_init()");
		goto error;
	}

	trace("sleep for 1 sec for driver to initialize...");
	sleep(1);
	trace("...done");

	config_thread.fd = vlo_epoll_get_config(vl);
	rx_thread.fd     = vlo_epoll_get_kick(vl, VIRTIO_TEST_QUEUE_RX    );
	tx_thread.fd     = vlo_epoll_get_kick(vl, VIRTIO_TEST_QUEUE_TX    );
	notify_thread.fd = vlo_epoll_get_kick(vl, VIRTIO_TEST_QUEUE_NOTIFY);
	ctrl_thread.fd   = vlo_epoll_get_kick(vl, VIRTIO_TEST_QUEUE_CTRL  );

	err = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (err < 0) {
		trace_err_p("timerfd_create()");
		goto error_virtio_done;
	}
	timer_thread.fd = err;

	struct itimerspec t = {
		.it_interval = {
			.tv_sec = 1,
			.tv_nsec = 0,
		},
		.it_value = {
			.tv_sec = 1,
			.tv_nsec = 0,
		},
	};

	err = timerfd_settime(timer_thread.fd, 0, &t, NULL);
	if (err < 0) {
		trace_err_p("timerfd_settime()");
		goto error_close_timer_fd;
	}

	struct es *es = es_init(
		&config_thread,
		&rx_thread,
		&tx_thread,
		&notify_thread,
		&ctrl_thread,
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

	close(timer_thread.fd);
	vlo_done(vl);

	exit(EXIT_SUCCESS);

error_close_timer_fd:
	close(timer_thread.fd);
error_virtio_done:
	vlo_done(vl);
error:
	exit(EXIT_FAILURE);
}
