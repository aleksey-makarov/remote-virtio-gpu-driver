#ifndef __virtio_request_h__
#define __virtio_request_h__

#include <sys/queue.h>

struct virtio_thread_request;

STAILQ_HEAD(virtio_request_queue, virtio_thread_request);

extern struct virtio_request_queue virtio_request_ready;
extern struct virtio_request_queue virtio_request_fence;

void virtio_request(struct virtio_thread_request *req);

#endif
