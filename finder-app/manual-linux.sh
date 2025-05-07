#!/bin/bash
# Script outline to install and build kernel.
# Author: Siddhant Jajoo.

set -e
set -u

OUTDIR=/tmp/aeld
KERNEL_REPO=git://git.kernel.org/pub/scm/linux/kernel/git/stable/linux-stable.git
KERNEL_VERSION=v5.15.163
BUSYBOX_VERSION=1_33_1
FINDER_APP=$(realpath $(dirname $0))
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
# If OUTDIR already exists next command won't do any thing
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
    # I will use a download version tarball so this command is not needed
    git checkout ${KERNEL_VERSION}

    # TODO: Add your kernel build steps here
    # [1] Deep clean a pre-exist build artifacts
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} mrproper
    # Default defconfig Configure for our “virt” arm dev board
    make ARCH=${ARCH}  CROSS_COMPILE=${CROSS_COMPILE} defconfig
    # vmlinux
    make -j8 ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} all
    # Skipping modules for this simple assignment
    # make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} modules
    # Build the devicetree
    make ARCH=${ARCH} CROSS_COMPILE=${CROSS_COMPILE} dtbs
fi

echo "Adding the Image in outdir"
# For assignment test requirment copying Image file to OUTDIR
cp ${OUTDIR}/linux-stable/arch/${ARCH}/boot/Image ${OUTDIR}

echo "Creating the staging directory for the root filesystem"
cd "$OUTDIR"
if [ -d "${OUTDIR}/rootfs" ]
then
	echo "Deleting rootfs directory at ${OUTDIR}/rootfs and starting over"
    sudo rm  -rf ${OUTDIR}/rootfs
fi

# TODO: Create necessary base directories
mkdir -p "${OUTDIR}/rootfs/" && cd "${OUTDIR}/rootfs/"
mkdir -p bin dev etc home lib lib64 proc sbin sys tmp var
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
#${CROSS_COMPILE}readelf -a /bin/busybox | grep "program interpreter"
#${CROSS_COMPILE}readelf -a /bin/busybox | grep "Shared library"
# TODO: Add library dependencies to rootfs

cd "${OUTDIR}/rootfs/"
SYSROOT=$(${CROSS_COMPILE}gcc -print-sysroot )
cp ${SYSROOT}/lib/ld-linux-aarch64.so.1 lib
cp ${SYSROOT}/lib64/libm.so.6 lib64
cp ${SYSROOT}/lib64/libresolv.so.2 lib64
cp ${SYSROOT}/lib64/libc.so.6 lib64

# TODO: Make device nodes

sudo mknod -m 666 dev/null c 1 3
sudo mknod -m 600 dev/console c 5 1

# TODO: Clean and build the writer utility
cd $FINDER_APP
pwd
ls
make clean
make ARCH=$ARCH CROSS_COMPILE=$CROSS_COMPILE

# TODO: Copy the finder related scripts and executables to the /home directory
# on the target rootfs

cd ${OUTDIR}/rootfs/home
mkdir conf
cp ${FINDER_APP}/writer .
cp "${FINDER_APP}/finder.sh" .
cp ${FINDER_APP}/finder-test.sh .
cp ${FINDER_APP}/../conf/username.txt ./conf
cp ${FINDER_APP}/../conf/assignment.txt ./conf
cp ${FINDER_APP}/autorun-qemu.sh .

# TODO: Chown the root directory

cd ${OUTDIR}/rootfs
sudo chown -R root:root *

# TODO: Create initramfs.cpio.gz

find . | cpio -H newc -ov --owner root:root > ../initramfs.cpio
cd ..
gzip initramfs.cpio