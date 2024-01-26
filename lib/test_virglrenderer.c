#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/eventfd.h>
#include <assert.h>

#include <linux/virtio_gpu.h>

#include <gbm.h>
#include <virgl/virglrenderer.h>
#include <epoxy/egl.h>
#include <epoxy/gl.h>

#include "egl_helpers.h"
#include "drm_state.h"
#include "gettid.h"
#include "epoll_scheduler.h"
#include "virtio_thread.h"
#include "virtio_request.h"
#include "merr.h"

static const char *drm_device = "/dev/dri/card0";

static EGLDisplay egl_display;
static EGLConfig egl_config;
static EGLContext egl_current_context = EGL_NO_CONTEXT;
static EGLSurface egl_surface;
bool egl_surface_initialized = false;

static void *native_surface;

static void test_write_fence(void *cookie, uint32_t fence)
{
	(void)cookie;

	trace("F0x%u", fence);

	struct virtio_request_queue reqs_tmp = STAILQ_HEAD_INITIALIZER(reqs_tmp);

	STAILQ_CONCAT(&reqs_tmp, &virtio_request_fence);

	while (!STAILQ_EMPTY(&reqs_tmp)) {

		struct virtio_thread_request *req = STAILQ_FIRST(&reqs_tmp);
		STAILQ_REMOVE_HEAD(&reqs_tmp, queue_entry);

		if (req->fence_id > fence) {
			// leave this in the fence queue
			trace("S%u F0x%lu -> fence", req->serial, req->fence_id);
			STAILQ_INSERT_TAIL(&virtio_request_fence, req, queue_entry);
		} else {
			// reply
			// trace("S%u F0x%lu -> ready", req->serial, req->fence_id);
			// STAILQ_INSERT_TAIL(&virtio_request_ready, req, queue_entry);
			trace("S%u F0x%lu done", req->serial, req->fence_id);
			virtio_thread_request_done(req);
		}
	}
}

static virgl_renderer_gl_context test_create_gl_context(void *cookie, int scanout_idx, struct virgl_renderer_gl_ctx_param *param)
{
	(void)cookie;

	trace("*** CREATE scanout=%d, ver=%d.%d, shared=%s", scanout_idx, param->major_ver, param->minor_ver, param->shared ? "ture" : "false");

	virgl_renderer_gl_context ctx = EGL_NO_CONTEXT;
	EGLContext shared_context = EGL_NO_CONTEXT;

	EGLint ctxattr[] =
		{ EGL_CONTEXT_MAJOR_VERSION_KHR, param->major_ver
		, EGL_CONTEXT_MINOR_VERSION_KHR, param->minor_ver
		, EGL_NONE
		};

	if (param->shared) {
		shared_context = eglGetCurrentContext();
		if (shared_context == EGL_NO_CONTEXT)
			EGL_CHECK_ERROR("eglGetCurrentContext()", out);
	}

	egl_current_context = ctx = eglCreateContext(egl_display, egl_config, shared_context, ctxattr);
	if (ctx == EGL_NO_CONTEXT)
		EGL_CHECK_ERROR("eglCreateContext()", out);

out:
	trace("0x%016lx", (unsigned long)ctx);
	return ctx;

	// (void)scanout_idx;
	// (void)param;
	// return 0;
}

static void test_destroy_gl_context(void *cookie, virgl_renderer_gl_context ctx)
{

	(void)cookie;
	(void)ctx;

	trace("*** DESTROY 0x%016lx", (unsigned long)ctx);

	EGL_RET(eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT), out);
	EGL_RET(eglDestroyContext(egl_display, ctx), out);

	if (egl_current_context == ctx) {
		trace("??? destroying current context");
		egl_current_context = EGL_NO_CONTEXT;
	}

out:
	return;
}

static int test_make_current(void *cookie, int scanout_idx, virgl_renderer_gl_context ctx)
{
	(void)cookie;
	(void)scanout_idx;
	(void)ctx;

	trace("*** MAKE CURRENT 0x%016lx", (unsigned long)ctx);

	egl_current_context = ctx;

	EGL_RET(eglMakeCurrent(egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx), out);

out:
	return 0;
}

#if 0
static int test_get_drm_fd(void *cookie)
{
	(void)cookie;
	trace();
	return drm_state_get_fd();

}
#endif

struct virgl_renderer_callbacks virgl_cbs = {
	.version = 1,

	.write_fence = test_write_fence,

	.create_gl_context  = test_create_gl_context,
	.destroy_gl_context = test_destroy_gl_context,
	.make_current       = test_make_current,

