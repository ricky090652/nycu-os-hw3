# 你的模組名稱（不用加 .ko）
obj-m := kfetch_mod_314581038.o

# devkit 目錄：作業說明中的 linux-6.8-devkit
KDIR ?= $(PWD)/linux-v6.8-devkit

# 交叉編譯工具鏈（跟作業一樣）
ARCH ?= riscv
CROSS_COMPILE ?= riscv64-linux-gnu-

all:
	$(MAKE) -C $(KDIR) M=$(PWD) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
	rm -f *.o *.mod *.mod.c *.symvers modules.order
