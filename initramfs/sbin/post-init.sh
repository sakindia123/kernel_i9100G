#!/sbin/busybox sh

# sdcard isn't mounted at this point, mount it for now
/sbin/busybox mount -o rw /dev/block/mmcblk0p11 /mnt/sdcard

if [ ! -f /data/cranium/efsbackup.tar.gz ];
then
  mkdir /data/cranium
  chmod 777 /data/cranium
  /sbin/busybox tar zcvf /data/cranium/efsbackup.tar.gz /efs
  /sbin/busybox cat /dev/block/mmcblk0p1 > /data/cranium/efsdev-mmcblk0p1.img
  /sbin/busybox gzip /data/cranium/efsdev-mmcblk0p1.img
  /sbin/busybox cp /data/cranium/efs* /mnt/sdcard
fi

# install custom boot animation
if [ -f /mnt/sdcard/cranium/bootanimation.zip ]; then
  /sbin/busybox mount -o rw,remount /dev/block/mmcblk0p9 /system
  /sbin/busybox rm /system/media/bootanimation.zip
  /sbin/busybox cp /mnt/sdcard/misc/bootanimation.zip /system/media/bootanimation.zip
  /sbin/busybox rm /mnt/sdcard/misc/bootanimation.zip
  /sbin/busybox mount -o ro,remount /dev/block/mmcblk0p9 /system
fi;

# install custom boot sound
if [ -f /mnt/sdcard/cranium/PowerOn.wav ]; then
  /sbin/busybox mount -o rw,remount /dev/block/mmcblk0p9 /system
  /sbin/busybox rm /system/etc/PowerOn.wav
  /sbin/busybox cp /mnt/sdcard/cranium/PowerOn.wav /system/etc/PowerOn.wav
  /sbin/busybox rm /mnt/sdcard/cranium/PowerOn.wav
  /sbin/busybox mount -o ro,remount /dev/block/mmcblk0p9 /system
fi;

# Remount ext4 partitions with optimizations
  for k in $(/sbin/busybox mount | /sbin/busybox grep ext4 | /sbin/busybox cut -d " " -f3)
  do
        sync
        /sbin/busybox mount -o remount,commit=15 $k
  done

# Remount all partitions with noatime
  for k in $(/sbin/busybox mount | /sbin/busybox grep relatime | /sbin/busybox cut -d " " -f3)
  do
        sync
        /sbin/busybox mount -o remount,noatime $k
  done 
# EXT4 Speed Tweaks
/sbin/busybox mount -o noatime,remount,rw,discard,barrier=0,commit=60,noauto_da_alloc,delalloc /cache /cache;
/sbin/busybox mount -o noatime,remount,rw,discard,barrier=0,commit=60,noauto_da_alloc,delalloc /data /data;

# UI tweaks
setprop debug.performance.tuning 1; 
setprop video.accelerate.hw 1;
setprop debug.sf.hw 1;

# Enable SCHED_MC - will introduce with ARM Topology
echo "1" > /sys/devices/system/cpu/sched_mc_power_savings

# TCP tweaks
echo "2" > /proc/sys/net/ipv4/tcp_syn_retries
echo "2" > /proc/sys/net/ipv4/tcp_synack_retries
echo "10" > /proc/sys/net/ipv4/tcp_fin_timeout

# fix for samsung roms - setting scaling_max_freq - gm
if [ "`cat /proc/sys/kernel/siyah_feature_set`" == "0" ];
then
  freq=`cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq`
  if [ "$freq" != "1200" ];then
    (
     sleep 25;
     echo $freq > /sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq;
    ) &
  fi
fi

# import/install custom boot logo if one exists
if [ -f /mnt/sdcard/cranium/logo.jpg ]; then
  /sbin/busybox mount -o rw,remount /dev/block/mmcblk0p9 /system
  /sbin/busybox mount -o rw,remount /
  /sbin/busybox touch /.bootlock

  if [ ! -f /system/lib/param.img ]; then
    /sbin/busybox dd if=/dev/block/mmcblk0p4 of=/system/lib/param.img bs=4096
    /sbin/busybox sed 's/.jpg/.org/g' /system/lib/param.img > /system/lib/param.tmp
    /sbin/busybox dd if=/system/lib/param.tmp of=/dev/block/mmcblk0p4 bs=4096
  fi;
  /sbin/busybox mkdir /mnt/sdcard/cranium/old
  /sbin/busybox cp /mnt/.lfs/*.jpg /mnt/sdcard/cranium/old/
  /sbin/busybox umount /mnt/.lfs
  /sbin/busybox mount /dev/block/mmcblk0p4 /mnt/.lfs
  /sbin/busybox cp /mnt/sdcard/cranium/logo.jpg /mnt/.lfs/logo.jpg
  /sbin/busybox rm /mnt/sdcard/cranium/logo.jpg

  /sbin/busybox rm /.bootlock
  /sbin/busybox mount -o ro,remount /dev/block/mmcblk0p9 /system
  /sbin/busybox mount -o ro,remount /
  /sbin/busybox umount /mnt/.lfs
  /sbin/busybox umount /mnt/sdcard
  reboot
fi;

# remove sdcard mount again
/sbin/busybox umount /mnt/sdcard
