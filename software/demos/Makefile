# Demos for the bar display. `make` builds the SDL2 ones (needs libsdl2-dev; the sim image has it).
#   ../../tools/sim/sim sdl life/bin/life       Game of Life, 240x80 cells, colour by age
#   ../../tools/sim/sim sdl clock/bin/clock     big LED pixel clock
#   ../../tools/sim/sim term python3 dash/dash.py   text-mode dashboard (curses, 120x20)
#   ../../tools/sim/sim sdl eyechart/bin/eyechart   font sizes on the panel (pick a console font)
#   ../../tools/sim/sim term python3 chat/chat.py   simulated Claude Code-style session (60x10)
#   ../../tools/sim/sim sdl wordproc/run.sh       ZineWrite types Shakespeare, xeyes watch the cursor
#   ../../tools/sim/sim term python3 exetui/exetui.py --demo   exe.dev VM manager (SIM backend; EXE_TOKEN for live)
#   ./playlist                                  all of them (and fbdemo) in turn
CFLAGS ?= -O2 -Wall -Wextra
SDL    = $(shell pkg-config --cflags --libs sdl2)

all: life/bin/life clock/bin/clock eyechart/bin/eyechart

life/bin/life: life/life.c
	mkdir -p $(@D); $(CC) $(CFLAGS) -o $@ $< $(SDL)

clock/bin/clock: clock/clock.c ../fbdemo/font8x8_basic.h
	mkdir -p $(@D); $(CC) $(CFLAGS) -o $@ $< $(SDL) -lm

eyechart/bin/eyechart: eyechart/eyechart.c
	mkdir -p $(@D); $(CC) $(CFLAGS) -o $@ $< -lX11

clean:
	rm -rf life/bin clock/bin eyechart/bin
.PHONY: all clean
