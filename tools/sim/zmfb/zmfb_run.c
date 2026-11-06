/*
 * zmfb-run: run a framebuffer app against the simulator's fake /dev/fb0 and show it.
 *
 *   zmfb-run [--] CMD [ARGS...]
 *
 * - starts CMD on a pseudo-terminal with LD_PRELOAD=libzmfb.so (unless already set),
 *   so its /dev/fb0 is the shared memory ZMFB_SHM (default /zmfb);
 * - shows that memory in a ZMFB_W x ZMFB_H X window (~60 Hz, only when it changed);
 * - turns X key presses into terminal bytes on the app's stdin, like a keyboard on
 *   the device's text console; the app's output goes to our stdout (the run log).
 * Exits with the app's status when it ends.
 */
#define _GNU_SOURCE
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef ZMFB_LIB
#define ZMFB_LIB "/opt/zm/lib/libzmfb.so"
#endif

static int W = 960, H = 320, BPP = 16;
static volatile sig_atomic_t stop;
static void on_sig(int s) { (void)s; stop = 1; }

static long long ms_now(void)
{
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000LL + ts.tv_nsec / 1000000;
}

/* X keysym -> bytes a Linux console would send */
static int key_bytes(KeySym ks, const char *txt, int n, char *out)
{
    const char *seq = NULL;
    switch (ks) {
    case XK_Up: seq = "\033[A"; break;      case XK_Down: seq = "\033[B"; break;
    case XK_Right: seq = "\033[C"; break;   case XK_Left: seq = "\033[D"; break;
    case XK_Home: seq = "\033[1~"; break;   case XK_End: seq = "\033[4~"; break;
    case XK_Insert: seq = "\033[2~"; break; case XK_Delete: seq = "\033[3~"; break;
    case XK_Page_Up: seq = "\033[5~"; break; case XK_Page_Down: seq = "\033[6~"; break;
    case XK_F1: seq = "\033[[A"; break;     case XK_F2: seq = "\033[[B"; break;
    case XK_F3: seq = "\033[[C"; break;     case XK_F4: seq = "\033[[D"; break;
    case XK_F5: seq = "\033[[E"; break;
    case XK_BackSpace: seq = "\177"; break;
    case XK_Return: case XK_KP_Enter: seq = "\r"; break;
    default: break;
    }
    if (seq) { int l = (int)strlen(seq); memcpy(out, seq, l); return l; }
    if (n > 0) { memcpy(out, txt, n); return n; }
    return 0;
}

int main(int argc, char **argv)
{
    int a = 1;
    if (a < argc && !strcmp(argv[a], "--")) a++;
    if (a >= argc) { fprintf(stderr, "usage: zmfb-run [--] CMD [ARGS...]\n"); return 2; }

    const char *s;
    if ((s = getenv("ZMFB_W"))) W = atoi(s);
    if ((s = getenv("ZMFB_H"))) H = atoi(s);
    if ((s = getenv("ZMFB_BPP"))) BPP = atoi(s) == 32 ? 32 : 16;
    const char *shm = getenv("ZMFB_SHM"); if (!shm) shm = "/zmfb";
    size_t len = (size_t)W * H * (BPP / 8);

    /* the framebuffer memory: fresh and black for each run */
    shm_unlink(shm);
    int fd = shm_open(shm, O_RDWR | O_CREAT, 0600);
    if (fd < 0 || ftruncate(fd, (off_t)len) < 0) { perror("zmfb-run: shm"); return 1; }
    uint8_t *fb = mmap(NULL, len, PROT_READ, MAP_SHARED, fd, 0);
    if (fb == MAP_FAILED) { perror("zmfb-run: mmap"); return 1; }

    Display *dpy = XOpenDisplay(NULL);
    if (!dpy) { fprintf(stderr, "zmfb-run: cannot open X display %s\n", getenv("DISPLAY")); return 1; }
    int scr = DefaultScreen(dpy);
    Visual *vis = DefaultVisual(dpy, scr);
    int depth = DefaultDepth(dpy, scr);
    Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, scr), 0, 0, W, H, 0, 0, BlackPixel(dpy, scr));
    XStoreName(dpy, win, "zmfb");
    XSelectInput(dpy, win, KeyPressMask | ExposureMask | StructureNotifyMask);
    XMapRaised(dpy, win);
    GC gc = DefaultGC(dpy, scr);

    /* image in the X server's format; converted from the fb format each frame */
    int xbpp = depth > 16 ? 32 : 16;
    uint8_t *ximg_data = malloc((size_t)W * H * (xbpp / 8));
    XImage *img = XCreateImage(dpy, vis, depth, ZPixmap, 0, (char *)ximg_data, W, H, xbpp, 0);
    if (!img) { fprintf(stderr, "zmfb-run: XCreateImage failed (depth %d)\n", depth); return 1; }

    /* the app, on a pty */
    if (!getenv("LD_PRELOAD")) setenv("LD_PRELOAD", ZMFB_LIB, 1);
    setenv("ZMFB_SHM", shm, 1);
    struct winsize ws = { .ws_row = 20, .ws_col = 120 };  /* fbcon 8x16 on 960x320 */
    int master;
    pid_t pid = forkpty(&master, NULL, NULL, &ws);
    if (pid < 0) { perror("zmfb-run: forkpty"); return 1; }
    if (pid == 0) {
        setenv("TERM", "linux", 1);
        execvp(argv[a], argv + a);
        fprintf(stderr, "zmfb-run: %s: %s\n", argv[a], strerror(errno));
        _exit(127);
    }
    unsetenv("LD_PRELOAD");   /* not for us */
    signal(SIGINT, on_sig); signal(SIGTERM, on_sig); signal(SIGHUP, on_sig);
    fcntl(master, F_SETFL, O_NONBLOCK);

    uint8_t *last = calloc(1, len);
    int status = 0, done = 0, dirty = 1;
    long long next = ms_now();
    while (!done) {
        struct pollfd p[2] = { { ConnectionNumber(dpy), POLLIN, 0 }, { master, POLLIN, 0 } };
        int to = (int)(next - ms_now()); if (to < 0) to = 0;
        poll(p, 2, to);

        if (p[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            char b[4096]; ssize_t r;
            while ((r = read(master, b, sizeof b)) > 0) fwrite(b, 1, (size_t)r, stdout);
            fflush(stdout);
        }
        while (XPending(dpy)) {
            XEvent ev; XNextEvent(dpy, &ev);
            if (ev.type == Expose) dirty = 1;
            else if (ev.type == KeyPress) {
                char txt[32], out[32]; KeySym ks;
                int n = XLookupString(&ev.xkey, txt, sizeof txt, &ks, NULL);
                int m = key_bytes(ks, txt, n, out);
                if (m > 0 && write(master, out, (size_t)m) < 0) { /* app gone */ }
            }
        }
        if (ms_now() >= next) {
            next += 16; if (next < ms_now()) next = ms_now() + 16;
            if (dirty || memcmp(fb, last, len)) {
                memcpy(last, fb, len);
                if (BPP == 16 && xbpp == 16) memcpy(ximg_data, fb, len);
                else {
                    for (int i = 0; i < W * H; i++) {
                        uint32_t r, g, b;
                        if (BPP == 16) { uint16_t v = ((uint16_t *)fb)[i]; r = (v >> 11) << 3; g = ((v >> 5) & 63) << 2; b = (v & 31) << 3; }
                        else { uint32_t v = ((uint32_t *)fb)[i]; r = (v >> 16) & 255; g = (v >> 8) & 255; b = v & 255; }
                        if (xbpp == 16) ((uint16_t *)ximg_data)[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
                        else ((uint32_t *)ximg_data)[i] = (r << 16) | (g << 8) | b;
                    }
                }
                XPutImage(dpy, win, gc, img, 0, 0, 0, 0, W, H);
                XFlush(dpy);
                dirty = 0;
            }
        }
        if (stop) kill(pid, SIGTERM), stop = 0;
        pid_t w = waitpid(pid, &status, WNOHANG);
        if (w == pid) done = 1;
    }
    /* drain the last output */
    char b[4096]; ssize_t r;
    while ((r = read(master, b, sizeof b)) > 0) fwrite(b, 1, (size_t)r, stdout);
    shm_unlink(shm);
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}
