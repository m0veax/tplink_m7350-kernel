#!/bin/sh

# section mismatch... YOLO
# build says:
#
# WARNING: vmlinux.o(.text+0x40178): Section mismatch in reference from the function smd_tty_probe_worker() to the function .init.text:smd_tty_core_init()
# The function smd_tty_probe_worker() references
# the function __init smd_tty_core_init().
# This is often because smd_tty_probe_worker lacks a __init
# annotation or the annotation of smd_tty_core_init is wrong.
#
# WARNING: vmlinux.o(.devinit.text+0x17ec): Section mismatch in reference from the function msm_smd_tty_probe() to the function .init.text:smd_tty_devicetree_init()
# The function __devinit msm_smd_tty_probe() references
# a function __init smd_tty_devicetree_init().
# If smd_tty_devicetree_init is only used by msm_smd_tty_probe then
# annotate smd_tty_devicetree_init with a matching annotation.
#
# WARNING: vmlinux.o(.devinit.text+0x1994): Section mismatch in reference from the function msm_pm_boot_probe() to the function .init.text:msm_pm_boot_init()
# The function __devinit msm_pm_boot_probe() references
# a function __init msm_pm_boot_init().
# If msm_pm_boot_init is only used by msm_pm_boot_probe then
# annotate msm_pm_boot_init with a matching annotation.
#
# To build the kernel despite the mismatches, build with:
# 'make CONFIG_NO_ERROR_ON_MISMATCH=y'
# (NOTE: This is not recommended)

_EXTRA=CONFIG_DEBUG_SECTION_MISMATCH=y
_EXTRA=CONFIG_NO_ERROR_ON_MISMATCH=y

ARCH=arm CROSS_COMPILE=arm-linux-gnueabi- make -j4 $_EXTRA
