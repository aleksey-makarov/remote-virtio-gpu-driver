#ifndef __egl_helpers_h__
#define __egl_helpers_h__

#include <epoxy/egl.h>

#include "merr.h"

const char *eglErrorString(EGLint error);

#define EGL_CHECK_ERROR(_s, _l)                                    \
	do {                                                       \
		EGLint _err = eglGetError();                       \
		if (_err != EGL_SUCCESS) {                         \
			const char *_err_s = eglErrorString(_err); \
			if (_err_s)                                \
				merr("%s : %s", _s, _err_s);     \
			else                                       \
				merr("%s : 0x%04x", _s, _err);   \
			goto _l;                                   \
		}                                                  \
	} while (0)


#define EGL_RET(_f, _l)                                            \
	do {                                                       \
		EGLBoolean _ret = (_f);                            \
		if (_ret == EGL_FALSE) {                           \
			const char *_f_s = #_f;                    \
			EGL_CHECK_ERROR(_f_s, _l);                 \
		}                                                  \
	} while (0)

#endif
