/*
 * fbdemo: a small demoscene-style demo for the Zine Machine's 960x320 bar display.
 *
 * Plain Linux fbdev (/dev/fb0), no libraries. Effects render 8-bit palette-indexed
 * ("chunky") pixels into a buffer at 1/SCALE resolution; a 256-entry LUT turns them
 * into the framebuffer's own pixel format (RGB565 on the real panel, XRGB8888 in
 * JSLinux) and the blit scales them up. Fades and flashes only touch the LUT.
 *
 * Scenes (loop): plasma, tunnel, rotozoomer, copper bars + starfield; a sine
 * scroller runs over all of them. Animation is driven by wall-clock time, so a
 * slow emulator drops frames but keeps the tempo.
 *
 * Portrait framebuffers (the ST7701S panel is natively 320x960) are drawn rotated
 * so the demo is landscape either way (-r 0/90/270 to force).
 *
 *   fbdemo [-d /dev/fb0] [-s SCALE] [-t SECONDS] [-r ROT] [-f]
 *   q, Esc or Ctrl-C quits.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/kd.h>
#include <math.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "font8x8_basic.h"

/* ---- options & state ------------------------------------------------------ */

static const char *fbdev = "/dev/fb0";
static int scale = 2;          /* chunky pixel size */
static double run_for = 0;     /* 0 = forever */
static int force_rot = -1;     /* -1 auto */
static int show_fps = 0;

static volatile sig_atomic_t quit = 0;
static void on_signal(int s) { (void)s; quit = 1; }

static int fb_fd = -1, tty_fd = -1, tty_was_text = 0;
static struct termios tio_saved;
static int tio_ok = 0;

static struct fb_var_screeninfo vi;
static struct fb_fix_screeninfo fi;
static uint8_t *fbmem;
static size_t fblen;
static int bpp;                /* bytes per pixel */
static int rot;                /* 0, 90, 270 */

static int LW, LH;             /* logical (landscape) size */
static int IW, IH;             /* internal chunky buffer size */
static uint8_t *buf;           /* IW*IH palette indices */
static uint32_t *row;          /* one scaled output row, as packed pixels */

/* palette: 0..191 effect, 192..223 bars, 224 shadow, 225..255 text rainbow */
static uint8_t pal[256][3];
static uint32_t lut[256];

/* ---- helpers -------------------------------------------------------------- */

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static void die(const char *what)
{
    fprintf(stderr, "fbdemo: %s: %s\n", what, strerror(errno));
    exit(1);
}

static void setpal(int i, double r, double g, double b)
{
    if (r < 0) r = 0; if (r > 255) r = 255;
    if (g < 0) g = 0; if (g > 255) g = 255;
    if (b < 0) b = 0; if (b > 255) b = 255;
    pal[i][0] = (uint8_t)r; pal[i][1] = (uint8_t)g; pal[i][2] = (uint8_t)b;
}

static uint32_t pack(int r, int g, int b)
{
    return ((uint32_t)(r >> (8 - vi.red.length))   << vi.red.offset)
         | ((uint32_t)(g >> (8 - vi.green.length)) << vi.green.offset)
         | ((uint32_t)(b >> (8 - vi.blue.length))  << vi.blue.offset);
}

/* fade: 0 black .. 1 normal; flash: 0 none .. 1 white */
static void build_lut(double fade, double flash)
{
    for (int i = 0; i < 256; i++) {
        double c[3];
        for (int k = 0; k < 3; k++) {
            double v = pal[i][k] * fade;
            c[k] = v + (255 - v) * flash;
        }
        lut[i] = pack((int)c[0], (int)c[1], (int)c[2]);
    }
}

/* ---- framebuffer ---------------------------------------------------------- */

