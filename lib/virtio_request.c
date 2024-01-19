#include "virtio_request.h"

#include <string.h>
#include <assert.h>
#include <stdlib.h>
#include <sys/mman.h>

#include <virgl/virglrenderer.h>

#include <linux/virtio_gpu.h>

#include "virtio_thread.h"
#include "libvirtiolo.h"
#include "error.h"
#include "iov.h"

#define CMDDB \
_X(GET_DISPLAY_INFO,        _CN,                          _RY(display_info)) \
_X(RESOURCE_CREATE_2D,      _CY(resource_create_2d),      _RN) \
_X(RESOURCE_UNREF,          _CY(resource_unref),          _RN) \
_X(SET_SCANOUT,             _CY(set_scanout),             _RN) \
_X(RESOURCE_FLUSH,          _CY(resource_flush),          _RN) \
_X(TRANSFER_TO_HOST_2D,     _CY(transfer_to_host_2d),     _RN) \
_X(RESOURCE_ATTACH_BACKING, _CY(resource_attach_backing), _RN) \
_X(RESOURCE_DETACH_BACKING, _CY(resource_detach_backing), _RN) \
_X(GET_CAPSET_INFO,         _CY(get_capset_info),         _RY(capset_info)) \
_X(GET_CAPSET,              _CY(get_capset),              _RY(capset)) \
_X(RESOURCE_ASSIGN_UUID,    _CY(resource_assign_uuid),    _RY(resource_uuid)) \
\
/* 3d commands */ \
_X(CTX_CREATE,              _CY(ctx_create),              _RN) \
_X(CTX_DESTROY,             _CY(ctx_destroy),             _RN) \
_X(CTX_ATTACH_RESOURCE,     _CY(ctx_resource),            _RN) \
_X(CTX_DETACH_RESOURCE,     _CY(ctx_resource),            _RN) \
_X(RESOURCE_CREATE_3D,      _CY(resource_create_3d),      _RN) \
_X(TRANSFER_TO_HOST_3D,     _CY(transfer_host_3d),        _RN) \
_X(TRANSFER_FROM_HOST_3D,   _CY(transfer_host_3d),        _RN) \
_X(SUBMIT_3D,               _CY(cmd_submit),              _RN) \
\
/* Cursor commands */ \
_X(MOVE_CURSOR,             _CY(update_cursor),           _RN) \
_X(UPDATE_CURSOR,           _CY(update_cursor),           _RN)

#ifndef NDEBUG
static const char *cmd_to_string(unsigned int type)
{
	switch (type) {
#define _X(n, x1, x2) case VIRTIO_GPU_CMD_ ## n: return #n ;
CMDDB
#undef _X
	default: return 0;
	}
}
#endif

// FIXME: implement one function instead of these two
static unsigned int cmd_to_rsize(unsigned int type)
{
	switch (type) {
#define _CY(t) sizeof (struct virtio_gpu_ ## t)
#define _CN sizeof(struct virtio_gpu_ctrl_hdr)
#define _X(n, rt, wt) case VIRTIO_GPU_CMD_ ## n: return rt;
CMDDB
#undef _CY
#undef _CN
#undef _X
	default: return 0;
	}
}

static unsigned int cmd_to_wsize(unsigned int type)
{
	switch (type) {
#define _RY(t) sizeof (struct virtio_gpu_resp_ ## t)
#define _RN sizeof(struct virtio_gpu_ctrl_hdr)
#define _X(n, rt, wt) case VIRTIO_GPU_CMD_ ## n: return wt;
CMDDB
#undef _RY
#undef _RN
#undef _X
	default: return 0;
	}
}

// Prototypes
#define _CY(t) struct virtio_gpu_ ## t *cmd
#define _CN struct virtio_gpu_ctrl_hdr *cmd
#define _RY(t) struct virtio_gpu_resp_ ## t *resp
#define _RN struct virtio_gpu_ctrl_hdr *resp

#define _X(n, rt, wt) static unsigned int cmd_ ## n (rt, wt);
CMDDB
#undef _X

