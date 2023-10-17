#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <assert.h>

#include "test.h"
#include "libvirtiolo.h"

#define TRACE_FILE "test.c"
#include "trace.h"

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

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	struct vlo *vl;
	int err;
	int n;

	struct virtio_lo_qinfo qinfos[VIRTIO_TEST_QUEUE_MAX] = {
		{
			.size = 16,
		},
		{
			.size = 16,
		},
		{
			.size = 16,
		},
		{
			.size = 16,
		},
	};

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

	int efd = epoll_create(1);
	if (efd == -1) {
		trace_err_p("epoll_create()");
		goto error_virtio_done;
	}

	{
		struct epoll_event eeconfig = {
			.events = EPOLLIN,
			.data.u32 = 0xff,
		};
		err = epoll_ctl(efd, EPOLL_CTL_ADD, vlo_epoll_get_config(vl), &eeconfig);
		if (err) {
			trace_err_p("epoll_ctl(config)");
			goto error_epoll_free;
		}

		unsigned int i;
		for (i = 0; i < VIRTIO_TEST_QUEUE_MAX; i++) {
			struct epoll_event eekick = {
				.events = EPOLLIN,
				.data.u32 = i,
			};
			err = epoll_ctl(efd, EPOLL_CTL_ADD, vlo_epoll_get_kick(vl, i), &eekick);
			if (err) {
				trace_err_p("epoll_ctl(kick)");
				goto error_epoll_free;
			}
		}
	}

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
		n = epoll_wait(efd, events, MAX_EVENTS, 3000);
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

	close(efd);
	vlo_done(vl);

	exit(EXIT_SUCCESS);

error_done:
error_epoll_free:
	close(efd);
error_virtio_done:
	vlo_done(vl);
error:
	exit(EXIT_FAILURE);
}
