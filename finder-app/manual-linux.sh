#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP_DIR=$(realpath $(dirname $0))
ARCH=arm64
export PATH="/usr/local/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/bin:$PATH"
CROSS_COMPILE=aarch64-none-linux-gnu-

if [ $# -lt 1 ]
then
	echo "Using default directory ${OUTDIR} for output"
else
	OUTDIR=$1
	echo "Using passed directory ${OUTDIR} for output"
fi

mkdir -p ${OUTDIR}

cd "$OUTDIR"
if [ ! -d "${OUTDIR}/linux-stable" ]; then
    #Clone only if the repository does not exist.
	echo "CLONING GIT LINUX STABLE VERSION ${KERNEL_VERSION} IN ${OUTDIR}"
	git clone ${KERNEL_REPO} --depth 1 --single-branch --branch ${KERNEL_VERSION}
fi
if [ ! -e ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ]; then
    cd linux-stable
    echo "Checking out version ${KERNEL_VERSION}"
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    echo "STARTING MY KERNEL BUILD"
    pwd
    sudo apt-get install libssl-dev
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE mrproper
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE defconfig
    make -j6 ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE all
    # make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE modules
    make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE dtbs

    echo "END OF MY KERNEL BUILD"
fi

echo "Adding the Image in outdir"

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir "$OUTDIR/rootfs"
cd "$OUTDIR/rootfs"
mkdir -p bin etc home lib lib64 proc sbin sys tmp usr var dev
mkdir -p usr/bin usr/lib usr/sbin
mkdir -p var/log


cd "$OUTDIR"
if [ ! -d "${OUTDIR}/busybox" ]
then
git clone git://busybox.net/busybox.git
    cd busybox
    git checkout ${BUSYBOX_VERSION}
    # TODO:  Configure busybox
    make distclean
    make defconfig
else
    cd busybox
fi

# TODO: Make and install busybox
make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE}
make CONFIG_PREFIX=${OUTDIR}/rootfs ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} install
echo "Library dependencies"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "program interpreter"
${CROSS_COMPILE}readelf -a ${OUTDIR}/rootfs/bin/busybox | grep "Shared library"

# TODO: Add library dependencies to rootfs
cp /usr/local/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib64/libm.so.6 /tmp/aeld/rootfs/lib64/libm.so.6
cp /usr/local/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib64/libresolv.so.2 /tmp/aeld/rootfs/lib64/libresolv.so.2
cp /usr/local/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib64/libc.so.6 /tmp/aeld/rootfs/lib64/libc.so.6
cp /usr/local/arm-gnu-toolchain-13.3.rel1-x86_64-aarch64-none-linux-gnu/aarch64-none-linux-gnu/libc/lib/ld-linux-aarch64.so.1 /tmp/aeld/rootfs/lib/ld-linux-aarch64.so.1

# TODO: Make device nodes
if [ ! -e "${OUTDIR}/rootfs//dev/null" ]
then
    sudo mknod -m 666 ${OUTDIR}/rootfs/dev/null c 1 3
fi
if [ ! -e "${OUTDIR}/rootfs/dev/console" ]
then
    sudo mknod -m 666 ${OUTDIR}/rootfs/dev/console c 5 1
fi
# TODO: Clean and build the writer utility
cd /home/frba/assignments-3-and-later-FrbaEnabl/finder-app
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE clean
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE
# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs
cp -r /home/frba/assignments-3-and-later-FrbaEnabl/finder-app ${OUTDIR}/rootfs/home/finder-app
# TODO: Chown the root directory
sudo chown -R frba:frba ${OUTDIR}

# TODO: Create initramfs.cpio.gz
cd "$OUTDIR/rootfs"
find . | cpio -H newc -ov --owner root:root > ${OUTDIR}/rootfs/initramfs.cpio
gzip -f initramfs.cpio