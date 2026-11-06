# Display simulator (Docker, live)

Develop apps for the 960×320 bar display on a Mac (or any Docker host) at native speed:
an arm64 Debian container with a **960×320, 16-bit (RGB565)** X display, shown inside the
bezel frame in the browser. Edit on the Mac, the container rebuilds and restarts the app,
the frame updates.

```sh
tools/sim/sim up                 # build image (first time ~3 min), start, open http://127.0.0.1:6080/
tools/sim/sim down
```

The repo is mounted at `/work`; `sim` commands run in the container directory that matches
your current directory, so paths work the same on both sides. Only `127.0.0.1` is published.

## Three kinds of app

| Kind | Command | What the app sees |
|---|---|---|
| Text / terminal | `sim term [-s PX] CMD…` (idle: `sim console`, a shell) | a terminal in Terminus **32 px (60×10, 3.7 mm tall)** by default — the size picked as readable on the panel with `software/demos/eyechart`; `-s 24` gives 80×13, `-s 16` the 120×20 fbcon default. On the device: `setfont ter-v32n` (console-terminus). Installed: htop, nano, mc, tmux, cmatrix, figlet, w3m, bc |
| Raw framebuffer | `sim fb CMD…` | `/dev/fb0`: 960×320, 16 bpp RGB565 (`ZMFB_BPP=32` for XRGB8888), `mmap`, `FBIOGET_*`, `FBIO_WAITFORVSYNC`; keys arrive on stdin like a VT |
| SDL2 (or any X11) | `sim sdl CMD…` | an X display of exactly 960×320, no window manager (like KMS on the device); size windows 960×320 or use `SDL_WINDOW_FULLSCREEN*` |

`sim stop` returns to the console. `sim logs [-f]` shows the app's output (also under the
frame), `sim status` what is running. Click the screen in the page to type into it.

**Framebuffer apps** run unmodified through `libzmfb.so` (`LD_PRELOAD`), which turns
`/dev/fb0` into shared memory that `zmfb-run` shows. That needs a **dynamically linked**
build (static binaries ignore `LD_PRELOAD`), e.g. `make sim` in `software/fbdemo`.

## Fast loop: rebuild and rerun on save

```sh
cd software/fbdemo
../../tools/sim/sim watch -m fb -b 'make sim' -- bin/fbdemo -f

cd software/examples/sdl2-bar
../../../tools/sim/sim watch -m sdl -b make -- bin/bar
```

`watch` polls the files under `-p PATH` (default `.`, ignoring `.git`, `bin`, `build`, `*.o`)
twice a second, runs the build, and restarts the app. A failed build is shown on the display
in red and in the log; the next save tries again.

## How close to the device is it?

- Same: resolution, RGB565 colour depth (banding included), arm64, Debian userland,
  fbdev ioctls/mmap for framebuffer apps, SDL2 API, 120×20 text grid.
- Different: X11 instead of KMS/DRM (SDL2 on the device would use `SDL_VIDEODRIVER=kmsdrm`),
  no real `/dev/tty0`/VT switching, CPU is the Mac's, not a 1.4 GHz Cortex-A53 (expect the
  device to be several times slower), the text console is xterm, not fbcon.
- `site/display-sim/` (JSLinux in the browser) has a real kernel `/dev/fb0` and console;
  use it for kernel-level checks.

## Files

- `Dockerfile` — Debian trixie, Xvfb 960x320x16 (no window manager), x11vnc + noVNC, build tools, SDL2.
- `bin/zm-start` — container entrypoint; `bin/zm` — the in-container command (`sim` forwards to it).
- `zmfb/zmfb_preload.c` — the fake `/dev/fb0`; `zmfb/zmfb_run.c` — shows it, runs the app on a pty.
- `www/index.html` — the bezel-frame page (noVNC), status and log.
- `sim` — the Mac-side command.
