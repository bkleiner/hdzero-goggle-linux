# Driver for the Allwinner XRadio XR819 wifi chip #

This is an experimental wifi driver for the XRADIO XR819 wifi chip - as used in Single Board Computers (SBCs) such as the *Orange Pi Zero* or the *Nanopi Duo*, and TV-boxes like the the *Sunvell R69*. 

Tested kernel versions: `4.14 - 5.5`

**STA-Mode** (standard client station) and **AP-Mode** (device as access point) using **WPA2** work. Hidden and open APs as well as WEP- or WPA1-encrypted connection are not supported. P2P has not been tested. 

# Firmware and dts-files #

Get **firmware binaries** from somewhere, e.g. https://github.com/karabek/xradio/tree/master/firmware (`boot_xr819.bin`, `fw_xr819.bin`, `sdd_xr819.bin`) and place into your firmware folder (e.g. `/lib/firmware/xr819/`)

Example **device tree** files (for kernel version 5.5) can be found here:
https://github.com/karabek/xradio/blob/master/dts/.

# Building on a host system #

Cross-compilations allows building a complete linux system with custom drivers on a suitable host system (e.g. your PC).

## Building with armbian ##

The **armbian project** (https://www.armbian.com/) provides an ideal build environment for building linux on arm based devices:
https://github.com/armbian/build
Armbian has built-in xradio-support.

## Building on any host system ##

To cross-compile and build the kernel module yourself on a host system get a suitable toolchain and try something like this:

```
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C <PATH TO YOUR LINUX SRC> M=$PWD modules
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -C <PATH TO YOUR LINUX SRC> M=$PWD INSTALL_MOD_PATH=<PATH TO INSTALL MODULE> modules_install
```

For info on Toolchains see http://linux-sunxi.org/Toolchain.

# Building an "out-of-tree" driver on the device #

Kernel headers have to be installed for building kernel modules on a device. Make sure that the xradio-chip is supported by the device tree. 

## Option 1: the quick way ##

Clone the driver code directly on the device and use the provided script to compile and install the driver. 

```
git clone https://github.com/karabek/xradio.git
cd xradio
sudo ./xr-install.sh
```

Reboot the device.

```
sudo reboot
```


## Option 2: step-by-step ##

First clone driver code:

```
git clone https://github.com/karabek/xradio.git
cd xradio
```

Uncomment line 4 and 5 of Makefile:
```
	CONFIG_WLAN_VENDOR_XRADIO := m
	ccflags-y += -DCONFIG_XRADIO_USE_EXTENSIONS
	# ccflags-y += -DCONFIG_XRADIO_WAPI_SUPPORT
```

Compile the kernel module:

```
make  -C /lib/modules/$(uname -r)/build M=$PWD modules
ll *.ko
```

You should see the compiled module (xradio_wlan.ko) in your source directory. 
Now copy the module to the correct driver directory and make module dependencies available:

```
mkdir /lib/modules/$(uname -r)/kernel/drivers/net/wireless/xradio
cp xradio_wlan.ko /lib/modules/$(uname -r)/kernel/drivers/net/wireless/xradio/
depmod
```

Finally reboot the device.

```
sudo reboot
```


 :black_small_square:  :black_small_square:  :black_small_square:



