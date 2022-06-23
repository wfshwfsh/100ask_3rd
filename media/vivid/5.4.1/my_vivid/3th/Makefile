KERN_DIR = /usr/src/linux-headers-5.4.0-117-generic

obj-m += my_vivid.o

all:
	make -C $(KERN_DIR) M=`pwd` modules
clean:
	make -C $(KERN_DIR) M=`pwd` modules clean
	rm -rf modules.order


