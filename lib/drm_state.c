/*******************************************************************************
 * Copyright (c) 2023 OpenSynergy GmbH.
 *
 * This software may not be used in any way or distributed without
 * permission. All rights reserved.
 ******************************************************************************/

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

#include <gbm.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define LOG_TAG "libuhmigl"
#include "pr.h"

#ifndef __unused
#define __unused __attribute__((unused))
#endif

static const char * device_name_default = "/dev/dri/card2";
static const char * device_name_env_var_name = "LIBUHMIGL_DEVICE_NAME";

static const char * connector_number_env_var_name = "LIBUHMIGL_CONNECTOR_NUMBER";

static int fd;
static drmModeRes *resources;
static drmModeConnector *connector;
static drmModeEncoder *encoder;
static drmModeCrtc *crtc;

static drmModeModeInfo *drm_state_mode;
struct gbm_device *drm_state_gbm_device;
struct gbm_surface *drm_state_gbm_surface;

static int crtc_set;

struct fb_state {
	int fd;
	struct gbm_bo *bo;
	uint32_t fb_id;
	struct fb_state *next;
};

void *drm_state_init(uint16_t *h, uint16_t *v)
{
	assert(!fd);
	assert(!resources);
	assert(!connector);
	assert(!encoder);
	assert(!crtc);
	assert(!drm_state_gbm_device);
	assert(!drm_state_gbm_surface);

	const char * device_name = getenv(device_name_env_var_name);
	if (!device_name)
		device_name = device_name_default;

	int connector_number = 0;
	const char * connector_number_string = getenv(connector_number_env_var_name);
	if (connector_number_string)
		connector_number = atoi(connector_number_string);

	pr_info("open(%s) (connector number: %d)", device_name, connector_number);
	fd = open(device_name, O_RDWR);
	if (fd < 0) {
		pr_err("open(%s) (%s) (device could be specified with %s environment variable)", device_name, strerror(errno), device_name_env_var_name);
		fd = 0;
		goto error;
	}

	pr_info("drmModeGetResources()");
	resources = drmModeGetResources(fd);
	if (!resources) {
		pr_err("drmModeGetResources()");
		goto error_close;
	}

	pr_info("count_fbs: %u, count_crtcs: %u, count_connectors: %u, count_encoders: %u, %ux%u - %ux%u",
		(unsigned)resources->count_fbs,
		(unsigned)resources->count_crtcs,
		(unsigned)resources->count_connectors,
		(unsigned)resources->count_encoders,
		(unsigned)resources->min_width, (unsigned)resources->min_height,
		(unsigned)resources->max_width, (unsigned)resources->max_height);

	pr_info("find a connected connector");
	for (int c = 0; c < resources->count_connectors; c++) {
		drmModeConnector *connector_tmp = drmModeGetConnector(fd, resources->connectors[c]);
		int this = DRM_MODE_CONNECTED == connector_tmp->connection && connector_number-- == 0;
		pr_info("%c connector_id: %u, connection: %u (%s)",
			this ? '>' : ' ',
			(unsigned)connector_tmp->connector_id,
			(unsigned)connector_tmp->connection,
			connector_tmp->connection == DRM_MODE_CONNECTED ? "connected" : "-"
		);
		if (this)
			connector = connector_tmp;
		else
			drmModeFreeConnector(connector_tmp);
	}

	if (!connector) {
		pr_err("could not find connected connectors");
		goto error_free_resources;
	}

	pr_info("find resolution (full-screen)");
	unsigned int best_area = 0;
	drmModeModeInfo* best_mode = NULL;
	for (int m = 0; m < connector->count_modes; m++) {
		drmModeModeInfo* cur_mode = &connector->modes[m];
#if 0
		pr_info("mode: \"%s\" %ux%u%s",
			cur_mode->name,
			(unsigned)cur_mode->hdisplay,
			(unsigned)cur_mode->vdisplay,
			cur_mode->type & DRM_MODE_TYPE_PREFERRED ? " (preferred)" : "");
#endif
		unsigned int cur_area = cur_mode->hdisplay * cur_mode->vdisplay;
		if (cur_area > best_area) {
			best_mode = cur_mode;
			best_area = cur_area;
		}
		if (cur_mode->type & DRM_MODE_TYPE_PREFERRED) {
			drm_state_mode = cur_mode;
		}
	}

	if (!drm_state_mode) {
		if (best_mode) {
			pr_err("no preferred mode, use the best one");
			drm_state_mode = best_mode;
		} else {
			pr_err("failed to find a suitable mode");
			goto error_free_connector;
		}
	}

	pr_info("final mode: \"%s\" %ux%u",
		drm_state_mode->name,
		(unsigned)drm_state_mode->hdisplay,
		(unsigned)drm_state_mode->vdisplay);

	pr_info("find a connected encoder");
	for (int e = 0; e < connector->count_encoders; e++) {
		encoder = drmModeGetEncoder(fd, connector->encoders[e]);
		if (!encoder)
			continue;
		for (int c = 0; c < resources->count_crtcs; c++) {
			if (encoder->crtc_id == resources->crtcs[c])
				goto encoder_found;
		}
		drmModeFreeEncoder(encoder);
	}

	pr_info("try to find a sutable encoder");
	for (int e = 0; e < connector->count_encoders; e++) {
		encoder = drmModeGetEncoder(fd, connector->encoders[e]);
		if (!encoder)
			continue;
		for (int c = 0; c < resources->count_crtcs; c++) {
			if (encoder->possible_crtcs & (1 << c)) {
				encoder->crtc_id = resources->crtcs[c];
				goto encoder_found;
			}
		}
		drmModeFreeEncoder(encoder);
	}

	pr_err("failed to find a suitable encoder");
	goto error_free_connector;

encoder_found:

	pr_info("drmModeGetCrtc()");
	crtc = drmModeGetCrtc(fd, encoder->crtc_id);
	if (!crtc) {
		pr_info("drmModeGetCrtc()");
		goto error_free_encoder;
	}

	pr_info("current mode: \"%s\" %ux%u",
		crtc->mode.name,
		(unsigned)crtc->mode.hdisplay,
		(unsigned)crtc->mode.vdisplay);

	pr_info("gbm_create_device()");
	drm_state_gbm_device = gbm_create_device(fd);
	if (!drm_state_gbm_device) {
		pr_err("Failed to create GBM device\n");
		goto error_free_crtc;
	}

	pr_info("gbm device backend: %s", gbm_device_get_backend_name(drm_state_gbm_device));

	if (h)
		*h = drm_state_mode->hdisplay;
	if (v)
		*v = drm_state_mode->vdisplay;

	return drm_state_gbm_device;

error_free_crtc:
	drmModeFreeCrtc(crtc);
	crtc = 0;
error_free_encoder:
	drmModeFreeEncoder(encoder);
	encoder = 0;
error_free_connector:
	drmModeFreeConnector(connector);
	connector = 0;
error_free_resources:
	drmModeFreeResources(resources);
	resources = 0;
error_close:
	close(fd);
	fd = 0;
error:
	return NULL;
}

