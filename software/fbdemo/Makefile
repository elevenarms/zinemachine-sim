# Static builds, so the binary runs on any Linux of that architecture (no libc match needed).
#   make            both
#   make x86        bin/fbdemo-i686     JSLinux "Alpine Linux x86" (display simulator)
#   make arm64      bin/fbdemo-aarch64  PocketBeagle 2 (AM6254, Cortex-A53)
#   make sim        bin/fbdemo          dynamic build for tools/sim (its fake /dev/fb0 is an LD_PRELOAD shim)
# On an arm64 host (the Backplane VM) `cc` builds arm64; i686 needs gcc-i686-linux-gnu.

CFLAGS  ?= -O2 -Wall -Wextra -Wno-misleading-indentation
LDLIBS  = -lm
CC_X86  ?= i686-linux-gnu-gcc
CC_A64  ?= $(if $(filter aarch64 arm64,$(shell uname -m)),cc,aarch64-linux-gnu-gcc)

all: x86 arm64
x86:   bin/fbdemo-i686
arm64: bin/fbdemo-aarch64

bin/fbdemo-i686: fbdemo.c font8x8_basic.h | bin
	$(CC_X86) $(CFLAGS) -march=i686 -static -o $@ fbdemo.c $(LDLIBS)

sim: bin/fbdemo

bin/fbdemo: fbdemo.c font8x8_basic.h | bin
	$(CC) $(CFLAGS) -o $@ fbdemo.c $(LDLIBS)

bin/fbdemo-aarch64: fbdemo.c font8x8_basic.h | bin
	$(CC_A64) $(CFLAGS) -mcpu=cortex-a53 -static -o $@ fbdemo.c $(LDLIBS)

bin:
	mkdir -p bin

clean:
	rm -rf bin

.PHONY: all x86 arm64 sim clean
