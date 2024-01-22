/*******************************************************************************
 * Copyright (c) 2023 OpenSynergy GmbH.
 *
 * This software may not be used in any way or distributed without
 * permission. All rights reserved.
 ******************************************************************************/

#ifndef __drm_state_h__
#define __drm_state_h__

#include <stdint.h>

/* Returns native display */
void *drm_state_init(uint16_t *h, uint16_t *v);
void drm_state_done(void);

/* Returns native window */
struct gbm_surface *drm_surface_create(uint32_t format);
void drm_surface_destroy(void);

int drm_state_flip(void);

#endif
