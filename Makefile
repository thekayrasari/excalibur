# SPDX-License-Identifier: GPL-2.0-or-later
# Makefile for the excalibur-wmi out-of-tree kernel module.

# In-tree Kconfig hook — used when built as part of the kernel tree.
ifneq ($(CONFIG_EXCALIBUR_WMI),)
obj-$(CONFIG_EXCALIBUR_WMI) += excalibur.o
else
# Out-of-tree build — CONFIG_ is not set, so force obj-m.
obj-m += excalibur.o
endif

KDIR ?= /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	depmod -a

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all install clean
