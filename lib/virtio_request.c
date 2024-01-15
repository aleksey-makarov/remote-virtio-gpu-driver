#include "virtio_request.h"

#include <assert.h>

#include "libvirtiolo.h"
#include "virtio_gpu_cmd.h"
#include "error.h"
#include "rvgpu-iov.h"

#ifndef NDEBUG
static const char *cmd_to_string(unsigned int type)
{
#define _X(n) case VIRTIO_GPU_CMD_ ## n: return #n ;

	switch (type) {
	_X(GET_DISPLAY_INFO)
	_X(RESOURCE_CREATE_2D)
	_X(RESOURCE_UNREF)
	_X(SET_SCANOUT)
	_X(RESOURCE_FLUSH)
	_X(TRANSFER_TO_HOST_2D)
	_X(RESOURCE_ATTACH_BACKING)
	_X(RESOURCE_DETACH_BACKING)
	_X(GET_CAPSET_INFO)
	_X(GET_CAPSET)

	/* 3d commands */
	_X(CTX_CREATE)
	_X(CTX_DESTROY)
	_X(CTX_ATTACH_RESOURCE)
	_X(CTX_DETACH_RESOURCE)
	_X(RESOURCE_CREATE_3D)
	_X(TRANSFER_TO_HOST_3D)
	_X(TRANSFER_FROM_HOST_3D)
	_X(SUBMIT_3D)

	/* Cursor commands */
	_X(MOVE_CURSOR)
	_X(UPDATE_CURSOR)

	default: return NULL;
	}

#undef _X
}
#endif

static unsigned int test_virgl_get_capset_info(struct virtio_gpu_ctrl_hdr *hdr,
					       struct iovec *w, unsigned int nw)
{
	(void)hdr;
	(void)w;
	(void)nw;

	return 0;
}

static unsigned int test_virgl_get_capset(struct virtio_gpu_ctrl_hdr *hdr,
					  struct iovec *w, unsigned int nw)
{
	(void)hdr;
	(void)w;
	(void)nw;

	return 0;
}

static unsigned int ctrl(struct virtio_gpu_ctrl_hdr *hdr,
			 struct iovec *r, unsigned int nr,
			 struct iovec *w, unsigned int nw)
{
	(void)r;
	(void)nr;

	trace("cmd=\"%s\"", cmd_to_string(hdr->type) ?: "???");

	unsigned int resp_len = sizeof(struct virtio_gpu_ctrl_hdr);

	switch(hdr->type) {
	case VIRTIO_GPU_CMD_GET_CAPSET_INFO:
		resp_len = test_virgl_get_capset_info(hdr, w, nw);
		break;
	case VIRTIO_GPU_CMD_GET_CAPSET:
		resp_len = test_virgl_get_capset(hdr, w, nw);
		break;
		break;
	default:
		hdr->type = VIRTIO_GPU_RESP_ERR_UNSPEC;
		hdr->flags = 0;
		break;
	}

	// resp_hdr->type = VIRTIO_GPU_RESP_OK_NODATA;
	// fence_id;
	// ctx_id;
	// ring_idx;

	return resp_len;
}

// returns resp_len
unsigned int virtio_request(struct vlo_buf *buf)
{
	assert(buf);

	// r - guest to device
	// w - device to guest
	struct iovec *r = buf->io;
	struct iovec *w = buf->io + buf->ion_transmit;
	size_t nr = buf->ion_transmit;
	size_t nw = buf->ion - nr;
	size_t copied;

	struct virtio_gpu_ctrl_hdr resp_hdr_buf;
	struct virtio_gpu_ctrl_hdr *resp_hdr;

	unsigned int resp_len;

	if (nr < 1 || nw < 1) {
		error("nr=%lu, nw=%lu", nr, nw);
		return 0;
	}

	// FIXME: check mapping (readonly, writeonly)

	if (w[0].iov_len >= sizeof(struct virtio_gpu_ctrl_hdr))
		resp_hdr = w[0].iov_base;
	else
		resp_hdr = &resp_hdr_buf;

	copied = copy_from_iov(r, nr, resp_hdr, sizeof(*resp_hdr));
	if (copied != sizeof(*resp_hdr)) {
		error("command buffer is too small");
		return 0;
	}

	resp_len = ctrl(resp_hdr, r, nr, w, nw);

	if (resp_hdr == &resp_hdr_buf) {
		copied = copy_to_iov(w, nw, &resp_hdr_buf, sizeof(resp_hdr_buf));
		if (copied != sizeof(resp_hdr_buf)) {
			error("responce buffer is too small");
			return 0;
		}
	}

	return resp_len;
}

