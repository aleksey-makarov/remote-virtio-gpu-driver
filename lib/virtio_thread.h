#ifndef __virtio_thread_h__
#define __virtio_thread_h__

#include <sys/queue.h>

struct virtio_thread_request {
	unsigned int serial;
	STAILQ_ENTRY(virtio_thread_request) queue_entry;
};

extern void virtio_thread_new_request(struct virtio_thread_request *req);
void virtio_thread_request_done(struct virtio_thread_request *req);

int virtio_thread_start(void);
void virtio_thread_stop(void);

#endif
