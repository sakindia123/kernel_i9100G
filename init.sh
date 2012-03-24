rm .config
make cranium_defconfig

echo "============================================== CRANIUM KERNEL COMPILER ==============================================================="

echo "Making Modules"
make modules

echo "Collecing Compiled Modules"
cp crypto/pcbc.ko modules
cp drivers/bluetooth/bthid/bthid.ko modules
cp drivers/media/video/gspca/gspca_main.ko modules
cp drivers/media/video/omapgfx/gfx_vout_mod.ko modules
cp drivers/net/wireless/bcm4330/dhd.ko modules
cp drivers/scsi/scsi_wait_scan.ko modules
cp drivers/staging/omap_hsi/hsi_char.ko modules
cp drivers/staging/ti-st/bt_drv.ko modules
cp drivers/staging/ti-st/fm_drv.ko modules
cp drivers/staging/ti-st/gps_drv.ko modules
cp drivers/staging/ti-st/st_drv.ko modules

echo "Stripping Modules"
cd  modules
./strip.sh

echo "Copying Modules to Initramfs Directory"
cp pcbc.ko bthid.ko gspca_main.ko gfx_vout_mod.ko dhd.ko scsi_wait_scan.ko hsi_char.ko bt_drv.ko fm_drv.ko gps_drv.ko st_drv.ko /home/sarthak/kernel_i9100G/initramfs/lib/modules

echo "Initiating Kernel Compilation"
cd ../
./build.sh


