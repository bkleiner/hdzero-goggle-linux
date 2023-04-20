
```shell
#!/bin/bash
set -e

# download and extract the same toolchain we use for the goggle app
TOOLCHAIN_URL="https://toolchains.bootlin.com/downloads/releases/toolchains/armv7-eabihf/tarballs/armv7-eabihf--musl--stable-2018.02-2.tar.bz2"
if [ ! -d toolchain ]; then
	echo "Extracting toolchain..."
	mkdir toolchain
	wget -qO- "$TOOLCHAIN_URL" | tar xj --strip-components=1 -C .toolchain
fi

# add our toolchain binaries to PATH
export PATH="$(pwd)/.toolchain/bin:$PATH"

# select the arch we want
export ARCH="arm"

# provide the prefix of our toolchain
# this must match gcc etc found in the toolchain/bin folder
export CROSS_COMPILE="arm-buildroot-linux-musleabihf-"

# configure our kernel. this is a very important step.
# a kernel source tree can contain default configurations in the arch/<arch/configs folder.
# i have create one for the goggle at arch/arm/configs/hdzgoggle_defconfig
make hdzgoggle_defconfig

# finally build the kernel with threading.
# nproc returns the number of cpus on a system.
make -j$(nproc)
```