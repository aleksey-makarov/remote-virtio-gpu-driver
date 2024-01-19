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

// static unsigned int cmd_GET_DISPLAY_INFO(       struct virtio_gpu_ctrl_hdr *cmd,                struct virtio_gpu_resp_display_info *resp)  { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_RESOURCE_CREATE_2D(     struct virtio_gpu_resource_create_2d *cmd,      struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_RESOURCE_UNREF(         struct virtio_gpu_resource_unref *cmd,          struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_SET_SCANOUT(            struct virtio_gpu_set_scanout *cmd,             struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_RESOURCE_FLUSH(         struct virtio_gpu_resource_flush *cmd,          struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
   static unsigned int cmd_TRANSFER_TO_HOST_2D(    struct virtio_gpu_transfer_to_host_2d *cmd,     struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_RESOURCE_ATTACH_BACKING(struct virtio_gpu_resource_attach_backing *cmd, struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_RESOURCE_DETACH_BACKING(struct virtio_gpu_resource_detach_backing *cmd, struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_GET_CAPSET_INFO(        struct virtio_gpu_get_capset_info *cmd,         struct virtio_gpu_resp_capset_info *resp)   { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_GET_CAPSET(             struct virtio_gpu_get_capset *cmd,              struct virtio_gpu_resp_capset *resp)        { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
   static unsigned int cmd_RESOURCE_ASSIGN_UUID(   struct virtio_gpu_resource_assign_uuid *cmd,    struct virtio_gpu_resp_resource_uuid *resp) { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_CTX_CREATE(             struct virtio_gpu_ctx_create *cmd,              struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_CTX_DESTROY(            struct virtio_gpu_ctx_destroy *cmd,             struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_CTX_ATTACH_RESOURCE(    struct virtio_gpu_ctx_resource *cmd,            struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_CTX_DETACH_RESOURCE(    struct virtio_gpu_ctx_resource *cmd,            struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_RESOURCE_CREATE_3D(     struct virtio_gpu_resource_create_3d *cmd,      struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
   static unsigned int cmd_TRANSFER_TO_HOST_3D(    struct virtio_gpu_transfer_host_3d *cmd,        struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
   static unsigned int cmd_TRANSFER_FROM_HOST_3D(  struct virtio_gpu_transfer_host_3d *cmd,        struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
// static unsigned int cmd_SUBMIT_3D(              struct virtio_gpu_cmd_submit *cmd,              struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
   static unsigned int cmd_MOVE_CURSOR(            struct virtio_gpu_update_cursor *cmd,           struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }
   static unsigned int cmd_UPDATE_CURSOR(          struct virtio_gpu_update_cursor *cmd,           struct virtio_gpu_ctrl_hdr *resp)           { (void)cmd; (void)resp; error("NOT IMPLEMENTED"); return 0; }

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

	// trace("heigth=%u, width=%u", resp->pmodes[0].r.height, resp->pmodes[0].r.width);

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

static unsigned int cmd_RESOURCE_UNREF(struct virtio_gpu_resource_unref *cmd, struct virtio_gpu_ctrl_hdr *resp)
{
	struct iovec *res_iovs = NULL;
	int num_iovs = 0;

	virgl_renderer_resource_detach_iov(cmd->resource_id, &res_iovs, &num_iovs);
	if (res_iovs != NULL && num_iovs > 0) {
		for (int i = 0; i < num_iovs; i++) {
			virtio_thread_unmap_guest(res_iovs[i].iov_base, res_iovs[i].iov_len);
		}
		free(res_iovs);
	}
	virgl_renderer_resource_unref(cmd->resource_id);

	resp->type = VIRTIO_GPU_RESP_OK_NODATA;
	return sizeof(*resp);
}

static unsigned int cmd_SET_SCANOUT(struct virtio_gpu_set_scanout *cmd, struct virtio_gpu_ctrl_hdr *resp)
{
	// FIXME: pretend we have just 1 scanout right now
	if (cmd->scanout_id >= 1) {
		error("scanout_id=%u", cmd->scanout_id);
		resp->type = VIRTIO_GPU_RESP_ERR_INVALID_SCANOUT_ID;
		goto out;
	}

	if (!cmd->resource_id || !cmd->r.width || !cmd->r.height) {
		trace("FIXME: hide window: resource_id=%u, width=%u, heigth=%u", cmd->resource_id, cmd->r.width, cmd->r.height);
		resp->type = VIRTIO_GPU_RESP_OK_NODATA;
		goto out;
	}

	struct virgl_renderer_resource_info info;
	int ret = virgl_renderer_resource_get_info(cmd->resource_id, &info);
	if (ret == -1) {
		error("virgl_renderer_resource_get_info(resource_id=%u)", cmd->resource_id);
		resp->type = VIRTIO_GPU_RESP_ERR_INVALID_RESOURCE_ID;
		goto out;
	}

	trace("FIXME: set window: resource=%ux%u, box=%ux%u@%u,%u", info.width, info.height, cmd->r.width, cmd->r.height, cmd->r.x, cmd->r.y);
	resp->type = VIRTIO_GPU_RESP_OK_NODATA;

out:
	return sizeof(*resp);
}

static unsigned int cmd_RESOURCE_FLUSH(struct virtio_gpu_resource_flush *cmd, struct virtio_gpu_ctrl_hdr *resp)
{
	trace("FIXME: flush: resource_id=%u, box=%ux%u@%u,%u", cmd->resource_id, cmd->r.width, cmd->r.height, cmd->r.x, cmd->r.y);
	resp->type = VIRTIO_GPU_RESP_OK_NODATA;
	return sizeof(*resp);
}

static unsigned int cmd_RESOURCE_ATTACH_BACKING(struct virtio_gpu_resource_attach_backing *cmd, struct virtio_gpu_ctrl_hdr *resp)
{
	unsigned int i;

	trace("nr_entries=%u", cmd->nr_entries);

	struct virtio_gpu_mem_entry *mem;
	mem = calloc(cmd->nr_entries, sizeof(*mem));
	if (!mem) {
		error("calloc(mem)");
		resp->type = VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
		goto done;
	}

	size_t mem_bytelen = sizeof(*mem) * cmd->nr_entries;
	size_t mem_bytelen_read = read_from_iovec(r, nr, sizeof(*cmd), mem, sizeof(*mem) * cmd->nr_entries);
	if (mem_bytelen_read != mem_bytelen) {
		error("read_from_iovec()");
		free(mem);
		resp->type = VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
		goto done;
	}

	struct iovec *mapped;
	mapped = calloc(cmd->nr_entries, sizeof(*mapped));
	if (!mapped) {
		error("calloc(backing)");
		free(mem);
		resp->type = VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
		goto done;
	}

	for (i = 0; i < cmd->nr_entries; i++) {
		// trace("%2u mmap(0x%08u@0x%016llu)", i, mem[i].length, mem[i].addr);
		void *b = virtio_thread_map_guest(mem[i].addr, PROT_READ | PROT_WRITE, mem[i].length);
		if (!b) {
			error("virtio_thread_map_guest()");
			free(mem);
			goto done_unmap;
		}
		mapped[i].iov_base = b;
		mapped[i].iov_len = mem[i].length;
	}

	free(mem);

	int err = virgl_renderer_resource_attach_iov(cmd->resource_id, mapped, cmd->nr_entries);
	if (err) {
		error("virgl_renderer_resource_attach_iov()");
		goto done_unmap;
	}

	resp->type = VIRTIO_GPU_RESP_OK_NODATA;

done:
	return sizeof(*resp);

done_unmap:
	for (unsigned int j = 0 ; j < i; j++)
		virtio_thread_unmap_guest(mapped[j].iov_base, mapped[j].iov_len);
	free(mapped);
	resp->type = VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
	return sizeof(*resp);
}

static unsigned int cmd_RESOURCE_DETACH_BACKING(struct virtio_gpu_resource_detach_backing *cmd, struct virtio_gpu_ctrl_hdr *resp)
{
	struct iovec *res_iovs = NULL;
	int num_iovs = 0;

	virgl_renderer_resource_detach_iov(cmd->resource_id, &res_iovs, &num_iovs);
	if (res_iovs && num_iovs > 0) {
		for (int i = 0; i < num_iovs; i++) {
			virtio_thread_unmap_guest(res_iovs[i].iov_base, res_iovs[i].iov_len);
		}
		free(res_iovs);
	}

	resp->type = VIRTIO_GPU_RESP_OK_NODATA;
	return sizeof(*resp);
}

static unsigned int cmd_GET_CAPSET_INFO(struct virtio_gpu_get_capset_info *cmd, struct virtio_gpu_resp_capset_info *resp)
{
	// trace("index=%u", cmd->capset_index);

	if (cmd->capset_index == 0) {
		resp->capset_id = VIRTIO_GPU_CAPSET_VIRGL;
		virgl_renderer_get_cap_set(resp->capset_id,
		                           &resp->capset_max_version,
		                           &resp->capset_max_size);
	} else if (cmd->capset_index == 1) {
		resp->capset_id = VIRTIO_GPU_CAPSET_VIRGL2;
		virgl_renderer_get_cap_set(resp->capset_id,
		                           &resp->capset_max_version,
		                           &resp->capset_max_size);
	} else {
		resp->capset_max_version = 0;
		resp->capset_max_size = 0;
	}
	resp->hdr.type = VIRTIO_GPU_RESP_OK_CAPSET_INFO;

	// trace("id=%u, max_version=%u, max_size=%u", resp->capset_id, resp->capset_max_version, resp->capset_max_size);

	return sizeof(*resp);
}

static unsigned int cmd_GET_CAPSET(struct virtio_gpu_get_capset *cmd, struct virtio_gpu_resp_capset *resp)
{
	uint32_t max_ver, max_size;

	virgl_renderer_get_cap_set(cmd->capset_id, &max_ver, &max_size);
	if (!max_size) {
		error("virgl_renderer_get_cap_set(%u)", cmd->capset_id);
		resp->hdr.type = VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
		goto err;
	}

	if (cmd->capset_version > max_ver) {
		error("requested version %u, max versioin is %u", cmd->capset_version, max_ver);
		resp->hdr.type = VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
		goto err;
	}

	void *capset_data = malloc(max_size);
	if (!capset_data) {
		error("malloc()");
		resp->hdr.type = VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
		goto err;
	}

	virgl_renderer_fill_caps(cmd->capset_id, cmd->capset_version, capset_data);

	size_t size_written = write_to_iovec(w, nw, sizeof(struct virtio_gpu_ctrl_hdr), capset_data, max_size);
	free(capset_data);
	if (size_written != max_size) {
		error("write_to_iovec(%u): %lu", max_size, size_written);
		resp->hdr.type = VIRTIO_GPU_RESP_ERR_INVALID_PARAMETER;
		goto err;
	}

	resp->hdr.type = VIRTIO_GPU_RESP_OK_CAPSET;
	return sizeof(*resp);

err:
	return sizeof(resp->hdr);
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

static unsigned int cmd_RESOURCE_CREATE_3D(struct virtio_gpu_resource_create_3d *cmd, struct virtio_gpu_ctrl_hdr *resp)
{
	struct virgl_renderer_resource_create_args args;

	args.handle = cmd->resource_id;
	args.target = cmd->target;
	args.format = cmd->format;
	args.bind = cmd->bind;
	args.width = cmd->width;
	args.height = cmd->height;
	args.depth = cmd->depth;
	args.array_size = cmd->array_size;
	args.last_level = cmd->last_level;
	args.nr_samples = cmd->nr_samples;
	args.flags = cmd->flags;
	virgl_renderer_resource_create(&args, NULL, 0);

	resp->type = VIRTIO_GPU_RESP_OK_NODATA;
	return sizeof(*resp);
}

static unsigned int cmd_SUBMIT_3D(struct virtio_gpu_cmd_submit *cmd, struct virtio_gpu_ctrl_hdr *resp)
{
	void *buf = malloc(cmd->size);
	if (!buf) {
		error("malloc()");
		resp->type = VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
		goto out;
	}

	size_t s = read_from_iovec(r, nr, sizeof(*cmd), buf, cmd->size);
	if (s != cmd->size) {
		error("read_from_ioved(%u): %lu", cmd->size, s);
		resp->type = VIRTIO_GPU_RESP_ERR_OUT_OF_MEMORY;
		goto out_free;
	}

	virgl_renderer_submit_cmd(buf, cmd->hdr.ctx_id, cmd->size / 4);

	resp->type = VIRTIO_GPU_RESP_OK_NODATA;

out_free:
	free(buf);
out:
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
		cmd_hdr = r[0].iov_base;
	} else {
		trace("header: have to copy");
		copied = read_from_iovec(r, nr, 0, cmd_hdr, sizeof(struct virtio_gpu_ctrl_hdr));
		if (copied != sizeof(struct virtio_gpu_ctrl_hdr)) {
			error("command buffer is too small (hdr)");
			return 0;
		}
	}

	trace("%s%s", cmd_to_string(cmd_hdr->type) ?: "???", cmd_hdr->flags & VIRTIO_GPU_FLAG_FENCE ? " F": "");

	unsigned int rs = cmd_to_rsize(cmd_hdr->type);
	if (!rs) {
		error("unknown command %u", cmd_hdr->type);
		return 0;
	}
	unsigned int ws = cmd_to_wsize(cmd_hdr->type);
	assert(ws); // zeroes should be excluded by cmd_to_rsize()

	if (rs == sizeof(struct virtio_gpu_ctrl_hdr) || r[0].iov_len >= rs) {
		cmd = r[0].iov_base;
	} else {
		trace("cmd: have to copy");
		copied = read_from_iovec(r, nr, 0, cmd, rs);
		if (copied != rs) {
			error("command buffer is too small (cmd)");
			return 0;
		}
	}

	if (w[0].iov_len >= ws)
		resp = w[0].iov_base;
	memset(resp, 0, ws);

	switch(cmd_hdr->type) {
#define _X(n, rt, wt) case VIRTIO_GPU_CMD_ ## n: resp_len = cmd_ ## n (cmd, resp); break;
CMDDB
#undef _X
	default:
		resp_len = 0;
		assert(0); // these sould be ruled out by cmd_to_?size()
	}

	if (cmd_hdr->flags & VIRTIO_GPU_FLAG_FENCE) {
		// FIXME: reply only on command processing completion (?)
		struct virtio_gpu_ctrl_hdr *resp_hdr = resp;
		resp_hdr->flags |= VIRTIO_GPU_FLAG_FENCE;
		resp_hdr->fence_id = cmd_hdr->fence_id;
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
