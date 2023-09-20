#include <stdlib.h>
#include <unistd.h>
#include <sys/epoll.h>

#include <linux/virtio_console.h>

#include "libvirtiolo.h"

#define TRACE_FILE "test_console_receive.c"
#include "trace.h"

int handle_tx(struct vlo *vl)
{
	unsigned int i;

	if (!vlo_buf_is_available(vl, 1)) {
		trace_err("no buf available (?)");
		return -1;
	}

	struct vlo_buf *req = vlo_buf_get(vl, 1);
	if (!req) {
		trace_err("vlo_buf_get()");
		return -1;
	}

	trace("req: allw=%s allr=%s ion=%u", req->allw ? "true" : "false", req->allr ? "true" : "false", req->ion);

	if (!req->allr) {
		trace_err("not all buffers are readable");
		return -1;
	}

	for (i = 0; i < req->ion; i++) {
		struct iovec *r = req->io + i;
		trace("read %lu chars", r->iov_len);
		long int err = write(1, r->iov_base, r->iov_len);
		if (err < 0) {
			trace_err_p("write(stdout)");
			break;
		}
		if ((long unsigned int)err != r->iov_len) {
			trace_err("write(stdout)");
			break;
		}
	}

	vlo_buf_put(req, 0);

	int err = vlo_kick(vl, 1);
	if (err) {
		trace_err("vlo_kick()");
		return -1;
	}

	return 0;
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
			.size = 64,
		},
		{
			.size = 64,
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

		int n = epoll_wait(efd, events, MAX_EVENTS, -1);
		if (n < 0) {
			trace_err_p("epoll_wait()");
			goto error_done;
		} else if (n == 0) {
			trace_err("epoll_wait() == 0 ???");
		}

		for (i = 0; i < n; i++) {
			trace("----------------------------------------------------");
			if (events[i].data.u32 == 0xff) {
				trace("config");
				err = vlo_config_get(vl, &config, sizeof(config));
				if (err) {
					trace_err("vlo_config_get()");
					goto error_done;
				}
				trace("cols=%hu, rows=%hu, nax_nr_ports=%u, emerg_wr=%u",
					config.cols, config.rows, config.max_nr_ports, config.emerg_wr);
			} else {
				uint64_t ev;
				trace("kick %u", events[i].data.u32);
				int ret = read(vlo_epoll_get_kick(vl, events[i].data.u32), &ev, sizeof(ev));
				if (ret > 0 && ret != sizeof(ev)) {
					trace_err("read()");
					goto error_done;
				} else if (ret < 0 && errno != EAGAIN) {
					trace_err_p("read()");
					goto error_done;
				}

				if (events[i].data.u32 == 1) {
					err = handle_tx(vl);
					if (err) {
						trace_err("handle_tx()");
						goto error_done;
					}
				}

			}
		}
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
