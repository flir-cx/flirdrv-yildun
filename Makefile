
# typically use the following to compile
# make ARCH=arm CROSS_COMPILE=/home/fredrik/mentor/arm-2011.03/bin/arm-none-linux-gnueabi
#
# Also modify 'KERNEL_SRC' to fit your system

ifeq ($(KERNEL_SRC),)
	KERNEL_SRC ?= ~/linux/flir-yocto/build_pico/tmp-eglibc/work/neco-oe-linux-gnueabi/linux-boundary/3.0.35-r0/git
endif

ifneq ($(KERNEL_PATH),)
       KERNEL_SRC = $(KERNEL_PATH)
endif

EXTRA_CFLAGS = -I$(ALPHAREL)/SDK/FLIR/Include/ 
# -DFVD_DEPRECIATED_OK=0

	obj-m := yildun.o
	yildun-objs += yildun_main.o
	yildun-objs += load_fpga.o
	yildun-objs += yildun_mx6s.o
	PWD := $(shell pwd)

all: 
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD)  $(EXTRA_CFLAGS) modules

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