static void fb_open(void)
{
    fb_fd = open(fbdev, O_RDWR);
    if (fb_fd < 0) die(fbdev);
    if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &vi) < 0) die("FBIOGET_VSCREENINFO");
    if (ioctl(fb_fd, FBIOGET_FSCREENINFO, &fi) < 0) die("FBIOGET_FSCREENINFO");
    bpp = vi.bits_per_pixel / 8;
    if (bpp != 2 && bpp != 3 && bpp != 4) {
        fprintf(stderr, "fbdemo: %d bpp framebuffers are not supported\n", vi.bits_per_pixel);
        exit(1);
    }
    fblen = (size_t)fi.line_length * vi.yres_virtual;
    fbmem = mmap(NULL, fblen, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
    if (fbmem == MAP_FAILED) die("mmap");
    fbmem += (size_t)vi.yoffset * fi.line_length + (size_t)vi.xoffset * bpp;

    rot = force_rot >= 0 ? force_rot : (vi.yres > vi.xres ? 90 : 0);
    LW = rot ? (int)vi.yres : (int)vi.xres;
    LH = rot ? (int)vi.xres : (int)vi.yres;
    IW = LW / scale; IH = LH / scale;
    buf = calloc((size_t)IW * IH, 1);
    row = calloc((size_t)LW, sizeof *row);
    if (!buf || !row) die("calloc");
}

static void put(uint8_t *p, uint32_t v)
{
    switch (bpp) {
    case 2: *(uint16_t *)p = (uint16_t)v; break;
    case 4: *(uint32_t *)p = v; break;
    default: p[0] = v; p[1] = v >> 8; p[2] = v >> 16; break;
    }
}

static void blit(void)
{
    const size_t ll = fi.line_length;
    for (int y = 0; y < IH; y++) {
        const uint8_t *src = buf + (size_t)y * IW;
        /* expand one chunky row to LW packed pixels */
        for (int x = 0, o = 0; x < IW; x++) {
            uint32_t v = lut[src[x]];
            for (int k = 0; k < scale; k++) row[o++] = v;
        }
        for (int dy = 0; dy < scale; dy++) {
            int ly = y * scale + dy;
            if (rot == 0) {
                uint8_t *dst = fbmem + (size_t)ly * ll;
                if (bpp == 4) memcpy(dst, row, (size_t)LW * 4);
                else if (bpp == 2) { uint16_t *d = (uint16_t *)dst; for (int x = 0; x < LW; x++) d[x] = (uint16_t)row[x]; }
                else for (int x = 0; x < LW; x++) put(dst + x * 3, row[x]);
            } else {
                /* logical (lx, ly) -> framebuffer column/row */
                for (int lx = 0; lx < LW; lx++) {
                    int fx = rot == 90 ? LH - 1 - ly : ly;
                    int fy = rot == 90 ? lx : LW - 1 - lx;
                    put(fbmem + (size_t)fy * ll + (size_t)fx * bpp, row[lx]);
                }
            }
        }
    }
}

/* ---- console / keyboard --------------------------------------------------- */

static void term_setup(void)
{
    /* hide the text console's cursor and stop it scribbling (only on a VT) */
    tty_fd = open("/dev/tty0", O_RDWR);
    if (tty_fd >= 0) {
        int mode;
        if (ioctl(tty_fd, KDGETMODE, &mode) == 0 && mode == KD_TEXT &&
            ioctl(tty_fd, KDSETMODE, KD_GRAPHICS) == 0)
            tty_was_text = 1;
    }
    if (isatty(0) && tcgetattr(0, &tio_saved) == 0) {
        struct termios t = tio_saved;
        t.c_lflag &= ~(ICANON | ECHO);
        t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &t);
        tio_ok = 1;
    }
}

static void term_restore(void)
{
    if (tio_ok) tcsetattr(0, TCSANOW, &tio_saved);
    if (tty_fd >= 0) {
        if (tty_was_text) ioctl(tty_fd, KDSETMODE, KD_TEXT);
        close(tty_fd);
    }
}

static void poll_keys(void)
{
    char c;
    while (tio_ok && read(0, &c, 1) == 1)
        if (c == 'q' || c == 'Q' || c == 27) quit = 1;
}

/* ---- tables --------------------------------------------------------------- */

static int8_t sintab[256];      /* -127..127 */
static uint8_t tex[256][256];   /* 0..47 */
static uint16_t *tun_ang, *tun_dep; static uint8_t *tun_shade;

