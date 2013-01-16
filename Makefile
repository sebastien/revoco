VERSION=0.3
MX_REVOLUTION=`lsusb | grep "MX Revolution" | cut -d' ' -f6 | cut -d':' -f2`
all:
	gcc -c revoco.c -DVERSION='"$(VERSION)"' -DMX_REVOLUTION=0x$(MX_REVOLUTION)
	gcc revoco.o -o revoco
