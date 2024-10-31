#!/bin/bash

export ARCH=arm
export CROSS_COMPILE=arm-linux-gnueabi-

make m7350-un-v3_defconfig
make -j8


