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

/* guest receive, i. e. we have to transmit */
static bool serv_has_to_receive(void)
{
	return false;
}

static int serv_receive(struct vlo_buf *req)
{
#define BUF_LEN 30
	char buf[BUF_LEN];
	int ret;

	static unsigned int n = 0;

	assert(serv_has_to_receive());

	if (req->io[0].iov_len < BUF_LEN) {
		trace_err("buffer is too short");
		ret = -1;
		goto done;
	}

	ret = snprintf(buf, BUF_LEN, "Hello world 0x%08x\n", n++);
	if (ret < 0) {
		trace_err("snprintf()");
		goto done;
	}

	memcpy(req->io[0].iov_base, buf, ret);

done:
	return ret;
}

static inline unsigned int umin(unsigned int a, unsigned int b)
{
	return a <= b ? a : b;
}

/* guest transmit, i. e. we receive */
static int serv_transmit(struct vlo_buf *req)
{
#define BUFX_LEN 10
	char bufx[BUFX_LEN];
	unsigned int i;

	for (i = 0; i < req->ion; i++) {
		int len = umin(req->io[i].iov_len, BUFX_LEN - 1);
		memcpy(bufx, req->io[i].iov_base, len);
		bufx[len] = 0;
		trace("%u: %lu \'%s\'", i, req->io[i].iov_len, bufx);
	}

	return 0;
}

static int receive1(struct vlo *vl)
{
	int ret = 0;

	// trace("*** RECEIVE");

	struct vlo_buf *req = vlo_buf_get(vl, VIRTIO_TEST_QUEUE_RX);
	if (!req) {
		trace_err("vlo_buf_get()");
		return -1;
	}

	trace("req: ion=%u, ion_trasmit=%u", req->ion, req->ion_transmit);

	if (req->ion_transmit != 0) {
		trace_err("not all buffers are writable");
		ret = -1;
		goto done;
	}

	ret = serv_receive(req);
	if (ret < 0)
		goto done;

done:
	vlo_buf_put(req, ret);

	return ret < 0 ? -1 : 0;
}

static inline int min(int a, int b)
{
	return a <= b ? a : b;
}

static int transmit1(struct vlo *vl)
{
	int ret = 0;

	// trace("*** TRANSMIT");

	struct vlo_buf *req = vlo_buf_get(vl, VIRTIO_TEST_QUEUE_TX);
	if (!req) {
		trace_err("vlo_buf_get()");
		return -1;
	}

	trace("req: ion=%u, ion_trasmit=%u", req->ion, req->ion_transmit);

	if (req->ion != req->ion_transmit) {
		trace_err("not all buffers are readable");
		ret = -1;
		goto done;
	}

	ret = serv_transmit(req);

done:
	vlo_buf_put(req, 0);

	return ret;
}

