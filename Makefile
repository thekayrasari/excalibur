obj-m += casper-wmi.o

all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	
install:
	zstd casper-wmi.ko -o /lib/modules/$(shell uname -r)/kernel/drivers/platform/x86/casper-wmi.ko.zst