static void tables(void)
{
    for (int i = 0; i < 256; i++) sintab[i] = (int8_t)lrint(127 * sin(i * 2 * M_PI / 256));
    /* a texture with some structure: xor pattern + rings, 48 levels */
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) {
            int v = ((x ^ y) & 63) + (sintab[(x * 2) & 255] + sintab[(y * 3) & 255]) / 8;
            v = (v & 63) * 47 / 63;
            tex[y][x] = (uint8_t)v;
        }
    size_t n = (size_t)IW * IH;
    tun_ang = malloc(n * 2); tun_dep = malloc(n * 2); tun_shade = malloc(n);
    if (!tun_ang || !tun_dep || !tun_shade) die("malloc");
    for (int y = 0; y < IH; y++)
        for (int x = 0; x < IW; x++) {
            double dx = x - IW / 2.0, dy = (y - IH / 2.0) * 1.0;
            double r = sqrt(dx * dx + dy * dy) + 0.5;
            size_t i = (size_t)y * IW + x;
            tun_ang[i] = (uint16_t)((int)(256 * (atan2(dy, dx) / (2 * M_PI) + 0.5) * 2) & 255);
            tun_dep[i] = (uint16_t)((int)(32.0 * IH / r) & 255);
            int s = (int)(r / (IW / 2.0) * 6);  /* 0 dark centre .. 3 bright edge */
            tun_shade[i] = (uint8_t)(s > 3 ? 3 : s);
        }
}

/* ---- palettes ------------------------------------------------------------- */

static void pal_effect(int scene, double t)
{
    for (int i = 0; i < 192; i++) {
        double a = i / 192.0 * 2 * M_PI;
        switch (scene) {
        case 0: /* plasma: cycling rainbow */
            setpal(i, 128 + 127 * sin(a + t * 0.9), 128 + 127 * sin(a * 2 + 2.1 + t * 0.7),
                      128 + 127 * sin(a * 3 + 4.2 + t * 0.5));
            break;
        default: { /* tunnel / roto: 4 shades x 48 levels */
            int shade = i / 48, lv = i % 48;
            double k = (shade + 1) / 4.0, u = lv / 47.0;
            if (scene == 1) setpal(i, k * (40 + 215 * u), k * (20 + 120 * u * u), k * (90 + 165 * (1 - u)));
            else            setpal(i, k * (255 * u), k * (60 + 195 * u), k * (40 + 60 * u));
        } }
    }
    /* 192..223: copper bars, 4 bars x 8-step gradients */
    static const double hue[4][3] = { {255, 60, 60}, {60, 255, 120}, {80, 140, 255}, {255, 210, 60} };
    for (int b = 0; b < 4; b++)
        for (int s = 0; s < 8; s++) {
            double k = sin((s + 0.5) / 8 * M_PI);
            setpal(192 + b * 8 + s, hue[b][0] * k + 40 * k * k, hue[b][1] * k + 40 * k * k, hue[b][2] * k + 40 * k * k);
        }
    setpal(224, 0, 0, 0);
    for (int i = 0; i < 31; i++) {
        double a = i / 31.0 * 2 * M_PI + t * 3;
        setpal(225 + i, 200 + 55 * sin(a), 200 + 55 * sin(a + 2.1), 200 + 55 * sin(a + 4.2));
    }
}

/* ---- effects -------------------------------------------------------------- */

static void fx_plasma(double t)
{
    int t1 = (int)(t * 60), t2 = (int)(t * 47), t3 = (int)(t * 31);
    for (int y = 0; y < IH; y++) {
        uint8_t *p = buf + (size_t)y * IW;
        int sy = sintab[(y * 3 + t2) & 255];
        for (int x = 0; x < IW; x++) {
            int v = sintab[(x + t1) & 255] + sy + sintab[((x + y) * 2 + t3) & 255]
                  + sintab[(x * y / 32 + t1 / 2) & 255];
            p[x] = (uint8_t)(((v + 512) * 191 / 1024));
        }
    }
}

static void fx_tunnel(double t)
{
    int du = (int)(t * 90), dv = (int)(t * 25) + sintab[(int)(t * 20) & 255] / 2;
    size_t n = (size_t)IW * IH;
    for (size_t i = 0; i < n; i++)
        buf[i] = (uint8_t)(tun_shade[i] * 48 + tex[(tun_dep[i] + du) & 255][(tun_ang[i] + dv) & 255]);
}

