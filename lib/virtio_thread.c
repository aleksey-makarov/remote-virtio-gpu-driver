#include "virtio_thread.h"

#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/eventfd.h>

#include <linux/virtio_gpu.h>
#include <linux/virtio_ids.h>
#include <linux/virtio_config.h>

#include <libvirtiolo.h>

#include "epoll_scheduler.h"
#include "error.h"
#include "gettid.h"
#include "container_of.h"

static pthread_t thread;
static volatile bool done = false;
static pthread_mutex_t req_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static STAILQ_HEAD(, virtio_thread_request) req_queue = STAILQ_HEAD_INITIALIZER(req_queue);

static struct vlo *vlo;
static struct es *es;

static struct virtio_gpu_config config = {
	.events_read = 0,
	.events_clear = 0,
	.num_scanouts = 1,
	.num_capsets = 1,
};

struct queue_thread {
	struct es_thread thread;
	unsigned int queue_index;
};

static inline struct queue_thread *thread_to_queue_thread(struct es_thread *th)
{
	return container_of(th, struct queue_thread, thread);
}

static struct es_thread notify_thread;

// called from the main thread
void virtio_thread_request_done(struct virtio_thread_request *req)
{
	assert(req);
	trace("(4) %d on %d", req->serial, gettid());

	pthread_mutex_lock(&req_queue_mutex);
	STAILQ_INSERT_TAIL(&req_queue, req, queue_entry);
	pthread_mutex_unlock(&req_queue_mutex);

	int err = eventfd_write(notify_thread.fd, 1);
	if (err)
		error_errno("eventfd_write()");
}

static enum es_test_result test_wait(struct es_thread *self)
{
	(void)self;
	return ES_WAIT;
}

static enum es_test_result notify_test(struct es_thread *self)
{
	(void)self;

	if (done)
		return ES_DONE;
	else
		return ES_WAIT;
}

static int config_go(struct es_thread *self, uint32_t events)
{
	(void)self;
	(void)events;

	struct virtio_gpu_config c;
	eventfd_t ev;
	int err;

	err = eventfd_read(self->fd, &ev);
	// FIXME: EAGAIN?
	if (err < 0) {
		error("eventfd_read()");
		// FIXME
		goto out;
	}

	err = vlo_config_get(vlo, &c, sizeof(c));
	if (err < 0) {
		error("vlo_config_get()");
		// FIXME
		goto out;
	}

	trace("config: events_read=0x%08x, events_clear=0x%08x, num_scanouts=%u, num_capsets=%u",
		c.events_read, c.events_clear, c.num_scanouts, c.num_capsets);

	if (c.events_clear) {
		config.events_read &= ~c.events_clear;

		err = vlo_config_set(vlo, &config, sizeof(config));
		if (err < 0) {
			error("vlo_config_set()");
			// FIXME
			goto out;
		}
	}

out:
	trace();
	return 0;
}

static int notify_go(struct es_thread *self, uint32_t events)
{
	(void)self;
	(void)events;

	static STAILQ_HEAD(, virtio_thread_request) tmp_queue = STAILQ_HEAD_INITIALIZER(tmp_queue);
	eventfd_t ev;
	bool kick0 = false, kick1 = false;
	int err;

	assert(vlo);

	trace();

	err = eventfd_read(self->fd, &ev);
	// FIXME: EAGAIN?
	if (err < 0) {
		error("eventfd_read()");
		// FIXME
		goto out;
	}

	pthread_mutex_lock(&req_queue_mutex);
	STAILQ_CONCAT(&tmp_queue, &req_queue);
	pthread_mutex_unlock(&req_queue_mutex);

	while(!STAILQ_EMPTY(&tmp_queue)) {
		struct virtio_thread_request *req = STAILQ_FIRST(&tmp_queue);
		STAILQ_REMOVE_HEAD(&tmp_queue, queue_entry);

		trace("(5) %d put %d", req->serial, gettid());

		if (req->queue == 0)
			kick0 = true;
		else if (req->queue == 1)
			kick1 = true;
		else
			assert(0);

		vlo_buf_put(req->buf, req->resp_len);
		free(req);
	}

	if (kick0) {
		err = vlo_kick(vlo, 0);
		if (err < 0)
			error("vlo_kick(0)");
	}
	if (kick1) {
		err = vlo_kick(vlo, 1);
		if (err < 0)
			error("vlo_kick(1)");
	}

out:
	return 0;
}

