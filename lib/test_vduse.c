#include <stdlib.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <libnl3/netlink/netlink.h>
#include <libnl3/netlink/cache.h>
#include <libnl3/netlink/route/link.h>
#include <libnl3/netlink/genl/genl.h>
#include <libnl3/netlink/genl/ctrl.h>

#include <linux/vdpa.h>
#include <linux/vduse.h>

#define TRACE_FILE "test_vduse.c"
#include "trace.h"

#define DRIVER_NAME "test"

static int vduse_message_handler(int dev_fd)
{
	int len;
	struct vduse_dev_request req;
	struct vduse_dev_response resp;

	len = read(dev_fd, &req, sizeof(req));
	if (len != sizeof(req))
		return -1;

	resp.request_id = req.request_id;

	switch (req.type) {
		/* handle different types of messages */
	}

	len = write(dev_fd, &resp, sizeof(resp));
	if (len != sizeof(resp))
		return -1;

	return 0;
}

static int netlink_add_vduse(const char *name, enum vdpa_command cmd)
{
	struct nl_sock *nlsock;
	struct nl_msg *msg;
	int famid;

	nlsock = nl_socket_alloc();
	if (!nlsock)
		return -ENOMEM;

	if (genl_connect(nlsock))
		goto free_sock;

	famid = genl_ctrl_resolve(nlsock, VDPA_GENL_NAME);
	if (famid < 0)
		goto close_sock;

	msg = nlmsg_alloc();
	if (!msg)
		goto close_sock;

	if (!genlmsg_put(msg, NL_AUTO_PORT, NL_AUTO_SEQ, famid, 0, 0, cmd, 0))
		goto nla_put_failure;

	NLA_PUT_STRING(msg, VDPA_ATTR_DEV_NAME, name);
	if (cmd == VDPA_CMD_DEV_NEW)
		NLA_PUT_STRING(msg, VDPA_ATTR_MGMTDEV_DEV_NAME, "vduse");

	if (nl_send_sync(nlsock, msg))
		goto close_sock;

	nl_close(nlsock);
	nl_socket_free(nlsock);

	return 0;
nla_put_failure:
	nlmsg_free(msg);
close_sock:
	nl_close(nlsock);
free_sock:
	nl_socket_free(nlsock);
	return -1;
}

static int perm_to_prot(uint8_t perm)
{
        int prot = 0;

        switch (perm) {
        case VDUSE_ACCESS_WO:
                prot |= PROT_WRITE;
                break;
        case VDUSE_ACCESS_RO:
                prot |= PROT_READ;
                break;
        case VDUSE_ACCESS_RW:
                prot |= PROT_READ | PROT_WRITE;
                break;
        }

        return prot;
}

static void *iova_to_va(int dev_fd, uint64_t iova, uint64_t *len)
{
        int fd;
        void *addr;
        size_t size;
        struct vduse_iotlb_entry entry;

        entry.start = iova;
        entry.last = iova;

        /*
         * Find the first IOVA region that overlaps with the specified
         * range [start, last] and return the corresponding file descriptor.
         */
        fd = ioctl(dev_fd, VDUSE_IOTLB_GET_FD, &entry);
        if (fd < 0)
                return NULL;

        size = entry.last - entry.start + 1;
        *len = entry.last - iova + 1;
        addr = mmap(0, size, perm_to_prot(entry.perm), MAP_SHARED,
                    fd, entry.offset);
        close(fd);
        if (addr == MAP_FAILED)
                return NULL;

        /*
         * Using some data structures such as linked list to store
         * the iotlb mapping. The munmap(2) should be called for the
         * cached mapping when the corresponding VDUSE_UPDATE_IOTLB
         * message is received or the device is reset.
         */

        return addr + iova - entry.start;
}

int main(int argc, char **argv)
{
	(void)argc;
	(void)argv;

	trace("Hello world");

	vduse_message_handler(0);
	netlink_add_vduse(DRIVER_NAME, VDPA_CMD_DEV_NEW);
	iova_to_va(0, 0, NULL);

	exit(EXIT_SUCCESS);
}