static int send_notification(struct vlo *vl)
{
	struct vlo_buf *req;
	struct virtio_test_notify *notify;
	int err;
	static unsigned int n;

	if (!vlo_buf_is_available(vl, VIRTIO_TEST_QUEUE_NOTIFY)) {
		trace_err("vlo_buf_is_available()");
		return -1;
	}

	req = vlo_buf_get(vl, VIRTIO_TEST_QUEUE_NOTIFY);
	if (!req) {
		trace_err("vlo_buf_get()");
		return -1;
	}

	trace("req: ion=%u, ion_trasmit=%u", req->ion, req->ion_transmit);

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
	notify->id = n++ % 2;

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

#endif

struct ctxt {
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

static int  config_test(void *ctxt) { (void)ctxt; return 0; }
static int  config_go(uint32_t events, void *ctxt) { (void)ctxt; (void)events; return 0; }
static void config_done(void *ctxt) { (void)ctxt; }

static int  rx_test(void *ctxt) { (void)ctxt; return 0; }
static int  rx_go(uint32_t events, void *ctxt) { (void)ctxt; (void)events; return 0; }
static void rx_done(void *ctxt) { (void)ctxt; }

static int  tx_test(void *ctxt) { (void)ctxt; return 0; }
static int  tx_go(uint32_t events, void *ctxt) { (void)ctxt; (void)events; return 0; }
static void tx_done(void *ctxt) { (void)ctxt; }

static int  notify_test(void *ctxt) { (void)ctxt; return 0; }
static int  notify_go(uint32_t events, void *ctxt) { (void)ctxt; (void)events; return 0; }
static void notify_done(void *ctxt) { (void)ctxt; }

static int  ctrl_test(void *ctxt) { (void)ctxt; return 0; }
static int  ctrl_go(uint32_t events, void *ctxt) { (void)ctxt; (void)events; return 0; }
static void ctrl_done(void *ctxt) { (void)ctxt; }

static int  timer_test(void *ctxt) { (void)ctxt; return 0; }
static int  timer_go(uint32_t events, void *ctxt) { (void)ctxt; (void)events; return 0; }
static void timer_done(void *ctxt) { (void)ctxt; }

static struct ctxt ctxt = {
	.config_thread = {
		.ctxt   = &ctxt,
		.events = EPOLLIN,
		.test   = config_test,
		.go     = config_go,
		.done   = config_done,
	},
	.rx_thread ={
		.ctxt   = &ctxt,
		.events = 0,
		.test   = rx_test,
		.go     = rx_go,
		.done   = rx_done,
	},
	.tx_thread = {
		.ctxt   = &ctxt,
		.events = EPOLLIN,
		.test   = tx_test,
		.go     = tx_go,
		.done   = tx_done,
	},
	.notify_thread = {
		.ctxt   = &ctxt,
		.events = 0,
		.test   = notify_test,
		.go     = notify_go,
		.done   = notify_done,
	},
	.ctrl_thread = {
		.ctxt   = &ctxt,
		.events = EPOLLIN,
		.test   = ctrl_test,
		.go     = ctrl_go,
		.done   = ctrl_done,
	},
	.timer_thread = {
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

	struct vlo *vl;
	int err;

	struct virtio_lo_qinfo qinfos[VIRTIO_TEST_QUEUE_MAX] = { { .size = 16, }, { .size = 16, }, { .size = 16, }, { .size = 16, } };

	struct virtio_test_config config = {
		.something = 3,
	};

	vl = vlo_init(VIRTIO_ID_TEST, 0x1af4, qinfos, VIRTIO_TEST_QUEUE_MAX, &config, sizeof(config));
	if (!vl) {
		trace_err("vlo_init()");
		goto error;
	}

	trace("sleep for 1 sec for driver to initialize...");
	sleep(1);
	trace("...done");

	ctxt.config_thread.fd = vlo_epoll_get_config(vl);
	ctxt.rx_thread.fd     = vlo_epoll_get_kick(vl, VIRTIO_TEST_QUEUE_RX    );
	ctxt.tx_thread.fd     = vlo_epoll_get_kick(vl, VIRTIO_TEST_QUEUE_TX    );
	ctxt.notify_thread.fd = vlo_epoll_get_kick(vl, VIRTIO_TEST_QUEUE_NOTIFY);
	ctxt.ctrl_thread.fd   = vlo_epoll_get_kick(vl, VIRTIO_TEST_QUEUE_CTRL  );

	err = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
	if (err < 0) {
		trace_err_p("timerfd_create()");
		goto error_virtio_done;
	}
	ctxt.timer_thread.fd = err;

	es_add(&ctxt.config_thread);
	es_add(&ctxt.rx_thread);
	es_add(&ctxt.tx_thread);
	es_add(&ctxt.notify_thread);
	es_add(&ctxt.ctrl_thread);
	es_add(&ctxt.timer_thread);

	err = es_schedule();
	if (err < 0) {
		trace_err("es_schedule()");
		goto error_close_timer_fd;
	}

	close(ctxt.timer_thread.fd);
	vlo_done(vl);

	exit(EXIT_SUCCESS);

error_close_timer_fd:
	close(ctxt.timer_thread.fd);
error_virtio_done:
	vlo_done(vl);
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
