#!/bin/bash

# Some of the regex's used in sed
# Catch basic IP6 address   "s/\([0-9a-fA-F]\{1,4\}:\)\{7,7\}[0-9a-fA-F]\{1,4\}/y.y.y.y.y.y.y.y/g"
# Catch y::y.y.y.y          "s/[0-9a-fA-F]\{1,4\}:\(:[0-9a-fA-F]\{1,4\}\)\{1,4\}/y::y.y.y.y/g"
# IP4 d.d.d.d decimal	    "s/\([0-9]\{1,3\}\.\)\{3,3\}[0-9]\{1,3\}/x.x.x.x/g"
# mac address	            "s/\([0-9a-fA-F]\{2,2\}\:\)\{5,5\}[0-9a-fA-F]\{2,2\}/m.m.m.m/g"

OUT=raspinfo.txt

rm -f $OUT

exec > >(tee -ia $OUT)

echo System Information 
echo ------------------
echo

cat /sys/firmware/devicetree/base/model
echo

cat /etc/os-release | head -4
echo

cat /etc/rpi-issue
echo
uname -a

cat /proc/cpuinfo | tail -3

echo "Throttled flag  : "`vcgencmd get_throttled`
echo "Camera          : "`vcgencmd get_camera`

echo
echo "Videocore information"
echo "---------------------"
echo

vcgencmd version
echo
vcgencmd mem_reloc_stats

echo
echo "Filesystem information"
echo "----------------------"

df
echo
cat /proc/swaps

echo
echo "Package version information"
echo "---------------------------"

apt-cache policy raspberrypi-ui-mods | head -2
apt-cache policy raspberrypi-sys-mods | head -2
apt-cache policy openbox | head -2
apt-cache policy lxpanel | head -2
apt-cache policy pcmanfm | head -2
apt-cache policy rpd-plym-splash | head -2

echo
echo "Networking Information"
echo "----------------------"
echo

ifconfig | sed -e "s/\([0-9a-fA-F]\{1,4\}:\)\{7,7\}[0-9a-fA-F]\{1,4\}/y.y.y.y.y.y.y.y/g" | sed -e "s/[0-9a-fA-F]\{1,4\}:\(:[0-9a-fA-F]\{1,4\}\)\{1,4\}/y::y.y.y.y/g" | sed -e "s/\([0-9]\{1,3\}\.\)\{3,3\}[0-9]\{1,3\}/x.x.x.x/g" | sed -e "s/\([0-9a-fA-F]\{2,2\}\:\)\{5,5\}[0-9a-fA-F]\{2,2\}/m.m.m.m/g"

echo
echo "USB Information"
echo "---------------"
echo

lsusb -t

echo
echo "config.txt"
echo "----------"
echo

#cat /boot/config.txt | egrep -v "^\s*(#|^$)"
vcgencmd get_config int
vcgencmd get_config str


echo
echo "cmdline.txt"
echo "-----------"

cat /proc/cmdline

echo
echo "raspi-gpio settings"
echo "-------------------"
echo

raspi-gpio get

echo
echo "vcdbg log messages"
echo "------------------"
echo

sudo vcdbg log msg 2>&1

echo
echo "dmesg log"
echo "---------"
echo

dmesg | sed -e "s/\([0-9a-fA-F]\{1,4\}:\)\{7,7\}[0-9a-fA-F]\{1,4\}/y.y.y.y.y.y.y.y/g" | sed -e "s/[0-9a-fA-F]\{1,4\}:\(:[0-9a-fA-F]\{1,4\}\)\{1,4\}/y::y.y.y.y/g" | sed -e "s/\([0-9a-fA-F]\{2,2\}\:\)\{5,5\}[0-9a-fA-F]\{2,2\}/m.m.m.m/g"
