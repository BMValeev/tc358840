#!/bin/bash
cp -f tc358743_h2c.c ~/bluez/poky/meta-solidrun-arm-imx6/recipes-kernel/linux/linux-solidrun-imx6-3.14.1.0/linux-fslc-3.14-1.0.x-mx6-sr-tc358743_h2c/drivers/media/platform/mxc/capture/tc358743_h2c.c
cp -f tc358743_ioctl.c ~/bluez/poky/meta-solidrun-arm-imx6/recipes-kernel/linux/linux-solidrun-imx6-3.14.1.0/linux-fslc-3.14-1.0.x-mx6-sr-tc358743_h2c/drivers/media/platform/mxc/capture/tc358743_ioctl.c
cp -f tc358743_lowl.c ~/bluez/poky/meta-solidrun-arm-imx6/recipes-kernel/linux/linux-solidrun-imx6-3.14.1.0/linux-fslc-3.14-1.0.x-mx6-sr-tc358743_h2c/drivers/media/platform/mxc/capture/tc358743_lowl.c
cp -f tc358743_i2c.c ~/bluez/poky/meta-solidrun-arm-imx6/recipes-kernel/linux/linux-solidrun-imx6-3.14.1.0/linux-fslc-3.14-1.0.x-mx6-sr-tc358743_h2c/drivers/media/platform/mxc/capture/tc358743_i2c.c
cd ~/bluez/poky/meta-solidrun-arm-imx6/recipes-kernel/linux/linux-solidrun-imx6-3.14.1.0
./repack_kernel.sh
echo "copied"
cd ~/Документы/driver2/tc358840/new
echo "end"