static void fx_roto(double t)
{
    double a = t * 0.6, z = 0.6 + 0.45 * sin(t * 0.8);
    int ca = (int)(cos(a) * z * 256), sa = (int)(sin(a) * z * 256);
    int cx = IW / 2, cy = IH / 2;
    int ox = (int)(t * 40 * 256), oy = (int)(t * 13 * 256);
    for (int y = 0; y < IH; y++) {
        uint8_t *p = buf + (size_t)y * IW;
        int dy = y - cy;
        int u = (-cx) * ca - dy * sa + ox, v = (-cx) * sa + dy * ca + oy;
        for (int x = 0; x < IW; x++) {
            int sx = (u >> 8) & 255, sy = (v >> 8) & 255;
            int shade = ((sx ^ sy) & 32) ? 3 : 2;
            p[x] = (uint8_t)(shade * 48 + tex[sy][sx]);
            u += ca; v += sa;
        }
    }
}

#define NSTARS 300
static struct { float x, y, z; } star[NSTARS];

static void fx_copper(double t, double dt)
{
    /* dark blue vertical gradient as background, using effect palette slot 0..47 */
    for (int i = 0; i < 48; i++) setpal(i, 0, 0, 10 + i * 1.2);
    for (int y = 0; y < IH; y++) memset(buf + (size_t)y * IW, y * 47 / IH, IW);
    /* parallax stars: palette 48..51, brighter when nearer */
    for (int i = 0; i < 4; i++) setpal(48 + i, 90 + 55 * i, 90 + 55 * i, 110 + 48 * i);
    for (int i = 0; i < NSTARS; i++) {
        star[i].x -= (float)(dt * (20 + 120 * star[i].z));
        if (star[i].x < 0) { star[i].x += IW; star[i].y = (float)(rand() % IH); }
        int x = (int)star[i].x, y = (int)star[i].y;
        if (x >= 0 && x < IW && y >= 0 && y < IH) buf[(size_t)y * IW + x] = (uint8_t)(48 + (int)(star[i].z * 3.99));
    }
    /* four bars bouncing on sines; later bars draw over earlier ones */
    for (int b = 0; b < 4; b++) {
        int h = IH / 10 > 8 ? IH / 10 : 8;
        int y0 = IH / 2 + (int)((IH / 2 - h) * sin(t * 1.7 + b * 0.8)) - h / 2;
        for (int k = 0; k < h; k++) {
            int y = y0 + k;
            if (y < 0 || y >= IH) continue;
            memset(buf + (size_t)y * IW, 192 + b * 8 + k * 8 / h, IW);
        }
    }
}

/* ---- scroller ------------------------------------------------------------- */

static const char *scrolltext =
    "      ZINE MACHINE V0   *   960 X 320 OF PURE BAR   *   POCKETBEAGLE 2 INSIDE   *   "
    "RGB565 OVER DPI, NO GPU NEEDED   *   PLASMA... TUNNEL... ROTOZOOM... COPPER!   *   "
    "GREETINGS TO BANCOUVER40, BEAGLEBOARD.ORG, KICAD, FREECAD, JSLINUX AND THE BACKPLANE CREW   *   "
    "PRESS Q TO QUIT   *   ";

static void scroller(double t)
{
    int sz = IH / 5 / 8; if (sz < 1) sz = 1;     /* glyph scale: ~1/5 of the height */
    int gw = 8 * sz;
    int len = (int)strlen(scrolltext);
    double speed = IW / 5.0;                      /* px/s at internal resolution */
    int off = (int)(t * speed);
    int first = off / gw, sub = off % gw;
    int base = IH - gw - IH / 8;                  /* baseline above the bottom */
    for (int c = 0; c * gw - sub < IW; c++) {
        unsigned char ch = (unsigned char)scrolltext[(first + c) % len];
        if (ch >= 128) ch = '?';
        const char *g = font8x8_basic[ch];
        int x0 = c * gw - sub;
        for (int gx = 0; gx < 8; gx++) {
            for (int px = 0; px < sz; px++) {
                int x = x0 + gx * sz + px;
                if (x < 0 || x >= IW) continue;
                int wob = (int)(sintab[((x * 256 / IW) * 2 + (int)(t * 120)) & 255] * (IH / 6) / 127);
                for (int gy = 0; gy < 8; gy++) {
                    if (!(g[gy] & (1 << gx))) continue;
                    for (int py = 0; py < sz; py++) {
                        int y = base + wob + gy * sz + py;
                        if (y + 1 < 0 || y + 1 >= IH || x + 1 >= IW) continue;
                        buf[(size_t)(y + 1) * IW + x + 1] = 224;          /* shadow */
                        if (y >= 0) buf[(size_t)y * IW + x] = (uint8_t)(225 + (gy * sz + py) * 30 / gw);
                    }
                }
            }
        }
    }
}

