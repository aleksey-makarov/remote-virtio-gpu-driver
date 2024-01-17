#ifndef __libvirtiolo_h__
#define __libvirtiolo_h__

#include <stdbool.h>
#include <sys/uio.h>

#include <linux/virtio_ring.h>
#include <linux/virtio_lo.h>

#include "counted_by.h"

struct vlo;
struct vlo_vring;

struct vlo_buf {
	uint16_t idx;
	struct vlo_vring *vr;

	/* length of io[] array */
	unsigned int ion;
	/*
	 * in the io[] array first zero or more items
	 * mapped readonly (guest transmit),
	 * all other are mapped write only (guest receive);
	 *
	 * ion_transmit is the length of the guest transmit subarray
	 */
	unsigned int ion_transmit;
	struct iovec io[] __counted_by(ion);
};

/*
 * NB: in qinfo should be filled only .size
 * Other fields are initialized by this function
 */
struct vlo *vlo_init(
	unsigned int device_id,
	unsigned int vendor_id,
	struct virtio_lo_qinfo *qinfos,
	unsigned int qinfosn,
	void *config,
	unsigned int confign,
	uint64_t *features
);

void vlo_done(struct vlo *v);

/* always successfull */
bool vlo_buf_is_available(struct vlo *vl, unsigned int queue);

struct vlo_buf *vlo_buf_get(struct vlo *vl, unsigned int queue);

void vlo_buf_put(struct vlo_buf *req, unsigned int resp_len);

/* if queue == -1, all the queues are notified */
int vlo_kick(struct vlo *vl, int queue);

int vlo_config_get(struct vlo *vl, void *config, unsigned int config_length);
int vlo_config_set(struct vlo *vl, void *config, unsigned int config_length);

/* always successfull on correct vl and queue, else abort on debug builds */
int vlo_epoll_get_config(struct vlo *vl);
int vlo_epoll_get_kick(struct vlo *vl, unsigned int queue);

#endif
