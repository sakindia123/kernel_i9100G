#!/bin/bash

if [ -e zImage ]; then
	rm zImage
fi

rm compile.log

# Set Default Path
TOP_DIR=$PWD
KERNEL_PATH="/home/sarthak/new/kernel_i9100G"

# Set toolchain and root filesystem path
TOOLCHAIN="/home/sarthak/Downloads/Toolchains/arm-2009q3/bin/arm-none-linux-gnueabi-"
ROOTFS_PATH="/home/sarthak/Downloads/initramfs"

export KBUILD_BUILD_VERSION="cranium-testkeys"
export KERNELDIR=$KERNEL_PATH

echo "Cleaning latest build"
make clean mrproper

echo "Setting up defconfig"
# Making our .config
make cranium_defconfig

echo "Compiling Kernel"
make -j8 ARCH=arm CROSS_COMPILE=$TOOLCHAIN CONFIG_INITRAMFS_SOURCE="$ROOTFS_PATH" || exit -1

# Copy Kernel Image
rm -f $KERNEL_PATH/releasetools/tar/$KBUILD_BUILD_VERSION.tar
cp -f $KERNEL_PATH/arch/arm/boot/zImage .
cp -f $KERNEL_PATH/arch/arm/boot/zImage $KERNEL_PATH/releasetools/tar

cd arch/arm/boot
tar -cf $KERNEL_PATH/arch/arm/boot/$KBUILD_BUILD_VERSION.tar ../../../zImage && ls -lh $KBUILD_BUILD_VERSION.tar

cd ../../..
cd releasetools/zip
tar -r $KBUILD_BUILD_VERSION.tar *

cp $KERNEL_PATH/arch/arm/boot/$KBUILD_BUILD_VERSION.tar $KERNEL_PATH/releasetools/tar/$KBUILD_BUILD_VERSION.tar
rm $KERNEL_PATH/arch/arm/boot/$KBUILD_BUILD_VERSION.tar
rm $KERNEL_PATH/releasetools/tar/zImage

rm vmlinux vmlinux.o .tmp_vmlinux1 .tmp_vmlinux2
