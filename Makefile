
# typically use the following to compile
# make ARCH=arm CROSS_COMPILE=/home/fredrik/mentor/arm-2011.03/bin/arm-none-linux-gnueabi
#
# Also modify 'KERNEL_SRC' to fit your system

YOCTO_DIRECTORY?=/media/a/evco-3.3
CAMAPPS_DIRECTORY?=${HOME}/camapps

ARCH=arm
CCACHE="ccache "
CROSS_COMPILE="$(CCACHE)arm-oe-linux-gnueabi-"

INCLUDE_SRC:=${YOCTO_DIRECTORY}/sources/meta-flir-base/recipes-flir/flirsdk/files
KERNEL_SRC:=${YOCTO_DIRECTORY}/dbuild_evco/tmp/work-shared/evco/kernel-build-artifacts
INCLUDE2_SRC:=${CAMAPPS_DIRECTORY}/camapps/alpha/flir_sdk/pub/flir_sdk/


ifeq ($(KERNEL_SRC),)
	KERNEL_SRC ?= ~/linux/flir-yocto/build_pico/tmp-eglibc/work/neco-oe-linux-gnueabi/linux-boundary/3.0.35-r0/git
endif

ifneq ($(KERNEL_PATH),)
       KERNEL_SRC = $(KERNEL_PATH)
endif

WERROR = -Werror
EXTRA_CFLAGS = -I${INCLUDE2_SRC} -I${INCLUDE_SRC} ${WERROR}

	obj-m := yildun.o
	yildun-objs += yildun_main.o
	yildun-objs += load_fpga.o
	yildun-objs += yildun_mx6s.o
	PWD := $(shell pwd)

all: 
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD)  $(EXTRA_CFLAGS) modules

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean

deploy: all
	scp yildun.ko ${SYSTEM_FLIR_TARGET}:
