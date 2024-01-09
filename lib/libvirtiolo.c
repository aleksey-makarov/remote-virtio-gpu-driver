#include <sys/eventfd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <assert.h>
#include <stdatomic.h>
#include <time.h> // FIXME
#include <sys/mman.h>

#include <linux/virtio_lo.h>

#include "libvirtiolo.h"
#define TRACE_FILE "libvirtiolo.c"
#include "trace.h"

#define VIRTIO_LO_PATH "/dev/virtio-lo"

struct vlo_vring {
	struct vring vring;
	int kick_fd;
	uint16_t last_avail_idx;
};

struct vlo {
	int fd;
	int config_fd;

	unsigned int idx;

	unsigned int config_size;

	unsigned int vringsn;
	struct vlo_vring vrings[];
};

static inline uint64_t align_to_page_down(uint64_t a)
{
	return a & (~4095ull);
}

static inline uint64_t align_to_page_up(uint64_t a)
{
	return align_to_page_down(a + 4095ull);
}

static void *map_guest(int fd, uint64_t gpa, int prot, size_t size)
{
	uint64_t realpa = align_to_page_down(gpa);
	size_t realsize = align_to_page_up(gpa + size) - realpa;
	void *ret;

	// trace("mapping %lu @ %lu", size, gpa);

	ret = mmap(NULL, realsize, prot, MAP_SHARED, fd, (off_t)realpa);
	if (ret == MAP_FAILED)
		return NULL;

	ret = (char *)ret + (gpa - realpa);
	return ret;
}

static void unmap_guest(void *addr, size_t size)
{
	uintptr_t ai = (uintptr_t)addr;
	uintptr_t realpa = align_to_page_down(ai);
	size_t realsize = align_to_page_up(ai + size) - realpa;

	munmap((void *)realpa, realsize);
}

#define SIZE_desc(s)  (   16 * (s))
#define SIZE_avail(s) (6 + 2 * (s))
#define SIZE_used(s)  (6 + 8 * (s))

static void unmap_vring(struct vring *vr)
{
	unmap_guest(vr->desc,  SIZE_desc(vr->num));
	unmap_guest(vr->avail, SIZE_avail(vr->num));
	unmap_guest(vr->used,  SIZE_used(vr->num));
}

static int map_vring(int fd, struct vring *vr, struct virtio_lo_qinfo *q)
{

	vr->desc  = map_guest(fd, q->desc,  PROT_READ,              SIZE_desc(vr->num));
	vr->avail = map_guest(fd, q->avail, PROT_READ,              SIZE_avail(vr->num));
	vr->used  = map_guest(fd, q->used,  PROT_READ | PROT_WRITE, SIZE_used(vr->num));
	if (!vr->desc || !vr->avail || !vr->used) {
		trace_err("%p %p %p", vr->desc, vr->avail, vr->used);
		unmap_vring(vr);
		return -1;
	}

	return 0;
}

static int map_vrings(struct vlo *vl, struct virtio_lo_qinfo *q)
{
	unsigned int i, j;

	for (i = 0; i < vl->vringsn; i++) {
		struct vring *vr = &vl->vrings[i].vring;
		vr->num = q->size;
		if (map_vring(vl->fd, vr, q + i)) {
			trace_err("error mapping ring %u", i);
			goto error;
		}
	}

	return 0;

error:
	for (j = 0 ; j < i; j++) {
		struct vring *vr = &vl->vrings[j].vring;
		unmap_vring(vr);
	}

	return -1;
}

static void unmap_vrings(struct vlo *vl)
{
	unsigned int i;

	for (i = 0; i < vl->vringsn; i++)
		unmap_vring(&vl->vrings[i].vring);
}

