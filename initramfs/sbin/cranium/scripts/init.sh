#!/sbin/busybox sh

# cf-root custom kernel properties
  	/sbin/busybox sh /sbin/cranium/scripts/properties.sh

# root install script
 	 /sbin/busybox sh /sbin/cranium/scripts/instroot.sh

# init.d
/sbin/busybox mount /system -o remount,rw
chmod 777 /system/etc/init.d/*
if cd /system/etc/init.d >/dev/null 2>&1 ; then
  for file in * ; do
    if ! ls "$file" >/dev/null 2>&1 ; then continue ; fi
    /sbin/busybox sh "$file"
  done
fi
chmod 777 /data/etc/init.d/*
if cd /data/etc/init.d >/dev/null 2>&1 ; then
  for file in * ; do
    if ! ls "$file" >/dev/null 2>&1 ; then continue ; fi
    /sbin/busybox sh "$file"
  done
fi
/sbin/busybox mount /system -o remount,ro
