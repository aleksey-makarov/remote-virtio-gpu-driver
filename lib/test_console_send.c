#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>

#include <linux/virtio_console.h>

#include "libvirtiolo.h"

#define TRACE_FILE "test_console_send.c"
#include "trace.h"

static const unsigned int RECEIVEQ = 0; // from device to driver
static const unsigned int TRANSMITQ = 1; // from driver to device

static const char *q_name(int q)
{
	switch (q) {
	case 0: return "RECEIVE";
	case 1: return "TRANSMIT";
	default: return "???";
	}
}

static int receive1(struct vlo *vl)
{
	unsigned int len = 0;
	int ret = 0;

#define BUF_LEN 30
	char buf[BUF_LEN];

	static unsigned int n = 0;

	trace("*** RECEIVE 0x%08x", n);

	struct vlo_buf *req = vlo_buf_get(vl, RECEIVEQ);
	if (!req) {
		trace_err("vlo_buf_get()");
		return -1;
	}

	// trace("req: allw=%s allr=%s ion=%u", req->allw ? "true" : "false", req->allr ? "true" : "false", req->ion);

	if (!req->allw) {
		trace_err("not all buffers are writable");
		ret = -1;
		goto done;
	}

	if (req->io[0].iov_len < BUF_LEN) {
		trace_err("buffer is too short");
		ret = -1;
		goto done;
	}

	int ilen = snprintf(buf, BUF_LEN, "Hello world 0x%08x\n", n++);
	if (ilen < 0) {
		ret = -1;
		trace_err("snprintf()");
		goto done;
	}

	memcpy(req->io[0].iov_base, buf, len);
	len = ilen;

done:
	vlo_buf_put(req, len);

	return ret;
}

static inline int min(int a, int b)
{
	return a <= b ? a : b;
}

static int transmit1(struct vlo *vl)
{
	int ret = 0;
	unsigned int i;

	trace("*** TRANSMIT");

	struct vlo_buf *req = vlo_buf_get(vl, TRANSMITQ);
	if (!req) {
		trace_err("vlo_buf_get()");
		return -1;
	}

	// trace("req: allw=%s allr=%s ion=%u", req->allw ? "true" : "false", req->allr ? "true" : "false", req->ion);

	if (!req->allr) {
		trace_err("not all buffers are readable");
		ret = -1;
		goto done;
	}

	for (i = 0; i < req->ion; i++) {
#define BUFX_LEN 10
		char bufx[BUFX_LEN];
		int len = min(req->io[i].iov_len, BUFX_LEN - 1);
		memcpy(bufx, req->io[i].iov_base, len);
		bufx[len] = 0;
		trace("%u: %lu \'%s\'", i, req->io[i].iov_len, bufx);
	}

done:
	vlo_buf_put(req, 0);

	return ret;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	struct vlo *vl;
	int err;

#define NQUEUES 2
	struct virtio_lo_qinfo qinfos[NQUEUES] = {
		{
			.size = 16,
		},
		{
			.size = 16,
		},
	};

	struct virtio_console_config config = {
		.cols = 0,
		.rows = 0,
		.max_nr_ports = 0,
		.emerg_wr = 0,
	};

	vl = vlo_init(VIRTIO_ID_CONSOLE, 0x1af4, qinfos, NQUEUES, &config, sizeof(config));
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
		for (i = 0; i < 2; i++) {
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

// 		int receive_n = 0;
// 		const int receive_n_max = 20;
// #define HAVE_RECEIVE (receive_n < receive_n_max)
		bool wait = true;

		trace("BEGIN");

		// driver to device
		if (vlo_buf_is_available(vl, TRANSMITQ)) {

			do {
				err = transmit1(vl);
				if (err) {
					trace_err("transmit1()");
					goto error_done;
				}
			} while (vlo_buf_is_available(vl, TRANSMITQ));

			err = vlo_kick(vl, TRANSMITQ);
			if (err) {
				trace_err("vlo_kick()");
				goto error_done;
			}
		}

		// device to driver
		if (vlo_buf_is_available(vl, RECEIVEQ)) {

			int i = 0;
			do {
				err = receive1(vl);
				if (err) {
					trace_err("receive1()");
					goto error_done;
				}
				// receive_n++;
				i++;
			} while (i < 10 && vlo_buf_is_available(vl, RECEIVEQ));

			err = vlo_kick(vl, RECEIVEQ);
			if (err) {
				trace_err("vlo_kick()");
				goto error_done;
			}
		}

		if (wait)
			trace("wait...");
		int n = epoll_wait(efd, events, MAX_EVENTS, wait ? -1 : 0);
		if (wait)
			trace("...done");
		if (n < 0) {
			trace_err_p("epoll_wait()");
			goto error_done;
		}
		else if (wait && n == 0) {
			trace_err("epoll_wait() == 0 ???");
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
				trace("config: %lu cols=%hu, rows=%hu, nax_nr_ports=%u, emerg_wr=%u",
					ev, config.cols, config.rows, config.max_nr_ports, config.emerg_wr);
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
