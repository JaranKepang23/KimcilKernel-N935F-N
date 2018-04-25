#!/system/bin/sh

# Busybox 
if [ -e /su/xbin/busybox ]; then
	BB=/su/xbin/busybox;
else if [ -e /sbin/busybox ]; then
	BB=/sbin/busybox;
else
	BB=/system/xbin/busybox;
fi;
fi;

# Mount root as RW to apply tweaks and settings
if [ "$($BB mount | grep rootfs | cut -c 26-27 | grep -c ro)" -eq "1" ]; then
	$BB mount -o remount,rw /;
fi;
if [ "$($BB mount | grep system | grep -c ro)" -eq "1" ]; then
	$BB mount -o remount,rw /system;
fi;

# Fix gps problem on startup
if [ ! -e /system/etc/gps.conf.bak ]; then
	$BB cp /system/etc/gps.conf /system/etc/gps.conf.bak;
fi;


# Make directory for Cron Task & cpuset
if [ ! -d /data/.Kimcil ]; then
	$BB mkdir -p /data/.Kimcil
	$BB chmod -R 0777 /.Kimcil/
fi;

# Backup EFS
if [ ! -d /data/media/0/Kimcil/Synapse/EFS ]; then
	$BB mkdir -p /data/media/0/Kimcil/Synapse/EFS;
fi;
if [ ! -e /data/media/0/Kimcil/Synapse/EFS/efs_backup.img ]; then
	$BB dd if=dev/block/platform/155a0000.ufs/by-name/EFS of=/data/media/0/Kimcil/Synapse/EFS/efs_backup.img 2> /dev/null;
fi;

# Reset CortexBrain WiFi auto screen ON-OFF intervals
if [ -e /data/.wifi_scron.log ]; then
	rm /data/.wifi_scron.log;
fi;
if [ -e /data/.wifi_scroff.log ]; then
	rm /data/.wifi_scroff.log;
fi;

# Set correct r/w permissions for LMK parameters
$BB chmod 666 /sys/module/lowmemorykiller/parameters/cost;
$BB chmod 666 /sys/module/lowmemorykiller/parameters/adj;
$BB chmod 666 /sys/module/lowmemorykiller/parameters/minfree;

# Disable rotational storage for all blocks
# We need faster I/O so do not try to force moving to other CPU cores (dorimanx)
for i in /sys/block/*/queue; do
	echo "0" > "$i"/rotational;
	echo "2" > "$i"/rq_affinity;
done;

if [ "$($BB mount | grep rootfs | cut -c 26-27 | grep -c ro)" -eq "1" ]; then
	$BB mount -o remount,rw /;
fi;

# Synapse
$BB chmod -R 755 /res/*
$BB ln -fs /res/synapse/uci /sbin/uci
/sbin/uci

if [ "$($BB mount | grep rootfs | cut -c 26-27 | grep -c ro)" -eq "1" ]; then
	$BB mount -o remount,rw /;
fi;
if [ "$($BB mount | grep system | grep -c ro)" -eq "1" ]; then
	$BB mount -o remount,rw /system;
fi;

# Init.d
if [ ! -d /system/etc/init.d ]; then
	mkdir -p /system/etc/init.d/;
	chown -R root.root /system/etc/init.d;
	chmod 777 /system/etc/init.d/;
	chmod 777 /system/etc/init.d/*;
fi;
$BB run-parts /system/etc/init.d

# Run Cortexbrain script
# Cortex parent should be ROOT/INIT and not Synapse
cortexbrain_background_process=$(cat /res/synapse/Kimcil/cortexbrain_background_process);
if [ "$cortexbrain_background_process" == "1" ]; then
	sleep 30
	$BB nohup $BB sh /sbin/cortexbrain-tune.sh > /dev/null 2>&1 &
fi;

# Start CROND by tree root, so it's will not be terminated.
cron_master=$(cat /res/synapse/Kimcil/cron/master);
if [ "$cron_master" == "1" ]; then
	$BB nohup $BB sh /res/crontab_service/service.sh 2> /dev/null;
fi;

# Kernel custom test

if [ -e /data/.Kimciltest.log ]; then
	rm /data/.Kimciltest.log
fi;
echo  Kernel script is working !!! >> /data/.Kimciltest.log
echo "excecuted on $(date +"%d-%m-%Y %r" )" >> /data/.Kimciltest.log

$BB mount -o remount,rw /data