/* tiny fps readout, top-left, 1x font */
static void draw_text(int x, int y, const char *s, uint8_t c)
{
    for (; *s; s++, x += 8)
        for (int gy = 0; gy < 8; gy++)
            for (int gx = 0; gx < 8; gx++)
                if ((font8x8_basic[(unsigned char)*s & 127][gy] & (1 << gx)) &&
                    x + gx < IW && y + gy < IH)
                    buf[(size_t)(y + gy) * IW + x + gx] = c;
}

/* ---- main ----------------------------------------------------------------- */

static void usage(void)
{
    fprintf(stderr, "usage: fbdemo [-d /dev/fb0] [-s scale 1..8] [-t seconds] [-r 0|90|270] [-f]\n");
    exit(2);
}

int main(int argc, char **argv)
{
    int o;
    while ((o = getopt(argc, argv, "d:s:t:r:fh")) != -1) {
        switch (o) {
        case 'd': fbdev = optarg; break;
        case 's': scale = atoi(optarg); if (scale < 1 || scale > 8) usage(); break;
        case 't': run_for = atof(optarg); break;
        case 'r': force_rot = atoi(optarg); if (force_rot != 0 && force_rot != 90 && force_rot != 270) usage(); break;
        case 'f': show_fps = 1; break;
        default: usage();
        }
    }

    fb_open();
    fprintf(stderr, "fbdemo: %s %ux%u %dbpp (r%u g%u b%u), drawing %dx%d at 1/%d, rotation %d\n",
            fbdev, vi.xres, vi.yres, vi.bits_per_pixel, vi.red.length, vi.green.length, vi.blue.length,
            LW, LH, scale, rot);

    signal(SIGINT, on_signal); signal(SIGTERM, on_signal); signal(SIGHUP, on_signal);
    term_setup();
    tables();
    for (int i = 0; i < NSTARS; i++) {
        star[i].x = (float)(rand() % IW); star[i].y = (float)(rand() % IH);
        star[i].z = (float)(rand() % 1000) / 1000.0f;
    }

    const double SCENE = 12.0, FADE = 0.6;
    double t0 = now(), last = t0, fps_t = t0, fps = 0;
    int frames = 0;
    char fpsbuf[32] = "";

    while (!quit) {
        double tn = now(), t = tn - t0, dt = tn - last;
        last = tn;
        if (run_for > 0 && t >= run_for) break;

        int scene = (int)(t / SCENE) % 4;
        double st = fmod(t, SCENE);
        double fade = 1.0, flash = 0.0;
        if (st < FADE) fade = st / FADE;
        if (st > SCENE - FADE) fade = (SCENE - st) / FADE;
        if (st < 0.25) flash = 1.0 - st / 0.25;   /* white flash on each cut */

        pal_effect(scene == 3 ? 3 : scene, t);
        switch (scene) {
        case 0: fx_plasma(t); break;
        case 1: fx_tunnel(t); break;
        case 2: fx_roto(t); break;
        default: fx_copper(t, dt); break;
        }
        scroller(t);
        if (show_fps) {
            setpal(255, 255, 255, 255);
            draw_text(2, 2, fpsbuf, 255);
        }
        build_lut(fade, flash * fade);
        blit();

        frames++;
        if (tn - fps_t >= 1.0) {
            fps = frames / (tn - fps_t);
            snprintf(fpsbuf, sizeof fpsbuf, "%.1f FPS", fps);
            frames = 0; fps_t = tn;
        }
        poll_keys();
    }

    /* leave a clean black screen */
    memset(buf, 224, (size_t)IW * IH);
    build_lut(1, 0);
    blit();
    term_restore();
    return 0;
}
