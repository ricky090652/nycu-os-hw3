# 模組名稱
obj-m := kfetch_mod_314581038.o

# devkit目錄
KDIR ?= $(PWD)/linux-v6.8-devkit

# 
ARCH ?= riscv
CROSS_COMPILE ?= riscv64-linux-gnu-

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.o *.mod *.mod.c *.symvers modules.order
