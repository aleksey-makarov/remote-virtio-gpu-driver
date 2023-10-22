#include "epoll_scheduler.h"
#include "trace.h"

#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/epoll.h>
#include <assert.h>

#define DATA_INITIAL_CAPACITY 16

// FIXME: don't use static, allocate context for this library

static struct es_thread **data = NULL;
static unsigned int data_len = 0;
static unsigned int data_capacity = 0;

static int epoll_fd = -1;

static void error_all_done(void)
{
	unsigned int n;

	for (n = 0; n < data_len; n++)
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, data[n]->fd, (void *)1);
	close(epoll_fd);
	epoll_fd = -1;
	for (n = 0; n < data_len; n++) {
		if (!data[n]->done)
			continue;
		data[n]->done(data[n]->ctxt);
	}
	free(data);
	data = NULL;
	data_len = data_capacity = 0;
}

int es_add(struct es_thread *thread)
{
	assert(thread);
	assert(thread->fd >= 0);
	assert(thread->test);

	int ret;

	if (!data) {
		epoll_fd = epoll_create(1);
		if (epoll_fd < 0) {
			trace_err_p("epoll_create()");
			return -1;
		}
		data = calloc(DATA_INITIAL_CAPACITY, sizeof(struct es_data *));
		if (!data) {
			close(epoll_fd);
			epoll_fd = -1;
			trace_err("calloc() @1");
			return -1;
		}
		data_len = 0;
		data_capacity = DATA_INITIAL_CAPACITY;
	} else if (data_len == data_capacity) {
		data_capacity *= 2;
		struct es_thread **data1 = reallocarray(data, data_capacity, sizeof(struct es_data *));
		if (!data1) {
			trace_err("calloc() @2");
			error_all_done();
			return -1;
		}
		data = data1;
	}

	thread->private.events = 0;
	thread->private.data.u32 = data_len;

	data[data_len++] = thread;

	ret = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, thread->fd, &thread->private);
	if (ret) {
		trace_err_p("epoll_ctl(ADD)");
		error_all_done();
		return ret;
	}

	return 0;
}

int es_schedule(void)
{
	unsigned int n;
	int i;
	int ret = 0;

// FIXME: should be DATA_INITIAL_CAPACITY
#define MAX_EVENTS 10
	struct epoll_event events[MAX_EVENTS];

	while (data_len) {

		unsigned int deleted = 0;
		for (n = 0; n < data_len; n++) {
			struct es_thread *th = data[n];
			ret = th->test(th->ctxt);
			if (ret == ES_DONE || ret < 0) {
				goto done;
			} else if (ret == ES_EXIT) {
				ret = epoll_ctl(epoll_fd, EPOLL_CTL_DEL, th->fd, (void *)1);
				if (ret < 0) {
					trace_err_p("epoll_ctl(DEL)");
					goto done;
				}
				if (th->done)
					th->done(th->ctxt);
				data[n] = NULL;
				deleted++;
			} else {
				assert(ret == ES_WAIT);
				if (th->events != th->private.events || n != th->private.data.u32 ) {
					th->private.events = th->events;
					th->private.data.u32 = n;
					ret = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, th->fd, &th->private);
					if (ret < 0) {
						trace_err_p("epoll_ctl(MOD)");
						goto done;
					}
				}
			}
		}

		ret = epoll_wait(epoll_fd, events, MAX_EVENTS, -1);
		if (ret < 0) {
			trace_err_p("epoll_wait()");
			goto done;
		} else if (ret == 0 || ret >= MAX_EVENTS) {
			trace_err("epoll_wait() ???");
			ret = -1;
			goto done;
		}

		for (i = 0; i < ret; i++) {
			struct es_thread *th = data[events[i].data.u32];
			if (th->go)
				th->go(events[i].events, th->ctxt);
		}

		if (deleted) {
			unsigned int from, to;
			for (from = to = 0; from < data_len; from++) {
				if (!data[from])
					continue;
				if (from != to)
					data[to] = data[from];
				to++;
			}
			data_len = to;
			ret = 0;
			assert(deleted == from - to);
		}
	}

done:
	error_all_done();
	return ret;
}
