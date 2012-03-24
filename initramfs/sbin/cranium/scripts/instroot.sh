#!/sbin/busybox sh

if [ -f /system/xbin/su ] || [ -f /system/bin/su ];
then
	echo "su already exists"
else
	echo "Copying su binary"
	/sbin/busybox mount /system -o remount,rw
	/sbin/busybox rm /system/bin/su
	/sbin/busybox rm /system/xbin/su
	/sbin/busybox cp /sbin/cranium/root/su /system/xbin/su
	/sbin/busybox chown 0.0 /system/xbin/su
	/sbin/busybox chmod 4755 /system/xbin/su
	/sbin/busybox mount /system -o remount,ro
fi;

if [ -f /system/app/Superuser.apk ] || [ -f /data/app/Superuser.apk ];
then
	echo "Superuser.apk already exists"
else
	echo "Copying Superuser.apk"
	/sbin/busybox mount /system -o remount,rw
	/sbin/busybox rm /system/app/Superuser.apk
	/sbin/busybox rm /data/app/Superuser.apk
	/sbin/busybox xzcat /sbin/cranium/Superuser.apk.xz > /system/app/Superuser.apk
	/sbin/busybox chown 0.0 /system/app/Superuser.apk
	/sbin/busybox chmod 644 /system/app/Superuser.apk
	/sbin/busybox mount /system -o remount,ro
fi;
# CWM Manager
if [ -f /system/app/CWMManager.apk ];
then
	echo "CWM Manager already exists"
else
	echo "Copying CWMManager.apk"
	/sbin/busybox mount /system -o remount,rw
	/sbin/busybox rm /system/app/CWMManager.apk
	/sbin/busybox rm /system/app/CWMManager.apk
	/sbin/busybox cp /sbin//cranium/root/CWMManager.apk /system/app/CWMManager.apk
	/sbin/busybox chown 0.0 /system/app/CWMManager.apk
	/sbin/busybox chmod 4755 /system/app/CWMManager.apk
	/sbin/busybox mount /system -o remount,ro
fi;

# create init.d folders
	/sbin/busybox mount /system -o remount,rw
	/sbin/busybox mkdir /system/etc
	/sbin/busybox mkdir /data/etc
	/sbin/busybox mkdir /system/etc/init.d
	/sbin/busybox mkdir /data/etc/init.d
	/sbin/busybox mount /system -o remount,ro


# Once be enough
	/sbin/busybox mount /system -o remount,rw
    	/sbin/busybox mkdir /system/cfroot
   	/sbin/busybox chmod 755 /system/cfroot
   	/sbin/busybox rm /data/cfroot/*
   	/sbin/busybox rmdir /data/cfroot
    	/sbin/busybox rm /system/cfroot/*
    	echo 1 > /system/cfroot/release-100-DZKL3-
	/sbin/busybox mount /system -o remount,ro
	


# end
