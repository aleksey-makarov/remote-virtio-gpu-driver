#!/usr/bin/env bash

# set -x

if [ "$1" == "" ] ; then
	target=modules
else
	target="$1"
fi

# if [ -z ${VIRTIO_LOOPBACK_DRIVER_KERNEL+x} ]; then
# 	# KERNEL_VERSION="6.5.5+"
# 	# LINUX_KERNEL_DIR=$(readlink ./linux.install)/lib/modules/"${KERNEL_VERSION}"/build
# 	# echo "linux from ${LINUX_KERNEL_DIR}"
	LINUX_KERNEL_DIR=$(realpath ./linux)
	echo "*** make $target"
	make -C "${LINUX_KERNEL_DIR}" M="$PWD" "$target"
# else
# 	echo "linux from ${VIRTIO_LOOPBACK_DRIVER_KERNEL}"
# 	echo "*** make $target"
# 	make -C "${VIRTIO_LOOPBACK_DRIVER_KERNEL}" M="$PWD" "$target"
# fi

if [ -f virtio-lo.ko ] ; then
	mkdir -p xchg
	cp virtio-lo.ko ./xchg
fi
