#ifndef __virtio_request_h__
#define __virtio_request_h__

struct vlo_buf;

unsigned int virtio_request(struct vlo_buf *buf, unsigned int queue);

#endif
