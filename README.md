# zinemachine-sim

A display simulator and Docker dev kit for the **Zine Machine** bar display: a 4.58″
ST7701S panel, **960×320 landscape, RGB565**, driven from a PocketBeagle 2 (arm64).

You write an app on your laptop, save it, and see it on a pixel-exact 960×320 screen in
the browser, drawn inside the device's bezel. Text, raw framebuffer and SDL2 apps all
work, and the same source builds for the real device.

Hardware (PCB, case, panel) lives in [zinemachine0](https://github.com/elevenarms/zinemachine0).
This repo was split out of it with history and keeps the same paths (`tools/sim`,
`software/…`), so code and docs work in either one.

## Quick start

Needs Docker (Docker Desktop on a Mac, including Apple silicon, or Docker on Linux).

```sh
git clone https://github.com/elevenarms/zinemachine-sim && cd zinemachine-sim
tools/sim/sim up            # first run builds the image (~3 min), then opens http://127.0.0.1:6080/
tools/sim/sim term htop     # something on the screen
tools/sim/sim down
```

`sim up` opens the page with `open`, which exists only on macOS. On Linux, browse to
http://127.0.0.1:6080/ yourself. The port is published on `127.0.0.1` only.

## Two simulators

| | Docker dev kit (`tools/sim`) | Browser-only (`site/display-sim`) |
|---|---|---|
| What it runs | arm64 Debian userland in a container; Xvfb at 960×320×16, shown with noVNC | [JSLinux](https://bellard.org/jslinux/) (x86 / RISC-V Linux in WebAssembly) at 960×320 |
| Speed | native (your CPU) | emulated, slow |
| Edit → screen | watches files, rebuilds and restarts on save (~1 s) | upload files into the VM |
| Good for | everyday app development | a real kernel `/dev/fb0` and console; no install at all |

`site/display-sim/index.html` is one static page: open it, or serve it from any web server.

## Three kinds of app

| Kind | Command | What the app sees |
|---|---|---|
| Text / terminal | `sim term [-s PX] CMD…` (idle: `sim console`) | xterm in Terminus **32 px = 60×10 characters**, 3.7 mm tall on the panel. `-s 24` gives 80×13 and `-s 16` gives 120×20. On the device, use `setfont ter-v32n`. |
| Raw framebuffer | `sim fb CMD…` | `/dev/fb0` at 960×320, 16 bpp RGB565, with `mmap`, `FBIOGET_*` and `FBIO_WAITFORVSYNC`. Keys arrive on stdin. |
| SDL2 / X11 | `sim sdl CMD…` | an X display of exactly 960×320 with no window manager. Use a 960×320 window or `SDL_WINDOW_FULLSCREEN*`. |

Framebuffer apps run unmodified. An `LD_PRELOAD` shim (`zmfb`) turns `/dev/fb0` into
shared memory that the viewer draws. Static binaries ignore `LD_PRELOAD`, so build these
apps dynamically linked (see `make sim` in `software/fbdemo`).

Other commands: `sim stop` (back to the console), `sim logs [-f]`, `sim status`,
`sim shell`, `sim rebuild`. To type into the app, click the screen in the page.

Commands run in the container directory that matches your current one: the repo is
mounted at `/work`, so relative paths mean the same thing on both sides.

## Rebuild and rerun on save

```sh
cd software/examples/sdl2-bar
../../../tools/sim/sim watch -m sdl -b make -- bin/bar

cd software/fbdemo
../../tools/sim/sim watch -m fb -b 'make sim' -- bin/fbdemo -f
```

`watch` polls the files, runs the build, and restarts the app. If the build fails, the
error shows on the display in red, and the next save tries again.

## Demos

Build them once inside the container (`tools/sim/sim shell`, then
`make -C software/demos && make -C software/fbdemo sim`), then run them from `software/demos`:

| Command (from `software/demos`) | What it is |
|---|---|
| `../../tools/sim/sim fb ../fbdemo/bin/fbdemo` | demoscene effects on the raw framebuffer |
| `../../tools/sim/sim sdl life/bin/life` | Game of Life, 240×80 cells, coloured by age |
| `../../tools/sim/sim sdl clock/bin/clock` | big LED-pixel clock |
| `../../tools/sim/sim sdl eyechart/bin/eyechart` | console font sizes on the real panel, to pick a readable one |
| `../../tools/sim/sim term python3 dash/dash.py` | curses text dashboard |
| `../../tools/sim/sim term python3 chat/chat.py` | a scripted agent-CLI conversation at 60×10 |
| `../../tools/sim/sim sdl wordproc/run.sh` | a word processor that types Shakespeare while xeyes watches the cursor |
| `../../tools/sim/sim term python3 exetui/exetui.py --demo` | an [exe.dev](https://exe.dev) VM manager TUI. It uses a built-in emulator by default; set `EXE_TOKEN` to use the live API. |
| `./playlist [SECONDS]` | cycles through the demos |

## How close to the device is it?

**The same as the device:**
- resolution, and RGB565 colour (so banding shows up)
- arm64 and a Debian userland
- fbdev ioctls and `mmap`
- the SDL2 API
- the text grid at a given font size

**Different from the device:**
- X11 instead of KMS/DRM. On the device, SDL2 uses `SDL_VIDEODRIVER=kmsdrm`.
- No real VT or `/dev/tty0`. The text console is xterm, not fbcon.
- The CPU is yours, not a 1.4 GHz Cortex-A53, so expect the device to be several times slower.

## Layout

```
tools/sim/            Docker dev kit
  sim                 host-side command
  Dockerfile          Debian trixie, Xvfb 960x320x16, x11vnc + noVNC, build tools, SDL2
  bin/zm, bin/zm-start  in-container command and entrypoint
  zmfb/               fake /dev/fb0 (LD_PRELOAD) and its viewer
  www/index.html      the bezel-frame page
site/display-sim/     JSLinux-in-the-browser simulator (one HTML file)
software/fbdemo/      framebuffer demo (also cross-builds for the device)
software/examples/    sdl2-bar: minimal SDL2 starting point
software/demos/       the demos above
```

## License

GPL-3.0, the same as zinemachine0. `software/fbdemo/font8x8_basic.h` is public domain.
JSLinux is loaded from bellard.org at runtime and is not included here.
