# Makefile para compilar el modulo DENTRO de la VM (build nativo arm64).
# Se ejecuta en el guest, donde estan los headers que matchean el kernel.

obj-m += sdec_signals.o

KDIR := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

.PHONY: all clean
