CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += $(shell pkg-config --cflags sdl2)
LDLIBS  = $(shell pkg-config --libs sdl2) -lm

bin/bar: bar.c
	mkdir -p bin
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

clean:
	rm -rf bin
.PHONY: clean