	// .get_drm_fd = test_get_drm_fd,
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
	fprintf(stderr, "%c VIRGL %s: %s",
		log_level == VIRGL_LOG_LEVEL_ERROR ? '*' : '-',
		virgl_log_level_to_string(log_level) ?: "???",
		message);
}

#if 0
static void
MessageCallback(GLenum source,
		GLenum type,
		GLuint id,
		GLenum severity,
		GLsizei length,
		const GLchar* message,
		const void* userParam )
{
	(void)source;
	(void)id;
	(void)severity;
	(void)length;
	(void)userParam;

	const char *type_string = "???";

#define _X(x) case GL_DEBUG_TYPE_ ## x: type_string = #x; break;
	switch (type) {
	_X(ERROR)		// An error, typically from the API
	_X(DEPRECATED_BEHAVIOR)	// Some behavior marked deprecated has been used
	_X(UNDEFINED_BEHAVIOR)	// Something has invoked undefined behavior
	_X(PORTABILITY)		// Some functionality the user relies upon is not portable
	_X(PERFORMANCE)		// Code has triggered possible performance issues
	_X(MARKER)		// Command stream annotation
	_X(PUSH_GROUP)		// Group pushing
	_X(POP_GROUP)		// Group popping
	_X(OTHER)		// Some type that isn't one of these
	default: type_string = "???"; break;
	}
#undef _X

	fprintf(stderr, "%c GL %s: %s\n", type == GL_DEBUG_TYPE_ERROR ? '*' : '-', type_string, message);
}
#endif

static pthread_mutex_t req_queue_mutex = PTHREAD_MUTEX_INITIALIZER;
static STAILQ_HEAD(, virtio_thread_request) req_queue = STAILQ_HEAD_INITIALIZER(req_queue);

static enum es_test_result test_wait(struct es_thread *self)
{
	(void)self;
	return ES_WAIT;
}

static void check_ready_requests(void)
{
	while (!STAILQ_EMPTY(&virtio_request_ready)) {
		struct virtio_thread_request *req = STAILQ_FIRST(&virtio_request_ready);
		STAILQ_REMOVE_HEAD(&virtio_request_ready, queue_entry);
		// trace("S%u done", req->serial);
		virtio_thread_request_done(req);
	}
}

static int notify_go(struct es_thread *self, uint32_t events)
{
	(void)self;
	(void)events;

	STAILQ_HEAD(, virtio_thread_request) tmp_queue = STAILQ_HEAD_INITIALIZER(tmp_queue);
	eventfd_t ev;
	int err;

	err = eventfd_read(self->fd, &ev);
	if (err < 0) {
		merr("eventfd_read()");
		goto out;
	}

	pthread_mutex_lock(&req_queue_mutex);
	STAILQ_CONCAT(&tmp_queue, &req_queue);
	pthread_mutex_unlock(&req_queue_mutex);

	while(!STAILQ_EMPTY(&tmp_queue)) {
		struct virtio_thread_request *req = STAILQ_FIRST(&tmp_queue);
		STAILQ_REMOVE_HEAD(&tmp_queue, queue_entry);

		// trace("S%d put %d", req->serial, gettid());

		virtio_request(req);
		check_ready_requests();
	}
out:
	return 0;
}

static int poll_go(struct es_thread *self, uint32_t events)
{
	(void)self;
	(void)events;

	virgl_renderer_poll();

	return 0;
}

static struct es_thread notify_thread = {
	.name   = "notify",
	.events = EPOLLIN,
	.test   = test_wait,
	.go     = notify_go,
	.done   = NULL,
};

static struct es_thread virgl_poll_thread = {
	.name   = "virgl_poll",
	.events = EPOLLIN,
	.test   = test_wait,
	.go     = poll_go,
	.done   = NULL,
};

// called from the helper thread
void virtio_thread_new_request(struct virtio_thread_request *req)
{
	// trace("S%d put %d", req->serial, gettid());

	pthread_mutex_lock(&req_queue_mutex);
	STAILQ_INSERT_TAIL(&req_queue, req, queue_entry);
	pthread_mutex_unlock(&req_queue_mutex);

	int err = eventfd_write(notify_thread.fd, 1);
	if (err < 0)
		merr_errno("eventfd_write()");
}

static unsigned int get_num_capsets(void)
{
	uint32_t capset2_max_ver, capset2_max_size;
	virgl_renderer_get_cap_set(VIRTIO_GPU_CAPSET_VIRGL2, &capset2_max_ver, &capset2_max_size);
	return capset2_max_ver ? 2 : 1;
}