struct vlo *vlo_init(
	unsigned int device_id,
	unsigned int vendor_id,
	struct virtio_lo_qinfo *qinfos,
	unsigned int qinfosn,
	void *config,
	unsigned int confign,
	uint64_t *features
) {
	int err;
	unsigned int i;

	struct vlo *ret = malloc(sizeof(struct vlo) + sizeof(struct vlo_vring) * qinfosn);
	if (!ret) {
		trace_err("calloc()");
		goto error;
	}
	ret->vringsn = qinfosn;
	ret->config_size = confign;

	trace("open(%s)", VIRTIO_LO_PATH);
	ret->fd = open(VIRTIO_LO_PATH, O_RDWR);
	if (ret->fd == -1) {
		trace_err_p("open(%s)", VIRTIO_LO_PATH);
		goto error_free;
	}

	ret->config_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (ret->config_fd == -1) {
		trace_err_p("eventfd(config_fd)");
		goto error_close;
	}

	for (i = 0; i < qinfosn; i++) {
		ret->vrings[i].kick_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
		if (ret->vrings[i].kick_fd == -1) {
			trace_err_p("eventfd(kick_fd)");
			unsigned int j;
			for (j = 0; j < i; j++)
				close(ret->vrings[i].kick_fd);
			goto error_close_config_fd;
		}
		qinfos[i].kickfd = ret->vrings[i].kick_fd;
	}

	struct virtio_lo_devinfo info = {
		.idx = 0,                     /* __u32 OUT */
		.device_id = device_id,       /* __u32 IN */
		.vendor_id = vendor_id,       /* __u32 IN */
		.nqueues = qinfosn,           /* __u32 IN */
		.features = features ? *features : 0, /* __u64 IN/OUT */
		.config_size = confign,       /* __u32 IN */
		.config_kick = ret->config_fd,/* __s32 IN */
		.card_index = 0,              /* __s32 IN */
		.padding = 0,                 /* __u32 IN */
		.config = (uint8_t *)&config, /* __u8* IN/OUT */
		.qinfo = qinfos,              /* struct virtio_lo_qinfo* IN/OUT */
	};

	trace("ioctl(VIRTIO_LO_ADDDEV)");
	if (ioctl(ret->fd, VIRTIO_LO_ADDDEV, &info)) {
		trace_err_p("ioctl(VIRTIO_LO_ADDDEV)");
		goto error_close_kick_fd;
	}

	ret->idx = info.idx;

	trace("output index: 0x%u", info.idx);
	trace("output features: 0x%llu", info.features);
	for (i = 0; i < qinfosn; i++)
		trace("%u: size=%u, desc=0x%llu, avail=0x%llu, used=0x%llu",
			i, qinfos[i].size, qinfos[i].desc, qinfos[i].avail, qinfos[i].used
		);

	trace("map_queues()");
	err = map_vrings(ret, qinfos);
	if (err) {
		trace_err("maps_vrings()");
		goto error_unmap_queues;
	}

	if (features)
		*features = info.features;

	return ret;

error_unmap_queues:
	unmap_vrings(ret);
error_close_kick_fd:
	for (i = 0; i < qinfosn; i++)
		close(ret->vrings[i].kick_fd);
error_close_config_fd:
	close(ret->config_fd);
error_close:
	close(ret->fd);
error_free:
	free(ret);
error:
	return NULL;
}

void vlo_done(struct vlo *v)
{
	unsigned int i;

	trace("ioctl(VIRTIO_LO_DELDEV, %u)", v->idx);
	if (ioctl(v->fd, VIRTIO_LO_DELDEV, &v->idx))
		trace_err_p("ioctl(VIRTIO_LO_DELDEV)");

	for (i = 0; i < v->vringsn; i++)
		close(v->vrings[i].kick_fd);
	close(v->config_fd);
	close(v->fd);
	free(v);
}

bool vlo_buf_is_available(struct vlo *vl, unsigned int queue)
{
	assert(queue < vl->vringsn);
	struct vlo_vring *vr = vl->vrings + queue;

	// trace("%u: last avail=%hu, idx=%hu", queue, vr->last_avail_idx, vr->vring.avail->idx);

	return vr->last_avail_idx != vr->vring.avail->idx;
}

struct vlo_buf *vlo_buf_get(struct vlo *vl, unsigned int queue)
{
	unsigned i, j;

	assert(queue < vl->vringsn);
	assert(vlo_buf_is_available(vl, queue));

	struct vlo_vring *vr = vl->vrings + queue;

	atomic_thread_fence(memory_order_seq_cst);

	uint16_t idx = vr->vring.avail->ring[vr->last_avail_idx % vr->vring.num];
	struct vring_desc d = vr->vring.desc[idx % vr->vring.num];

	vr->last_avail_idx++;

	unsigned int ion = 1;
	unsigned int ion_transmit = 0;

	if (!(d.flags & VRING_DESC_F_WRITE))
		ion_transmit++;

	// trace("first buffer: %u@0x%llx for %s", d.len, d.addr, d.flags & VRING_DESC_F_WRITE ? "WRITE" : "READ");
	// trace("ion=%u, ion_transmit=%u", ion, ion_transmit);

