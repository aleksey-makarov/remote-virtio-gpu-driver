#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/eventfd.h>

#include <linux/virtio_gpu.h>

#include <virgl/virglrenderer.h>

#include "gettid.h"
#include "epoll_scheduler.h"
#include "virtio_thread.h"
#include "virtio_request.h"
#include "error.h"

static const char *drm_device = "/dev/dri/card0";
static int drm_device_fd;

static void test_write_fence(void *cookie, uint32_t fence)
{
	(void)cookie;
	(void)fence;
	trace();
}


static int test_get_drm_fd(void *cookie)
{
	(void)cookie;
	trace();
	return drm_device_fd;
}

#if 0
static virgl_renderer_gl_context create_gl_context(void *cookie, int scanout_idx, struct virgl_renderer_gl_ctx_param *param)
{ return NULL; }
static void destroy_gl_context(void *cookie, virgl_renderer_gl_context ctx)
{}
static int make_current(void *cookie, int scanout_idx, virgl_renderer_gl_context ctx)
{ return 0; }
#endif

struct virgl_renderer_callbacks virgl_cbs = {
	.version = 2,
	.write_fence = test_write_fence,
#if 0
	/*
	 * The following 3 callbacks allows virglrenderer to
	 * use winsys from caller, instead of initializing it's own
	 * winsys (flag VIRGL_RENDERER_USE_EGL or VIRGL_RENDERER_USE_GLX).
	 */
	/* create a GL/GLES context */
	virgl_renderer_gl_context (*create_gl_context)(void *cookie, int scanout_idx, struct virgl_renderer_gl_ctx_param *param);
	/* destroy a GL/GLES context */
	void (*destroy_gl_context)(void *cookie, virgl_renderer_gl_context ctx);
	/* make a context current, returns 0 on success and negative errno on failure */
	int (*make_current)(void *cookie, int scanout_idx, virgl_renderer_gl_context ctx);
#else
	.create_gl_context = NULL,
	.destroy_gl_context = NULL,
	.make_current = NULL,

#endif
	.get_drm_fd = test_get_drm_fd,
};

static const char *virgl_log_level_to_string(enum virgl_log_level_flags l)
{
	switch(l) {
#define _X(x) case VIRGL_LOG_LEVEL_ ## x: return #x;
	_X(DEBUG)
	_X(INFO)
	_X(WARNING)
	_X(ERROR)
#undef _X
	default:
		return NULL;
	}
}

static void virgl_log_callback(
	enum virgl_log_level_flags log_level,
	const char *message,
	void* user_data)
{
	(void)user_data;
	fprintf(stderr, "%c %s: %s",
		log_level == VIRGL_LOG_LEVEL_ERROR ? '*' : '-',
		virgl_log_level_to_string(log_level) ?: "???",
		message);
}

static pthread_mutex_t req_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static STAILQ_HEAD(, virtio_thread_request) req_queue = STAILQ_HEAD_INITIALIZER(req_queue);

static enum es_test_result test_wait(struct es_thread *self)
{
	(void)self;
	return ES_WAIT;
}

static int notify_go(struct es_thread *self, uint32_t events)
{
	(void)self;
	(void)events;

	static STAILQ_HEAD(, virtio_thread_request) tmp_queue = STAILQ_HEAD_INITIALIZER(tmp_queue);
	eventfd_t ev;
	int err;

	err = eventfd_read(self->fd, &ev);
	if (err < 0) {
		error("eventfd_read()");
		goto out;
	}

	pthread_mutex_lock(&req_queue_mutex);
	STAILQ_CONCAT(&tmp_queue, &req_queue);
	pthread_mutex_unlock(&req_queue_mutex);

	while(!STAILQ_EMPTY(&tmp_queue)) {
		struct virtio_thread_request *req = STAILQ_FIRST(&tmp_queue);
		STAILQ_REMOVE_HEAD(&tmp_queue, queue_entry);

		// trace("(3) %d put %d", req->serial, gettid());

		req->resp_len = virtio_request(req->buf);
		virtio_thread_request_done(req);
	}
out:
	return 0;
}

static struct es_thread notify_thread = {
	.name   = "notify",
	.events = EPOLLIN,
	.test   = test_wait,
	.go     = notify_go,
	.done   = NULL,
};

// called from the helper thread
void virtio_thread_new_request(struct virtio_thread_request *req)
{
	// trace("(2) %d put %d", req->serial, gettid());

	pthread_mutex_lock(&req_queue_mutex);
	STAILQ_INSERT_TAIL(&req_queue, req, queue_entry);
	pthread_mutex_unlock(&req_queue_mutex);

	int err = eventfd_write(notify_thread.fd, 1);
	if (err < 0)
		error_errno("eventfd_write()");
}

static unsigned int get_num_capsets(void)
{
	uint32_t capset2_max_ver, capset2_max_size;
	virgl_renderer_get_cap_set(VIRTIO_GPU_CAPSET_VIRGL2, &capset2_max_ver, &capset2_max_size);
	return capset2_max_ver ? 2 : 1;
}

int main(int argc, char **argv)
{
	struct es *es;
	int err;

	notify_thread.fd = eventfd(0, EFD_NONBLOCK);
	if (notify_thread.fd < 0) {
		error_errno("eventfd()");
		goto err;
	}

	if (argc == 2)
		drm_device = argv[1];

	trace("open(\"%s\")", drm_device);
	drm_device_fd = open(drm_device, O_RDWR);
	if (drm_device_fd < 0) {
		error_errno("open(\"%s\")", drm_device);
		goto err_close_efd;
	}

	virgl_set_log_callback(virgl_log_callback, NULL, NULL);

	// For some reason cookie can not be NULL
	err = virgl_renderer_init((void *)1, VIRGL_RENDERER_USE_EGL, &virgl_cbs);
	if (err < 0) {
		error("virgl_renderer_init(): %s", strerror(err));
		goto err_close_drm;
	}

	unsigned int num_capsets = get_num_capsets();
	trace("num_capsets=%u", num_capsets);

	err = virtio_thread_start(num_capsets);
	if (err) {
		error("virtio_thread_start()");
		goto err_virgl_cleanup;
	}

	es = es_init(
		&notify_thread,
		NULL);
	if (!es) {
		error("es_init()");
		goto err_stop_virtio_thread;
	}

	err = es_schedule(es);
	if (err < 0) {
		error("es_schedule()");
		goto err_stop_virtio_thread;
	}

	virtio_thread_stop();
	virgl_renderer_cleanup(NULL);
	close(drm_device_fd);
	close(notify_thread.fd);
	exit(EXIT_SUCCESS);

err_stop_virtio_thread:
	virtio_thread_stop();
err_virgl_cleanup:
	virgl_renderer_cleanup(NULL);
err_close_drm:
	close(drm_device_fd);
err_close_efd:
	close(notify_thread.fd);
err:
	exit(EXIT_FAILURE);
}