void draw(void)
{
	trace();

	if (!egl_surface_initialized) {
		trace("initialize surface");
		egl_surface = eglCreateWindowSurface(egl_display, egl_config, native_surface, NULL);
		if (egl_surface == EGL_NO_SURFACE)
			EGL_CHECK_ERROR("eglCreateWindowSurface()", out);
		// glGenFramebuffers(1, &s->fb);
		egl_surface_initialized = true;
	}

	EGL_RET(eglMakeCurrent(egl_display, egl_surface, egl_surface, egl_current_context), out);
	EGL_RET(eglSwapInterval(egl_display, 0), out);
	trace("vierport %ux%u@0,0", current_scanout.width, current_scanout.height);
	glViewport(0, 0, current_scanout.width, current_scanout.height);
	EGL_RET(eglSwapBuffers(egl_display, egl_surface), out);

	int err = drm_state_flip();
	if (err)
		merr("drm_state_flip()");

out:
	return;
}

int main(int argc, char **argv)
{
	void *native_display;
	struct es *es;
	int err;

	notify_thread.fd = eventfd(0, EFD_NONBLOCK);
	if (notify_thread.fd < 0) {
		merr_errno("eventfd()");
		goto err;
	}

	if (argc == 2)
		drm_device = argv[1];

	trace("drm_state_init(\"%s\")", drm_device);
	native_display = drm_state_init(drm_device, 0, &width, &heigth);
	if (!native_display) {
		merr("native_display()");
		goto err_close_efd;
	}

	trace("%hux%hu", width, heigth);

	egl_display = eglGetDisplay(native_display);
	if (egl_display == EGL_NO_DISPLAY)
		EGL_CHECK_ERROR("eglGetDisplay()", err_drm_state_done);

	trace("eglInitialize()");
	EGLint major, minor;
	EGL_RET(eglInitialize(egl_display, &major, &minor), err_egl_terminate);
	trace("EGL: %d.%d", (int)major, (int)minor);

	trace("eglBindAPI(EGL_OPENGL_ES_API)");
	EGL_RET(eglBindAPI(EGL_OPENGL_ES_API), err_egl_terminate);

	static const EGLint config_attrs[] =
		{    EGL_SURFACE_TYPE, EGL_WINDOW_BIT
		,        EGL_RED_SIZE, 8
		,      EGL_GREEN_SIZE, 8
		,       EGL_BLUE_SIZE, 8
		,      EGL_CONFORMANT, EGL_OPENGL_ES2_BIT
		, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT
		,            EGL_NONE
		};

	EGLint num_config;
	EGLint num_config2;

	trace("eglChooseConfig(0)");
	EGL_RET(eglChooseConfig(egl_display, config_attrs, NULL, 0, &num_config), err_egl_terminate);
	if (num_config < 1) {
		merr("num_config < 1");
		goto err_egl_terminate;
	}

	EGLConfig *configs = calloc(num_config, sizeof(EGLConfig));
	if (!configs) {
		merr("calloc()");
		goto err_egl_terminate;
	}

	trace("eglChooseConfig(%u)", num_config);
	EGL_RET(eglChooseConfig(egl_display, config_attrs, configs, num_config, &num_config2), err_free_configs);
	if (num_config != num_config2) {
		merr("num_config != num_config2");
		goto err_free_configs;
	}

#define GET_ATTRIBUTE(NAME) \
		EGLint attr_ ## NAME; \
		EGL_RET(eglGetConfigAttrib(egl_display, configs[i], EGL_ ## NAME, & attr_ ## NAME), err_free_configs);

	bool config_found = false;
	for (int i = 0; i < num_config; i++) {

		GET_ATTRIBUTE(NATIVE_VISUAL_ID);

		if (!config_found && attr_NATIVE_VISUAL_ID == GBM_FORMAT_XRGB8888) {

			config_found = true;
			egl_config = configs[i];

			GET_ATTRIBUTE(CONFIG_ID);
			GET_ATTRIBUTE(BUFFER_SIZE);
			GET_ATTRIBUTE(ALPHA_SIZE);
			GET_ATTRIBUTE(RED_SIZE);
			GET_ATTRIBUTE(GREEN_SIZE);
			GET_ATTRIBUTE(BLUE_SIZE);
			GET_ATTRIBUTE(DEPTH_SIZE);
			GET_ATTRIBUTE(STENCIL_SIZE);
			GET_ATTRIBUTE(SAMPLES);

			char format_string[5];
			format_string[0] = (char)((attr_NATIVE_VISUAL_ID >>  0) & 0xff);
			format_string[1] = (char)((attr_NATIVE_VISUAL_ID >>  8) & 0xff);
			format_string[2] = (char)((attr_NATIVE_VISUAL_ID >> 16) & 0xff);
			format_string[3] = (char)((attr_NATIVE_VISUAL_ID >> 24) & 0xff);
			format_string[4] = 0;

			trace("id: %u, format=%u (\"%s\"), buffer_size=%u, alpha=%u, red=%u, green=%u, blue=%u, depth=%u, stencil=%u, samples=%u",
				attr_CONFIG_ID, attr_NATIVE_VISUAL_ID, format_string,
				attr_BUFFER_SIZE, attr_ALPHA_SIZE, attr_RED_SIZE, attr_GREEN_SIZE, attr_BLUE_SIZE, attr_DEPTH_SIZE, attr_STENCIL_SIZE, attr_SAMPLES
			);

			break;
		}
	}

#undef GET_ATTRIBUTE

	if (!config_found) {
		merr("could not find a good configuration");
		goto err_free_configs;
	}

	EGLint attr_native_visual_id;
	EGL_RET(eglGetConfigAttrib(egl_display, egl_config, EGL_NATIVE_VISUAL_ID, &attr_native_visual_id), err_free_configs);

	trace("drm_surface_create(%u)", attr_native_visual_id);
	native_surface = drm_surface_create((uint32_t)attr_native_visual_id);
	if (!native_surface) {
		merr("drm_surface_create()");
		goto err_free_configs;
	}

	// EGLint ctxattr[] = { EGL_CONTEXT_MAJOR_VERSION_KHR, 3,
	// 		     EGL_CONTEXT_MINOR_VERSION_KHR, 0, EGL_NONE };
	// egl_context = eglCreateContext(egl_display, egl_config, EGL_NO_CONTEXT, ctxattr);
	// if (egl_context == EGL_NO_CONTEXT)
	// 	EGL_CHECK_ERROR("eglCreateContext()", err_drm_surface_destroy);
	// trace("*** CONTEXT 0x%016lx", (unsigned long)egl_context);

	virgl_set_log_callback(virgl_log_callback, NULL, NULL);

	// For some reason cookie can not be NULL
	// err = virgl_renderer_init((void *)1, VIRGL_RENDERER_USE_EGL, &virgl_cbs);
	err = virgl_renderer_init((void *)1, VIRGL_RENDERER_THREAD_SYNC, &virgl_cbs);
	if (err < 0) {
		merr("virgl_renderer_init(): %s", strerror(err));
		// goto err_egl_destroy_context;
		goto err_drm_surface_destroy;
	}

	virgl_poll_thread.fd = virgl_renderer_get_poll_fd();
	if (virgl_poll_thread.fd < 0) {
		merr_errno("virgl_renderer_get_poll_fd()");
		goto err_virgl_cleanup;
	}

	unsigned int num_capsets = get_num_capsets();
	trace("num_capsets=%u", num_capsets);

	glEnable(GL_DEBUG_OUTPUT);
	// glDebugMessageCallback(MessageCallback, 0);

	err = virtio_thread_start(num_capsets);
	if (err) {
		merr("virtio_thread_start()");
		goto err_virgl_cleanup;
	}

	es = es_init(
		&notify_thread,
		&virgl_poll_thread,
		NULL);
	if (!es) {
		merr("es_init()");
		goto err_stop_virtio_thread;
	}

	err = es_schedule(es);
	if (err < 0) {
		merr("es_schedule()");
		goto err_stop_virtio_thread;
	}

	virtio_thread_stop();
	virgl_renderer_cleanup(NULL);
	// eglDestroyContext(egl_display, egl_context);
	drm_surface_destroy();
	free(configs);
	eglTerminate(egl_display);
	drm_state_done();
	close(notify_thread.fd);
	exit(EXIT_SUCCESS);

err_stop_virtio_thread:
	virtio_thread_stop();
err_virgl_cleanup:
	virgl_renderer_cleanup(NULL);
// err_egl_destroy_context:
// 	eglDestroyContext(egl_display, egl_context);
err_drm_surface_destroy:
	drm_surface_destroy();
err_free_configs:
	free(configs);
err_egl_terminate:
	eglTerminate(egl_display);
err_drm_state_done:
	drm_state_done();
err_close_efd:
	close(notify_thread.fd);
err:
	exit(EXIT_FAILURE);
}
