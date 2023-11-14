#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/timerfd.h>
#include <assert.h>

#include "test.h"
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

struct ctxt {
	struct vlo *vl;

	/*
	 * Config
	 */
	struct es_thread config_thread;

	/*
	 * RX
	 */
	struct es_thread rx_thread;

	/*
	 * TX
	 */
	struct es_thread tx_thread;

	/*
	 * Notify
	 */
	struct es_thread notify_thread;

	/*
	 * Ctrl
	 */
	struct es_thread ctrl_thread;

	/*
	 * Timer
	 */
	struct es_thread timer_thread;

};

static struct ctxt ctxt;

/*
 * Config
 */
static int config_test(void *vctxt)
{
	(void)vctxt;
	return ES_WAIT;
}

static int config_go(uint32_t events, void *vctxt)
{
	(void)vctxt;
	(void)events;
	trace();
	return 0;
}

static void config_done(void *vctxt) { (void)vctxt; }

/*
 * RX
 */
static int rx_test(void *vctxt)
{
	(void)vctxt;

	int ret;

	if (serv_data_length() == 0) {
		ctxt.rx_thread.events = 0;
		return ES_WAIT;
	}

	ret = readfd_uint64(ctxt.rx_thread.fd);
	if (ret < 0) {
		trace_err("readfd_uint64()");
		return -1;
	}

	if (vlo_buf_is_available(ctxt.vl, VIRTIO_TEST_QUEUE_RX)) {
		// trace("buffer is available");
		return ES_READY;
	} else {
		ctxt.rx_thread.events = EPOLLIN;
		// trace("waiting for buffer");
		return ES_WAIT;
	}
}

static int rx_go(uint32_t events, void *vctxt)
{
	(void)vctxt;
	(void)events;

	int ret;

	// trace_err("events=0x%x", events);

	assert(vlo_buf_is_available(ctxt.vl, VIRTIO_TEST_QUEUE_RX));

	struct vlo_buf *req = vlo_buf_get(ctxt.vl, VIRTIO_TEST_QUEUE_RX);
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
	vlo_kick(ctxt.vl, VIRTIO_TEST_QUEUE_RX);

	// ret = ret < 0 ? -1 : 0;
	// trace_err("ret=%d", ret);
	// return ret;

	return ret < 0 ? -1 : 0;
}

static void rx_done(void *vctxt) { (void)vctxt; }

/*
 * TX
 */
static int tx_test(void *vctxt)
{
	(void)vctxt;
	int ret;

	ret = readfd_uint64(ctxt.tx_thread.fd);
	if (ret < 0) {
		trace_err("readfd_uint64()");
		return -1;
	}

	if (vlo_buf_is_available(ctxt.vl, VIRTIO_TEST_QUEUE_TX)) {
		// trace("buffer is available");
		ctxt.tx_thread.events = 0;
		return ES_READY;
	} else {
		ctxt.tx_thread.events = EPOLLIN;
		// trace("waiting for buffer");
		return ES_WAIT;
	}
}

static int tx_go(uint32_t events, void *vctxt)
{
	(void)vctxt;
	(void)events;

	int ret;

	// trace();

	static unsigned int bad = 0;

	if (!vlo_buf_is_available(ctxt.vl, VIRTIO_TEST_QUEUE_TX)) {
		bad++;
		return 0;
	}

	if (bad) {
		trace_err("vlo_buf_is_available(TX), %u", bad);
		bad = 0;
	}

	struct vlo_buf *req = vlo_buf_get(ctxt.vl, VIRTIO_TEST_QUEUE_TX);
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
	vlo_kick(ctxt.vl, VIRTIO_TEST_QUEUE_TX);

	return ret < 0 ? -1 : 0;
}

static void tx_done(void *vctxt) { (void)vctxt; }

/*
 * Notify
 */
static int notify_test(void *vctxt)
{
	(void)vctxt;
	/*
	 * FIXME: don't use this thread for now.
	 * In a full fledged app, there should be a queue
	 * where messages would wait till this thread discovers
	 * new buffers available from virtio.
	 * Now we just bail out if there is no virtio buffers available.
	 */
	return ES_EXIT;
}

static int notify_go(uint32_t events, void *vctxt)
{
	(void)vctxt;
	(void)events;
	trace();
	return 0;
}

static void notify_done(void *vctxt) { (void)vctxt; }

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
static int ctrl_test(void *vctxt)
{
	(void)vctxt;
	return ES_WAIT;
}

static int ctrl_go(uint32_t events, void *vctxt)
{
	(void)vctxt;
	(void)events;
	trace();
	return 0;
}

static void ctrl_done(void *vctxt) { (void)vctxt; }

/*
 * Timer
 */
static int timer_test(void *vctxt)
{
	(void)vctxt;
	return ES_WAIT;
}

static int timer_go(uint32_t events, void *vctxt)
{
	(void)vctxt;
	(void)events;

	static unsigned int n = 0;
	int ret;

	ret = readfd_uint64(ctxt.timer_thread.fd);
	if (ret < 0) {
		trace_err("readfd_uint64()");
		return -1;
	}

	if (n % 5 == 0) {
		ret = send_notification(ctxt.vl, n);
		if (ret < 0) {
			trace_err("send_notification(STOP)");
			return -1;
		}
	}

	n++;

	return 0;
}

static void timer_done(void *ctxt) { (void)ctxt; }

