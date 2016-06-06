# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
	obj-m := pref-test.o
# Otherwise we were called directly from the command
# line; invoke the kernel build system.
else
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
	#$(KERNELDIR)/scripts/sign-file sha512 $(KERNELDIR)/certs/signing_key.pem $(KERNELDIR)/certs/signing_key.x509 pref-test.ko
endif

clean:
	rm -fv *.ko *.o modules.order Module.symvers *.mod.c
	rm -vrf .tmp_versions
