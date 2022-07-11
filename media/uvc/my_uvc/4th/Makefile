KERN_DIR = /usr/src/linux-headers-4.15.0-112-generic

obj-m += my_uvc.o

all:
	make -C $(KERN_DIR) M=`pwd` modules
clean:
	make -C $(KERN_DIR) M=`pwd` modules clean
	rm -rf modules.order


