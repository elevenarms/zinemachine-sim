# fbdemo

A demoscene-style demo for the bar display, drawn straight to the Linux framebuffer
(`/dev/fb0`, no X, no libraries): plasma, tunnel, rotozoomer and copper bars with a
starfield, a sine scroller over all of them, 12 s per scene with fades and flashes.

Effects draw 8-bit palette indices at half resolution (480×160 "chunky" pixels); a
256-entry lookup table turns them into the framebuffer's own format (RGB565 on the
panel, XRGB8888 in JSLinux), so fades cost 256 colours, not 307 200 pixels. Animation
follows the clock, so slow hardware drops frames but keeps time. A portrait
framebuffer (the ST7701S is natively 320×960) is drawn rotated.

```
fbdemo [-d /dev/fb0] [-s SCALE 1..8] [-t SECONDS] [-r 0|90|270] [-f]
```
`-s 1` full resolution, `-f` FPS counter, `-t` stop after N seconds. `q`, Esc or Ctrl-C quits.

## Build

```sh
make          # bin/fbdemo-i686 (simulator) and bin/fbdemo-aarch64 (PocketBeagle 2), both static
```
Needs `gcc` and, for the simulator build, `gcc-i686-linux-gnu` + `libc6-dev-i386-cross`
(both installed in the Backplane VM). `font8x8_basic.h` is Daniel Hepper's public-domain
8×8 font (github.com/dhepper/font8x8).

## Run in the display simulator

1. Open JSLinux at the panel resolution:
   <https://bellard.org/jslinux/vm.html?cpu=x86&url=alpine-x86-xwin.cfg&mem=256&graphic=1&w=960&h=320>
2. Upload `bin/fbdemo-i686` with the ⬆ icon under the screen (it lands in `/root`, named as uploaded).
3. Right-click the desktop → Terminal, then `chmod +x fbdemo-i686 && ./fbdemo-i686 -f`.

Measured there: 36–56 FPS at `-s 2` (Chrome, Apple silicon). X is left running underneath;
the demo simply owns the framebuffer while it runs.

## Run on the PocketBeagle 2

Not tried yet (no board, and the panel driver/overlay is still open; see AGENTS.md §5).
Once `/dev/fb0` drives the panel: copy `bin/fbdemo-aarch64` over, run it from a text
console as a user in the `video` group (or root). Use `-r` if the panel comes up rotated
the other way.