#undef _CY
#undef _CN
#undef _RY
#undef _RN

// static unsigned int cmd_GET_DISPLAY_INFO(       struct virtio_gpu_ctrl_hdr *cmd,                struct virtio_gpu_resp_display_info *resp)  { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_RESOURCE_CREATE_2D(     struct virtio_gpu_resource_create_2d *cmd,      struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
static unsigned int cmd_RESOURCE_UNREF(         struct virtio_gpu_resource_unref *cmd,          struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
static unsigned int cmd_SET_SCANOUT(            struct virtio_gpu_set_scanout *cmd,             struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
static unsigned int cmd_RESOURCE_FLUSH(         struct virtio_gpu_resource_flush *cmd,          struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
static unsigned int cmd_TRANSFER_TO_HOST_2D(    struct virtio_gpu_transfer_to_host_2d *cmd,     struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_RESOURCE_ATTACH_BACKING(struct virtio_gpu_resource_attach_backing *cmd, struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
static unsigned int cmd_RESOURCE_DETACH_BACKING(struct virtio_gpu_resource_detach_backing *cmd, struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_GET_CAPSET_INFO(        struct virtio_gpu_get_capset_info *cmd,         struct virtio_gpu_resp_capset_info *resp)   { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
static unsigned int cmd_GET_CAPSET(             struct virtio_gpu_get_capset *cmd,              struct virtio_gpu_resp_capset *resp)        { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
static unsigned int cmd_RESOURCE_ASSIGN_UUID(   struct virtio_gpu_resource_assign_uuid *cmd,    struct virtio_gpu_resp_resource_uuid *resp) { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_CTX_CREATE(             struct virtio_gpu_ctx_create *cmd,              struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_CTX_DESTROY(            struct virtio_gpu_ctx_destroy *cmd,             struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_CTX_ATTACH_RESOURCE(    struct virtio_gpu_ctx_resource *cmd,            struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_CTX_DETACH_RESOURCE(    struct virtio_gpu_ctx_resource *cmd,            struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
static unsigned int cmd_RESOURCE_CREATE_3D(     struct virtio_gpu_resource_create_3d *cmd,      struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
static unsigned int cmd_TRANSFER_TO_HOST_3D(    struct virtio_gpu_transfer_host_3d *cmd,        struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
static unsigned int cmd_TRANSFER_FROM_HOST_3D(  struct virtio_gpu_transfer_host_3d *cmd,        struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
static unsigned int cmd_SUBMIT_3D(              struct virtio_gpu_cmd_submit *cmd,              struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
static unsigned int cmd_MOVE_CURSOR(            struct virtio_gpu_update_cursor *cmd,           struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }
static unsigned int cmd_UPDATE_CURSOR(          struct virtio_gpu_update_cursor *cmd,           struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; trace("NOT IMPLEMENTED"); return 0; }

// r - guest to device
// w - device to guest
static struct iovec *r;
static struct iovec *w;
static size_t nr;
static size_t nw;

static unsigned int cmd_GET_DISPLAY_INFO(struct virtio_gpu_ctrl_hdr *cmd, struct virtio_gpu_resp_display_info *resp)
{
	(void)cmd;

	// FIXME: where should we get the size from?
	resp->pmodes[0].r.height = 800;
	resp->pmodes[0].r.width = 1200;
	resp->pmodes[0].enabled = 1;

	trace("heigth=%u, width=%u", resp->pmodes[0].r.height, resp->pmodes[0].r.width);

	resp->hdr.type = VIRTIO_GPU_RESP_OK_DISPLAY_INFO;
	return sizeof(*resp);
}

static unsigned int cmd_RESOURCE_CREATE_2D(struct virtio_gpu_resource_create_2d *cmd, struct virtio_gpu_ctrl_hdr *resp)
{
	struct virgl_renderer_resource_create_args args;

	args.handle = cmd->resource_id;
	args.target = 2;
	args.format = cmd->format;
	args.bind = (1 << 1);
	args.width = cmd->width;
	args.height = cmd->height;
	args.depth = 1;
	args.array_size = 1;
	args.last_level = 0;
	args.nr_samples = 0;
	args.flags = VIRTIO_GPU_RESOURCE_FLAG_Y_0_TOP;
	virgl_renderer_resource_create(&args, NULL, 0);

	resp->type = VIRTIO_GPU_RESP_OK_NODATA;
	return sizeof(*resp);
}

static unsigned int cmd_RESOURCE_ATTACH_BACKING(struct virtio_gpu_resource_attach_backing *cmd, struct virtio_gpu_ctrl_hdr *resp)
{
	unsigned int i;

	struct virtio_gpu_mem_entry *mem;
	mem = calloc(cmd->nr_entries, sizeof(*mem));
	if (!mem) {
		error("calloc(mem)");
		resp->type = VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
		goto done;
	}

	// FIXME: copy data
	size_t mem_bytelen = sizeof(*mem) * cmd->nr_entries;
	size_t mem_bytelen_fact = read_from_iovec(r, nr, sizeof(*cmd), mem, sizeof(*mem) * cmd->nr_entries);
	if (mem_bytelen_fact != mem_bytelen) {
		error("read_from_iovec()");
		free(mem);
		resp->type = VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
		goto done;
	}

	struct iovec *backing;
	backing = calloc(cmd->nr_entries, sizeof(*backing));
	if (!backing) {
		error("calloc(backing)");
		free(mem);
		resp->type = VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
		goto done;
	}

	for (i = 0; i < cmd->nr_entries; i++) {
		void *b = virtio_thread_map_guest(mem[i].addr, PROT_READ | PROT_WRITE, mem[i].length);
		if (!b) {
			error("virtio_thread_map_guest()");
			goto done_unmap;
		}
		backing[i].iov_base = b;
		backing[i].iov_len = mem[i].length;
	}

	int err = virgl_renderer_resource_attach_iov(cmd->resource_id, backing, cmd->nr_entries);
	if (err) {
		error("virgl_renderer_resource_attach_iov()");
		goto done_unmap;
	}

	resp->type = VIRTIO_GPU_RESP_OK_NODATA;

done:
	return sizeof(*resp);

done_unmap:
	for (unsigned int j = 0 ; j < i; j++)
		virtio_thread_unmap_guest(backing[j].iov_base, backing[j].iov_len);
	resp->type = VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
	return sizeof(*resp);
}

static unsigned int cmd_GET_CAPSET_INFO(struct virtio_gpu_get_capset_info *cmd, struct virtio_gpu_resp_capset_info *resp)
{
	trace("index=%u", cmd->capset_index);

	if (cmd->capset_index == 0) {
		resp->capset_id = VIRTIO_GPU_CAPSET_VIRGL;
		trace();
		virgl_renderer_get_cap_set(resp->capset_id,
		                           &resp->capset_max_version,
		                           &resp->capset_max_size);
		trace();
	} else if (cmd->capset_index == 1) {
		resp->capset_id = VIRTIO_GPU_CAPSET_VIRGL2;
		trace();
		virgl_renderer_get_cap_set(resp->capset_id,
		                           &resp->capset_max_version,
		                           &resp->capset_max_size);
		trace();
	} else {
		trace();
		resp->capset_max_version = 0;
		resp->capset_max_size = 0;
	}
	resp->hdr.type = VIRTIO_GPU_RESP_OK_CAPSET_INFO;

	trace("id=%u, max_version=%u, max_size=%u", resp->capset_id, resp->capset_max_version, resp->capset_max_size);

	return sizeof(*resp);
}

static unsigned int cmd_CTX_CREATE(struct virtio_gpu_ctx_create *cmd, struct virtio_gpu_ctrl_hdr *resp)
{
	virgl_renderer_context_create(cmd->hdr.ctx_id, cmd->nlen, cmd->debug_name);

	resp->type = VIRTIO_GPU_RESP_OK_NODATA;
	return sizeof(*resp);
}

static unsigned int cmd_CTX_DESTROY(struct virtio_gpu_ctx_destroy *cmd, struct virtio_gpu_ctrl_hdr *resp)
{
	virgl_renderer_context_destroy(cmd->hdr.ctx_id);

	resp->type = VIRTIO_GPU_RESP_OK_NODATA;
	return sizeof(*resp);
}

static unsigned int cmd_CTX_ATTACH_RESOURCE(struct virtio_gpu_ctx_resource *cmd, struct virtio_gpu_ctrl_hdr *resp)
{
	virgl_renderer_ctx_attach_resource(cmd->hdr.ctx_id, cmd->resource_id);

	resp->type = VIRTIO_GPU_RESP_OK_NODATA;
	return sizeof(*resp);
}

static unsigned int cmd_CTX_DETACH_RESOURCE(struct virtio_gpu_ctx_resource *cmd, struct virtio_gpu_ctrl_hdr *resp)
{
	virgl_renderer_ctx_detach_resource(cmd->hdr.ctx_id, cmd->resource_id);

	resp->type = VIRTIO_GPU_RESP_OK_NODATA;
	return sizeof(*resp);
}

// returns resp_len
unsigned int virtio_request(struct vlo_buf *buf)
{
	assert(buf);

	size_t copied;

	unsigned char cmd_buf[1024];
	struct virtio_gpu_ctrl_hdr *cmd_hdr = (struct virtio_gpu_ctrl_hdr * )&cmd_buf;
	void *cmd = &cmd_buf;

	unsigned char resp_buf[1024];
	void *resp = &resp_buf;

	unsigned int resp_len;

	r = buf->io;
	w = buf->io + buf->ion_transmit;
	nr = buf->ion_transmit;
	nw = buf->ion - nr;

	if (nr < 1 || nw < 1) {
		error("nr=%lu, nw=%lu", nr, nw);
		return 0;
	}

	virgl_renderer_poll();
	virgl_renderer_force_ctx_0();

	// FIXME: check mapping (readonly, writeonly)

	if (r[0].iov_len >= sizeof(struct virtio_gpu_ctrl_hdr)) {
		trace("header: can use iov_base");
		cmd_hdr = r[0].iov_base;
	} else {
		trace("header: have to copy");
		copied = read_from_iovec(r, nr, 0, cmd_hdr, sizeof(struct virtio_gpu_ctrl_hdr));
		if (copied != sizeof(struct virtio_gpu_ctrl_hdr)) {
			error("command buffer is too small (hdr)");
			return 0;
		}
	}

	trace("cmd=\"%s\"", cmd_to_string(cmd_hdr->type) ?: "???");

	unsigned int rs = cmd_to_rsize(cmd_hdr->type);
	if (!rs) {
		error("unknown command %u", cmd_hdr->type);
		return 0;
	}
	unsigned int ws = cmd_to_wsize(cmd_hdr->type);
	assert(ws); // zeroes should be excluded by cmd_to_rsize()

	if (rs == sizeof(struct virtio_gpu_ctrl_hdr) || r[0].iov_len >= rs) {
		trace("cmd: can use iov_base");
		cmd = r[0].iov_base;
	} else {
		trace("cmd: have to copy");
		copied = read_from_iovec(r, nr, 0, cmd, rs);
		if (copied != rs) {
			error("command buffer is too small (cmd)");
			return 0;
		}
	}

	if (w[0].iov_len >= ws) {
		trace("resp: can use iov_base");
		resp = w[0].iov_base;
	}
	memset(resp, 0, ws);

	switch(cmd_hdr->type) {
#define _X(n, rt, wt) case VIRTIO_GPU_CMD_ ## n: resp_len = cmd_ ## n (cmd, resp); break;
CMDDB
#undef _X
	default:
		resp_len = 0;
		assert(0); // these sould be ruled out by cmd_to_?size()
	}

	if (resp == &resp_buf) {
		trace("resp: have to copy back to iov");
		copied = write_to_iovec(w, nw, 0, resp, ws);
		if (copied != ws) {
			error("responce buffer is too small");
			return 0;
		}
	}

	return resp_len;
}