	struct vring_desc di = d;
	while (di.flags & VRING_DESC_F_NEXT) {
		di = vr->vring.desc[di.next % vr->vring.num];

		// trace("buffer: %u@0x%llx for %s", di.len, di.addr, di.flags & VRING_DESC_F_WRITE ? "WRITE" : "READ");

		if (ion_transmit == ion++) {
			if (!(di.flags & VRING_DESC_F_WRITE)) {
				ion_transmit++;
			}
		} else {
			if (!(di.flags & VRING_DESC_F_WRITE)) {
				trace_err("read buffers follow write buffers - not implemented");
				return NULL;
			}
		}

		// trace("ion=%u, ion_transmit=%u", ion, ion_transmit);
	}

	struct vlo_buf *req = malloc(sizeof(struct vlo_buf) + ion * sizeof(struct iovec));
	if (!req) {
		trace_err("malloc()");
		return NULL;
	}

	req->idx = idx;
	req->vr = vr;
	req->ion = ion;
	req->ion_transmit = ion_transmit;

	for (i = 0, di = d; i < ion; i++) {

		// trace("mapping %u@0x%llx for %s", di.len, di.addr, di.flags & VRING_DESC_F_WRITE ? "WRITE" : "READ");

		req->io[i].iov_len = di.len;
		req->io[i].iov_base = map_guest(vl->fd, di.addr, di.flags & VRING_DESC_F_WRITE ? (PROT_WRITE | PROT_READ) : PROT_READ, di.len);
		if (!req->io[i].iov_base) {
			trace_err("map_guest()");
			goto error;
		}

		di = vr->vring.desc[di.next % vr->vring.num];
	}

	assert( req->ion > 0 );
	assert( req->ion >= req->ion_transmit );

	// trace("new req %p", req);
	return req;

error:
	for (j = 0; j < i; j++) {
		unmap_guest(req->io[i].iov_base, req->io[i].iov_len);
	}

	free(req);

	return NULL;
}

void vlo_buf_put(struct vlo_buf *req, unsigned int resp_len)
{
	// trace("send resp %p len=%u", req, resp_len);

	struct vlo_vring *vr = req->vr;

	uint16_t idx = atomic_load((atomic_ushort *)&vr->vring.used->idx);

	struct vring_used_elem *el = &vr->vring.used->ring[idx % vr->vring.num];

	struct timespec barrier_delay = { .tv_nsec = 10 };
	size_t i;

	for (i = 0; i < req->ion; i++)
		unmap_guest(req->io[i].iov_base, req->io[i].iov_len);

	/* FIXME: Without this delay, kernel crashes in virtio-gpu driver
	 * most probable cause is race condition.
	 * Remove delay when race condition is fixed properly.
	 */
	clock_nanosleep(CLOCK_MONOTONIC, 0, &barrier_delay, NULL);

	atomic_store((atomic_uint *)&el->len, resp_len);
	atomic_store((atomic_uint *)&el->id, req->idx);
	idx++;
	atomic_store((atomic_ushort *)&vr->vring.used->idx, idx);
	free(req);
}

int vlo_kick(struct vlo *vl, int queue)
{
	int err;

	struct virtio_lo_kick k = {
		.idx = vl->idx,
		.qidx = queue,
	};

	// trace("ioctl(VIRTIO_LO_KICK, %d)", queue);
	err = ioctl(vl->fd, VIRTIO_LO_KICK, &k);
	if (err) {
		trace_err_p("ioctl(VIRTIO_LO_KICK)");
		return -1;
	}

	return 0;
}

int vlo_config_get(struct vlo *vl, void *config, unsigned int config_length)
{
	int err;

	assert(config);
	assert(config_length > vl->config_size);

	struct virtio_lo_config cfg = {
		.idx = vl->idx,
		.config = config,
		.len = vl->config_size
	};

	trace("ioctl(VIRTIO_LO_GCONF)");
	err = ioctl(vl->fd, VIRTIO_LO_GCONF, &cfg);
	if (err)
		trace_err_p("ioctl(VIRTIO_LO_GCONF)");

	return err;
}

int vlo_config_set(struct vlo *vl, void *config, unsigned int config_length)
{
	int err;

	assert(config);
	assert(config_length > vl->config_size);

	struct virtio_lo_config cfg = {
		.idx = vl->idx,
		.config = config,
		.len = vl->config_size
	};

	trace("ioctl(VIRTIO_LO_SCONF)");
	err = ioctl(vl->fd, VIRTIO_LO_SCONF, &cfg);
	if (err)
		trace_err_p("ioctl(VIRTIO_LO_SCONF)");

	return err;
}

int vlo_epoll_get_config(struct vlo *vl)
{
	assert(vl);
	return vl->config_fd;
}

int vlo_epoll_get_kick(struct vlo *vl, unsigned int queue)
{
	assert(vl);
	assert(queue < vl->vringsn);
	return vl->vrings[queue].kick_fd;
}
