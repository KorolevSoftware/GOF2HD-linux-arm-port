TC ?= $(HOME)/tools/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-linux-gnueabihf
CC = $(TC)/bin/arm-none-linux-gnueabihf-gcc
CXX = $(TC)/bin/arm-none-linux-gnueabihf-g++
SYSROOT = $(TC)/arm-none-linux-gnueabihf/libc

CFLAGS = -O2 -fno-stack-protector -Wall
LDFLAGS = -ldl

SHIM_OBJS = shim/libc.so shim/liblog.so shim/libandroid.so shim/libm.so shim/libdl.so
GLES_OBJS = gles-stub/libGLESv2.so gles-stub/libGLESv1_CM.so
HOST_OBJ = host/gof2hd

all: $(SHIM_OBJS) $(GLES_OBJS) $(HOST_OBJ)

shim/libc.so: shim/shim.c shim/sscanf.c shim/stdio.c shim/version.map
	$(CC) -shared -fPIC -O2 -fno-stack-protector -fno-builtin \
		-Wl,--version-script=shim/version.map -o $@ \
		shim/shim.c shim/sscanf.c shim/stdio.c -ldl

shim/liblog.so: shim/liblog.c
	$(CC) -shared -fPIC -O2 -o $@ shim/liblog.c

shim/libandroid.so shim/libm.so shim/libdl.so:
	$(CC) -shared -fPIC -x c -o $@ -

gles-stub/libGLESv2.so: gles-stub/gles-stub.c
	$(CC) -shared -fPIC -O2 -o $@ gles-stub/gles-stub.c

gles-stub/libGLESv1_CM.so: gles-stub/libGLESv2.so
	cp $< $@

host/gof2hd: host/gof2hd.c host/jni.c host/jni.h
	$(CC) $(CFLAGS) -o $@ host/gof2hd.c host/jni.c $(LDFLAGS)

clean:
	rm -f $(SHIM_OBJS) $(GLES_OBJS) $(HOST_OBJ)

.PHONY: all clean
