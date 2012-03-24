#!/sbin/busybox sh

# initiate custom boot animation or stock animation if no custom one present

if [ -f /system/media/bootanimation.zip ]; then
  /sbin/bootanimation
else
  /system/bin/samsungani
fi;
