#ifndef __virtio_request_h__
#define __virtio_request_h__

#include <sys/queue.h>
#include <stdint.h>

struct virtio_thread_request;

STAILQ_HEAD(virtio_request_queue, virtio_thread_request);

extern struct virtio_request_queue virtio_request_ready;
extern struct virtio_request_queue virtio_request_fence;

extern uint16_t width, heigth;

extern struct virgl_renderer_resource_info current_scanout_info;
struct current_scanout {
	uint32_t x;
	uint32_t y;
	uint32_t width;
	uint32_t height;
	uint32_t scanout_id;
	uint32_t resource_id;
};
extern struct current_scanout current_scanout;

extern void draw(void);

void virtio_request(struct virtio_thread_request *req);

#endif