void drm_state_done(void)
{
	int err;

	assert(fd);
	assert(resources);
	assert(connector);
	assert(encoder);
	assert(crtc);
	assert(drm_state_gbm_device);

	pr_info("gbm_device_destroy()");
	gbm_device_destroy(drm_state_gbm_device);
	drm_state_gbm_device = 0;

	pr_info("drmModeSetCrtc()");
	err = drmModeSetCrtc(fd, crtc->crtc_id, crtc->buffer_id, crtc->x, crtc->y, &connector->connector_id, 1, &crtc->mode);
	if (err < 0)
		pr_err("failed to restore original CRTC: %d", err);
	pr_info("drmModeFreeCrtc()");
	drmModeFreeCrtc(crtc);
	crtc = 0;

	pr_info("drmModeFreeEncoder()");
	drmModeFreeEncoder(encoder);
	encoder = 0;

	pr_info("drmModeFreeConnector()");
	drmModeFreeConnector(connector);
	connector = 0;

	pr_info("drmModeFreeResources()");
	drmModeFreeResources(resources);
	resources = 0;

	pr_info("close()");
	close(fd);
	fd = 0;
}

void *drm_surface_create(uint32_t format)
{
	pr_info("gbm_surface_create(h=%u,v=%u,format=%u)", drm_state_mode->hdisplay, drm_state_mode->vdisplay, format);
	drm_state_gbm_surface = gbm_surface_create(drm_state_gbm_device,
		drm_state_mode->hdisplay,
		drm_state_mode->vdisplay,
		format,
		GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
	if (!drm_state_gbm_surface) {
		pr_err("gbm_surface_create()");
		goto error;
	}

	return drm_state_gbm_surface;

error:
	return NULL;
}

void drm_surface_destroy(void)
{
	pr_info("gbm_surface_destroy()");
	gbm_surface_destroy(drm_state_gbm_surface);
}

/* Frame buffer allocation
 */

static void fb_destroy_callback(__unused struct gbm_bo* bo, void* data)
{
	struct fb_state *fb = data;

	assert(fb);

	if (fb->fb_id)
		drmModeRmFB(fb->fd, fb->fb_id);

	free(fb);
}

static struct fb_state *fb_get_from_bo(struct gbm_bo* bo)
{
	struct fb_state *fb;
	assert(bo);

	fb = gbm_bo_get_user_data(bo);
	if (fb)
		return fb;

	fb = malloc(sizeof(struct fb_state));
	if (!fb) {
		pr_err("alloc()");
		goto error;
	}

	unsigned int width = gbm_bo_get_width(bo);
	unsigned int height = gbm_bo_get_height(bo);
	unsigned int handles[4] = { 0, };
	unsigned int strides[4] = { 0, };
	unsigned int offsets[4] = { 0, };
	unsigned int format = gbm_bo_get_format(bo);
	unsigned int fb_id = 0;

	for (int i = 0; i < gbm_bo_get_plane_count(bo); i++) {
		handles[i] = gbm_bo_get_handle_for_plane(bo, i).u32;
		strides[i] = gbm_bo_get_stride_for_plane(bo, i);
		offsets[i] = gbm_bo_get_offset(bo, i);
	}

	int err = drmModeAddFB2(fd, width, height, format, handles,
				strides, offsets, &fb_id, 0);
	if (err < 0) {
		pr_err("drmModeAddFB2(): %d", err);
		goto error_fb_state_free;
	}

	fb->fd = fd;
	fb->bo = bo;
	fb->fb_id = fb_id;

	gbm_bo_set_user_data(bo, fb, fb_destroy_callback);
	return fb;

error_fb_state_free:
	free(fb);
error:
	return NULL;
}

/* Page flip
 */

static void page_flip_handler(__unused int fd,
			      __unused unsigned int frame,
			      __unused unsigned int sec,
			      __unused unsigned int usec,
			      __unused void* data)
{
	// FIXME: is it mandatory to provide the callback function?
}

#define TIMEOUT_MS 500

int drm_state_flip(void)
{
	int err;
	struct gbm_bo *bo;
	struct fb_state *fb;

	struct timeval timeout = {0, TIMEOUT_MS * 1000};
	fd_set fds;
	drmEventContext ev_ctx;

	bo = gbm_surface_lock_front_buffer(drm_state_gbm_surface);
	if (!bo) {
		pr_err("failed to get gbm front buffer");
		return -1;
	}

	fb = fb_get_from_bo(bo);
	if (!fb) {
		pr_err("fb_get_from_bo()");
		goto error;
	}

	if (!crtc_set) {
		err = drmModeSetCrtc(fd, encoder->crtc_id, fb->fb_id, 0, 0,
				     &connector->connector_id, 1, drm_state_mode);
		if (err < 0) {
			pr_err("drmModeSetCrtc(): %d", err);
			goto error;
		}
		crtc_set = 1;
	} else {
		err = drmModePageFlip(fd, encoder->crtc_id, fb->fb_id, DRM_MODE_PAGE_FLIP_EVENT, NULL);
		if (err < 0) {
			pr_err("drmModePageFlip(): %d", err);
			goto error;
		}

		// FIXME: if vsync

		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		memset(&ev_ctx, 0, sizeof(ev_ctx));
		ev_ctx.version = 2;
		ev_ctx.page_flip_handler = page_flip_handler;

		err = select(fd + 1, &fds, 0, 0, &timeout);
		if (err == 1) {
			drmHandleEvent(fd, &ev_ctx);
		} else if (err == 0) {
			pr_err("select(): timeout");
			goto error;
		} else {
			if (err < 0)
				pr_err("select() (%s)", strerror(errno));

			else
				pr_err("select() ???");
			goto error;
		}
	}

	gbm_surface_release_buffer(drm_state_gbm_surface, bo);
	return 0;

error:
	gbm_surface_release_buffer(drm_state_gbm_surface, bo);
	return -1;
}
