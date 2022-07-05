#rs485
CC ?= gcc
TAR = test

all: clean build

build:
	$(CC) $(TAR).c -o $(TAR).app -L/usr/local/lib -lusb-1.0

clean:
	rm -rf *.app

exec:
	./$(TAR).app

help:
	@echo cmd to cross compile
	@echo source /opt/fsl-imx-wayland/4.14-sumo/environment-setup-aarch64-poky-linux
	@echo export ARCH=arm64
	@echo unset LDFLAGS

install:
	cp $(TAR).app /nfsshare

uninstall:
	rm -f /nfsshare/$(TAR).app
