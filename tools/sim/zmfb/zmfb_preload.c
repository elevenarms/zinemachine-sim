/*
 * libzmfb.so: a fake /dev/fb0 for the display simulator (LD_PRELOAD).
 *
 * open("/dev/fb0") returns a POSIX shared-memory object (ZMFB_SHM, default /zmfb)
 * sized for the panel; the fbdev ioctls an app needs are answered for a
 * ZMFB_W x ZMFB_H (960x320) framebuffer at ZMFB_BPP (16 = RGB565, as on the
 * ST7701S over DPI; 32 = XRGB8888). mmap() of that fd is a plain shm mapping,
 * so apps draw exactly as they would on the device. zmfb-run shows the memory.
 *
 * Only dynamically linked programs can be intercepted (static binaries ignore
 * LD_PRELOAD): build the simulator copy of an app without -static.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define MAXFD 4096
static unsigned char is_fb[MAXFD];

static int W = 960, H = 320, BPP = 16;
static const char *SHM = "/zmfb";
static int cfg_done;

static void cfg(void)
{
    if (cfg_done) return;
    const char *s;
    if ((s = getenv("ZMFB_W")))   W = atoi(s);
    if ((s = getenv("ZMFB_H")))   H = atoi(s);
    if ((s = getenv("ZMFB_BPP"))) BPP = atoi(s) == 32 ? 32 : 16;
    if ((s = getenv("ZMFB_SHM"))) SHM = s;
    cfg_done = 1;
}

static int is_fb_path(const char *p)
{
    return p && (!strcmp(p, "/dev/fb0") || !strcmp(p, "/dev/fb/0") || !strcmp(p, "/dev/graphics/fb0"));
}

static int open_fb(void)
{
    cfg();
    int fd = shm_open(SHM, O_RDWR | O_CREAT, 0600);
    if (fd < 0) return -1;
    off_t len = (off_t)W * H * (BPP / 8);
    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size < len && ftruncate(fd, len) < 0) { close(fd); return -1; }
    if (fd < MAXFD) is_fb[fd] = 1;
    return fd;
}

#define REAL(name, ret, ...) \
    static ret (*real_##name)(__VA_ARGS__); \
    if (!real_##name) real_##name = (ret (*)(__VA_ARGS__))dlsym(RTLD_NEXT, #name)

static mode_t va_mode(int flags, va_list ap)
{
    return (flags & (O_CREAT | __O_TMPFILE)) ? (mode_t)va_arg(ap, int) : 0;
}

int open(const char *p, int flags, ...)
{
    va_list ap; va_start(ap, flags); mode_t m = va_mode(flags, ap); va_end(ap);
    if (is_fb_path(p)) return open_fb();
    REAL(open, int, const char *, int, ...);
    return real_open(p, flags, m);
}
int open64(const char *p, int flags, ...)
{
    va_list ap; va_start(ap, flags); mode_t m = va_mode(flags, ap); va_end(ap);
    if (is_fb_path(p)) return open_fb();
    REAL(open64, int, const char *, int, ...);
    return real_open64(p, flags, m);
}
int openat(int dir, const char *p, int flags, ...)
{
    va_list ap; va_start(ap, flags); mode_t m = va_mode(flags, ap); va_end(ap);
    if (is_fb_path(p)) return open_fb();
    REAL(openat, int, int, const char *, int, ...);
    return real_openat(dir, p, flags, m);
}
int openat64(int dir, const char *p, int flags, ...)
{
    va_list ap; va_start(ap, flags); mode_t m = va_mode(flags, ap); va_end(ap);
    if (is_fb_path(p)) return open_fb();
    REAL(openat64, int, int, const char *, int, ...);
    return real_openat64(dir, p, flags, m);
}
int __open_2(const char *p, int flags)
{
    if (is_fb_path(p)) return open_fb();
    REAL(__open_2, int, const char *, int);
    return real___open_2(p, flags);
}
int __open64_2(const char *p, int flags)
{
    if (is_fb_path(p)) return open_fb();
    REAL(__open64_2, int, const char *, int);
    return real___open64_2(p, flags);
}

int close(int fd)
{
    if (fd >= 0 && fd < MAXFD) is_fb[fd] = 0;
    REAL(close, int, int);
    return real_close(fd);
}

static void fill_var(struct fb_var_screeninfo *v)
{
    memset(v, 0, sizeof *v);
    v->xres = v->xres_virtual = W;
    v->yres = v->yres_virtual = H;
    v->bits_per_pixel = BPP;
    if (BPP == 16) {
        v->red   = (struct fb_bitfield){11, 5, 0};
        v->green = (struct fb_bitfield){5, 6, 0};
        v->blue  = (struct fb_bitfield){0, 5, 0};
    } else {
        v->red   = (struct fb_bitfield){16, 8, 0};
        v->green = (struct fb_bitfield){8, 8, 0};
        v->blue  = (struct fb_bitfield){0, 8, 0};
    }
    v->height = 37;  /* mm: 36.77 x 110.3 active area */
    v->width = 110;
    v->pixclock = 39722;
    v->activate = FB_ACTIVATE_NOW;
    v->vmode = FB_VMODE_NONINTERLACED;
}

static void fill_fix(struct fb_fix_screeninfo *f)
{
    memset(f, 0, sizeof *f);
    strncpy(f->id, "zmfb", sizeof f->id);
    f->smem_len = (uint32_t)(W * H * (BPP / 8));
    f->type = FB_TYPE_PACKED_PIXELS;
    f->visual = FB_VISUAL_TRUECOLOR;
    f->line_length = (uint32_t)(W * (BPP / 8));
    f->accel = FB_ACCEL_NONE;
}

static void wait_vsync(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    long long ns = ts.tv_sec * 1000000000LL + ts.tv_nsec, period = 16666667LL;
    long long next = (ns / period + 1) * period;
    ts.tv_sec = next / 1000000000LL; ts.tv_nsec = next % 1000000000LL;
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL);
}

int ioctl(int fd, unsigned long req, ...)
{
    va_list ap; va_start(ap, req); void *arg = va_arg(ap, void *); va_end(ap);
    if (fd >= 0 && fd < MAXFD && is_fb[fd]) {
        switch (req) {
        case FBIOGET_VSCREENINFO: fill_var(arg); return 0;
        case FBIOPUT_VSCREENINFO: fill_var(arg); return 0;   /* fixed mode: report what we are */
        case FBIOGET_FSCREENINFO: fill_fix(arg); return 0;
        case FBIOPAN_DISPLAY:     return 0;
        case FBIOBLANK:           return 0;
        case FBIO_WAITFORVSYNC:   wait_vsync(); return 0;
        default: errno = ENOTTY; return -1;
        }
    }
    REAL(ioctl, int, int, unsigned long, ...);
    return real_ioctl(fd, req, arg);
}
