#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "error.h"

#include <virgl/virglrenderer.h>

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

int main(int argc, char **argv)
{
	int err;

	if (argc == 2)
		drm_device = argv[1];

	trace("drm device: \"%s\"", drm_device);

	drm_device_fd = open(drm_device, O_RDWR);
	if (drm_device_fd < 0) {
		error_errno("open(\"%s\")", drm_device);
		goto err;
	}

	virgl_set_log_callback(virgl_log_callback, NULL, NULL);

	// For some reason cookie can not be NULL
	err = virgl_renderer_init((void *)1, VIRGL_RENDERER_USE_EGL, &virgl_cbs);
	if (err < 0) {
		error("virgl_renderer_init(): %s", strerror(err));
		goto err_close_drm;
	}

	exit(EXIT_SUCCESS);

err_close_drm:
	close(drm_device_fd);
err:
	exit(EXIT_FAILURE);
}
