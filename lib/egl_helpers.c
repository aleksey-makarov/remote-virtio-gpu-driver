#include "egl_helpers.h"

#include <stddef.h>

const char *eglErrorString(EGLint error)
{
	switch (error) {
	case EGL_SUCCESS:
		return "no error";
	case EGL_NOT_INITIALIZED:
		return "EGL not initialized or failed to initialize";
	case EGL_BAD_ACCESS:
		return "resource inaccessible";
	case EGL_BAD_ALLOC:
		return "cannot allocate resources";
	case EGL_BAD_ATTRIBUTE:
		return "unrecognized attribute or attribute value";
	case EGL_BAD_CONTEXT:
		return "invalid EGL context";
	case EGL_BAD_CONFIG:
		return "invalid EGL frame buffer configuration";
	case EGL_BAD_CURRENT_SURFACE:
		return "current surface is no longer valid";
	case EGL_BAD_DISPLAY:
		return "invalid EGL display";
	case EGL_BAD_SURFACE:
		return "invalid surface";
	case EGL_BAD_MATCH:
		return "inconsistent arguments";
	case EGL_BAD_PARAMETER:
		return "invalid argument";
	case EGL_BAD_NATIVE_PIXMAP:
		return "invalid native pixmap";
	case EGL_BAD_NATIVE_WINDOW:
		return "invalid native window";
	case EGL_CONTEXT_LOST:
		return "context lost";
	default:
		return NULL;
	}
}
