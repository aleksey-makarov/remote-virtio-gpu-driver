#ifndef __drm_state_h__
#define __drm_state_h__

#include <stdint.h>

/* Returns native display */
void *drm_state_init(const char *device_name, unsigned int connector_number, uint16_t *h, uint16_t *v);
void drm_state_done(void);

/* Returns native window */
void *drm_surface_create(uint32_t format);
void drm_surface_destroy(void);

int drm_state_flip(void);

int drm_state_get_fd(void);

#endif
