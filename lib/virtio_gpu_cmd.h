#ifndef __virtio_gpu_cmd_h__
#define __virtio_gpu_cmd_h__

#include <linux/virtio_gpu.h>

union virtio_gpu_cmd {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_resource_unref resource_unref;
	struct virtio_gpu_resource_create_2d resource_create_2d;
	struct virtio_gpu_set_scanout set_scanout;
	struct virtio_gpu_resource_flush resource_flush;
	struct virtio_gpu_transfer_to_host_2d transfer_to_host_2d;
	struct {
		struct virtio_gpu_resource_attach_backing resource_attach_backing;
		struct virtio_gpu_mem_entry mem_entry[];
	};
	struct virtio_gpu_resource_detach_backing resource_detach_backing;
	struct virtio_gpu_transfer_host_3d transfer_host_3d;
	struct virtio_gpu_resource_create_3d resource_create_3d;
	struct virtio_gpu_ctx_create ctx_create;
	struct virtio_gpu_ctx_destroy ctx_destroy;
	struct virtio_gpu_ctx_resource ctx_resource;
	struct {
		struct virtio_gpu_cmd_submit cmd_submit;
		unsigned char cmdbuf[];
	};
	struct virtio_gpu_get_capset get_capset;
	struct virtio_gpu_get_capset_info get_capset_info;
	struct virtio_gpu_update_cursor update_cursor;
	struct virtio_gpu_resource_assign_uuid resource_assign_uuid;
};

union virtio_gpu_resp {
	struct virtio_gpu_ctrl_hdr hdr;
	struct virtio_gpu_resp_display_info display_info;
	struct virtio_gpu_resp_capset_info capset_info;
	struct virtio_gpu_resp_capset capset;
	struct virtio_gpu_resp_resource_uuid resource_uuid;
};

#endif
