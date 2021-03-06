#!/bin/bash

CURRENT=`pwd`
TARGET=$1

cd $TARGET/src
make
cp kernel $CURRENT
make clean
cd $CURRENT

sudo /sbin/losetup /dev/loop1000 floppy-initrd.img
mkdir mnt
sudo mount /dev/loop1000 mnt
sudo cp kernel mnt/kernel
sudo cp initrd.img mnt/initrd
sudo umount /dev/loop1000
sudo /sbin/losetup -d /dev/loop1000
rmdir mnt