static int queue_go(struct es_thread *self, uint32_t events)
{
	(void)events;

	static unsigned int serial = 0;
	eventfd_t ev;
	int err;

	unsigned int queue = thread_to_queue_thread(self)->queue_index;

	assert(vlo);
	assert(queue == 0 || queue == 1);

	err = eventfd_read(self->fd, &ev);
	// FIXME: EAGAIN?
	if (err < 0) {
		error("eventfd_read() queue=%u (%s)", queue, self->name);
		// FIXME
		goto out;
	}

	while (vlo_buf_is_available(vlo, queue)) {
		struct virtio_thread_request *req = malloc(sizeof(*req));
		if (!req) {
			error("malloc()");
			goto out;
		}
		req->buf = vlo_buf_get(vlo, queue);
		if (!req->buf) {
			free(req);
			error("vlo_get_buffer()");
			goto out;
		}
		req->queue = queue;
		req->serial = serial++;

		trace("(1) %d on %d", req->serial, gettid());

		virtio_thread_new_request(req);
	}

	trace();
out:
	return 0;
}

static struct es_thread config_thread = {
	.name   = "config",
	.events = EPOLLIN,
	.test   = test_wait,
	.go     = config_go,
	.done   = NULL,
};
static struct queue_thread ctrl_thread = {
	.thread = {
		.name   = "ctrl",
		.events = EPOLLIN,
		.test   = test_wait,
		.go     = queue_go,
		.done   = NULL,
	},
	.queue_index = 0,
};
static struct queue_thread cursor_thread = {
	.thread = {
		.name   = "cursor",
		.events = EPOLLIN,
		.test   = test_wait,
		.go     = queue_go,
		.done   = NULL,
	},
	.queue_index = 1.
};
static struct es_thread notify_thread = {
	.name   = "notify",
	.events = EPOLLIN,
	.test   = notify_test,
	.go     = notify_go,
	.done   = NULL,
};

static void *virtio_thread(void *ptr)
{
	int err;

	assert(vlo);
	assert(es);

	(void)ptr;

	trace("tid: %d", gettid());

	err = es_schedule(es);
	if (err < 0) {
		error("es_schedule()");
		// FIXME: error
	}

	trace("exit");

	return NULL;
}

int virtio_thread_start(unsigned int num_capsets)
{
	int err;

	struct virtio_lo_qinfo qinfos[2] = { { .size = 1024u, }, { .size = 1024u, }, };
	config.num_capsets = num_capsets;

	notify_thread.fd = eventfd(0, EFD_NONBLOCK);
	if (notify_thread.fd < 0) {
		error("eventfd()");
		goto err;
	}

	uint64_t features = 1UL << VIRTIO_GPU_F_VIRGL | 1UL << VIRTIO_F_VERSION_1 | 1UL << VIRTIO_GPU_F_RESOURCE_UUID;
	vlo = vlo_init(VIRTIO_ID_GPU, 0x1af4, qinfos, 2, &config, sizeof(config), &features);
	if (!vlo) {
		error("vlo_init()");
		goto err_notify_close;
	}

	config_thread.fd = vlo_epoll_get_config(vlo);
	ctrl_thread.thread.fd   = vlo_epoll_get_kick(vlo, 0);
	cursor_thread.thread.fd = vlo_epoll_get_kick(vlo, 1);

	es = es_init(
		&config_thread,
		&ctrl_thread,
		&cursor_thread,
		&notify_thread,
		NULL);
	if (!es) {
		error("es_init()");
		goto err_vlo_done;
	}

	err = pthread_create(&thread, NULL, virtio_thread, NULL);
	if (err) {
		error("pthread_create(): %s", strerror(err));
		// FIXME: what shoul I do with the es instance that was not run?
		goto err_vlo_done;
	}

	return 0;

err_vlo_done:
	vlo_done(vlo);
err_notify_close:
	close(notify_thread.fd);
err:
	return -1;
}

void virtio_thread_stop(void)
{
	trace("");
	done = true;
	int err = eventfd_write(notify_thread.fd, 1);
	if (err)
		error_errno("eventfd_write()");
	pthread_join(thread, NULL);

	vlo_done(vlo);
}

/*
 * These two will be called from another thread, but as they call mmap()
 * of the driver, it looks like no mutex required.
 */
void *virtio_thread_map_guest(uint64_t gpa, int prot, size_t size)
{
	return vlo_map_guest(vlo, gpa, prot, size);
}

void virtio_thread_unmap_guest(void *addr, size_t size)
{
	vlo_unmap_guest(addr, size);
}
