#Normal Makefile for ARM
#
#Export the compiler if you haven´t export it yet. 
#export PATH=~/Desktop/Android/Toolchain/arm-2011.09/bin:$PATH
#
#>For any ARM any plataform
#(Example) $make ARCH=arm CROSS_COMPILE=(ARM_COMPILER_PATH)/arm-none-linux-gnueabi-
#
#>For any ARM specific plataform -> (Use de defconfig file located in /kernel/arch/arm/configs/xxx_defconfig)
#(Example) $make ARCH=arm CROSS_COMPILE=(ARM_COMPILER_PATH)/arm-none-linux-gnueabi- omap3_evm_android_defconfig
#
#Configs for Android and the OMAP Plataform
#omap3evm_config 		->Used to build the X-loader
#omap3_evm_config 		->Used to build the U-boot
#omap3_evm_android_defconfig	->Used to build the kernel
#

#This ifs are used only for not writting wich architecutre and compiler to use each time you compile the source.
ifndef ARCH
ARCH=arm
export ARCH
endif

ifndef CROSS_COMPILE
CROSS_COMPILE =~/Desktop/Android/Toolchain/arm-2011.09/bin/arm-none-linux-gnueabi-
export	CROSS_COMPILE
endif


obj-m := miwi-driver.o
KDIR := ~/Desktop/Android/GingerBread/SourceCode/TI_Android_GingerBread_2_3_4Sources/kernel/
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean



#Normal Makefile for x86
#
#obj-m += miwi-driver.o
#
#all:
#	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
#
#clean:
#	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
