#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "error.h"

#include <virgl/virglrenderer.h>

static void test_write_fence(void *cookie, uint32_t fence)
{
	(void)cookie;
	(void)fence;
}

struct virgl_renderer_callbacks virgl_cbs = {
	.version = 1,
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
#endif
};

int main(int argc, char **argv)
{
	int err;

	(void)argc;
	(void)argv;

	printf("Hello world\n");

	int virgl_flags = VIRGL_RENDERER_USE_EGL | VIRGL_RENDERER_USE_SURFACELESS;
	err = virgl_renderer_init(NULL, virgl_flags, &virgl_cbs);
	if (err < 0) {
		error("virgl_renderer_init(): %s", strerror(err));
		goto err;
	}

	exit(EXIT_SUCCESS);
err:
	exit(EXIT_FAILURE);
}