static struct ctxt ctxt = {
	.config_thread = {
		.name   = "config",
		.ctxt   = &ctxt,
		.events = EPOLLIN,
		.test   = config_test,
		.go     = config_go,
		.done   = config_done,
	},
	.rx_thread ={
		.name   = "rx",
		.ctxt   = &ctxt,
		.events = 0,
		.test   = rx_test,
		.go     = rx_go,
		.done   = rx_done,
	},
	.tx_thread = {
		.name   = "tx",
		.ctxt   = &ctxt,
		.events = EPOLLIN,
		.test   = tx_test,
		.go     = tx_go,
		.done   = tx_done,
	},
	.notify_thread = {
		.name   = "notify",
		.ctxt   = &ctxt,
		.events = 0,
		.test   = notify_test,
		.go     = notify_go,
		.done   = notify_done,
	},
	.ctrl_thread = {
		.name   = "ctrl",
		.ctxt   = &ctxt,
		.events = EPOLLIN,
		.test   = ctrl_test,
		.go     = ctrl_go,
		.done   = ctrl_done,
	},
	.timer_thread = {
		.name   = "timer",
		.ctxt   = &ctxt,
		.events = EPOLLIN,
		.test   = timer_test,
		.go     = timer_go,
		.done   = timer_done,
	},
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

	ctxt.vl = vlo_init(VIRTIO_ID_TEST, 0x1af4, qinfos, VIRTIO_TEST_QUEUE_MAX, &config, sizeof(config));
	if (!ctxt.vl) {
		trace_err("vlo_init()");
		goto error;
	}

	trace("sleep for 1 sec for driver to initialize...");
	sleep(1);
	trace("...done");

	ctxt.config_thread.fd = vlo_epoll_get_config(ctxt.vl);
	ctxt.rx_thread.fd     = vlo_epoll_get_kick(ctxt.vl, VIRTIO_TEST_QUEUE_RX    );
	ctxt.tx_thread.fd     = vlo_epoll_get_kick(ctxt.vl, VIRTIO_TEST_QUEUE_TX    );
	ctxt.notify_thread.fd = vlo_epoll_get_kick(ctxt.vl, VIRTIO_TEST_QUEUE_NOTIFY);
	ctxt.ctrl_thread.fd   = vlo_epoll_get_kick(ctxt.vl, VIRTIO_TEST_QUEUE_CTRL  );

	err = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (err < 0) {
		trace_err_p("timerfd_create()");
		goto error_virtio_done;
	}
	ctxt.timer_thread.fd = err;

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

	err = timerfd_settime(ctxt.timer_thread.fd, 0, &t, NULL);
	if (err < 0) {
		trace_err_p("timerfd_settime()");
		goto error_close_timer_fd;
	}

	struct es *es = es_init(
		&ctxt.config_thread,
		&ctxt.rx_thread,
		&ctxt.tx_thread,
		&ctxt.notify_thread,
		&ctxt.ctrl_thread,
		&ctxt.timer_thread,
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

	close(ctxt.timer_thread.fd);
	vlo_done(ctxt.vl);

	exit(EXIT_SUCCESS);

error_close_timer_fd:
	close(ctxt.timer_thread.fd);
error_virtio_done:
	vlo_done(ctxt.vl);
error:
	exit(EXIT_FAILURE);
}

#if 0
	while(1) {
#define MAX_EVENTS 10
		struct epoll_event events[MAX_EVENTS];
		int i;

		trace("BEGIN");

		// guest to host
		if (vlo_buf_is_available(vl, VIRTIO_TEST_QUEUE_TX)) {

			do {
				err = transmit1(vl);
				if (err) {
					trace_err("transmit1()");
					goto error_done;
				}
			} while (vlo_buf_is_available(vl, VIRTIO_TEST_QUEUE_TX));

			err = vlo_kick(vl, VIRTIO_TEST_QUEUE_TX);
			if (err) {
				trace_err("vlo_kick()");
				goto error_done;
			}
		}

		// host to guest
		if (serv_has_to_receive() && vlo_buf_is_available(vl, VIRTIO_TEST_QUEUE_RX)) {

			do {
				err = receive1(vl);
				if (err) {
					trace_err("receive1()");
					goto error_done;
				}
			} while (serv_has_to_receive() && vlo_buf_is_available(vl, VIRTIO_TEST_QUEUE_RX));

			err = vlo_kick(vl, VIRTIO_TEST_QUEUE_RX);
			if (err) {
				trace_err("vlo_kick()");
				goto error_done;
			}
		}

again:
		n = epoll_wait(efd, events, MAX_EVENTS, -1);
		if (n < 0) {
			trace_err_p("epoll_wait()");
			goto error_done;
		}
		else if (n == 0) {
			err = send_notification(vl);
			if (err) {
				trace_err("send_notification()");
				goto error_done;
			}
			goto again;
		}

		for (i = 0; i < n; i++) {
			trace("----------------------------------------------------");

			uint64_t ev;
			int ret;
			int fd = (events[i].data.u32 == 0xff) ?
				vlo_epoll_get_config(vl) :
				vlo_epoll_get_kick(vl, events[i].data.u32);

			ret = read(fd, &ev, sizeof(ev));
			if (ret >= 0 && ret != sizeof(ev)) {
				trace_err("read(): wrong size");
				goto error_done;
			} else if (ret < 0) {
				trace_err_p("read()");
				goto error_done;
			}

			if (events[i].data.u32 == 0xff) {
				err = vlo_config_get(vl, &config, sizeof(config));
				if (err) {
					trace_err("vlo_config_get()");
					goto error_done;
				}
				// trace("config: %lu cols=%hu, rows=%hu, nax_nr_ports=%u, emerg_wr=%u",
				// 	ev, config.cols, config.rows, config.max_nr_ports, config.emerg_wr);
			} else {
				trace("%s: %lu", q_name(events[i].data.u32), ev);
			}
		}
		trace("----------------------------------------------------");
	}
#endif
