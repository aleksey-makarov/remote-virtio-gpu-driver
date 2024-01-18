#ifndef __virtio_thread_h__
#define __virtio_thread_h__

#include <sys/queue.h>
#include <stdint.h>
#include <stddef.h>

struct vlo_buf;

struct virtio_thread_request {
	struct vlo_buf *buf;
	unsigned int resp_len;
	unsigned int queue;
	unsigned int serial;
	STAILQ_ENTRY(virtio_thread_request) queue_entry;
};

extern void virtio_thread_new_request(struct virtio_thread_request *req);
void virtio_thread_request_done(struct virtio_thread_request *req);

int virtio_thread_start(unsigned int num_capsets);
void virtio_thread_stop(void);

void *virtio_thread_map_guest(uint64_t gpa, int prot, size_t size);
void virtio_thread_unmap_guest(void *addr, size_t size);

#endif
