/*
 * eyechart: pick a readable console font for the bar display.
 *
 * Draws the same line in every Terminus bitmap size the console can use (12..32 px),
 * with what that size gives on the 960x320 panel: the terminal grid and the physical
 * character height at 8.70 px/mm (110.3 mm across 960 px). Uses the X server's own
 * Terminus fonts, so it is pixel-for-pixel what `zm console -s PX` shows (and what
 * `setfont ter-vPXn` would show on the device).
 *
 *   eyechart [TEXT]     q / Esc quits, space toggles dark/light
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <string.h>

#define W 960
#define H 320
#define PX_PER_MM (960.0 / 110.3)

static const struct { int px, cw; } sizes[] = {
    {12, 6}, {14, 8}, {16, 8}, {18, 10}, {20, 10}, {22, 11}, {24, 12}, {28, 14}, {32, 16},
};
#define N (int)(sizeof sizes / sizeof sizes[0])

int main(int argc, char **argv)
{
    const char *sample = argc > 1 ? argv[1] : "The quick brown fox jumps 0O 1lI| {}[] ~$ ls -la";
    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "eyechart: no X display\n"); return 1; }
    int scr = DefaultScreen(d);
    Window w = XCreateSimpleWindow(d, RootWindow(d, scr), 0, 0, W, H, 0, 0, BlackPixel(d, scr));
    XStoreName(d, w, "eyechart");
    XSelectInput(d, w, ExposureMask | KeyPressMask);
    XMapRaised(d, w);
    GC gc = XCreateGC(d, w, 0, NULL);
    Colormap cm = DefaultColormap(d, scr);
    XColor fg, dim, acc, bg, tmp;
    XAllocNamedColor(d, cm, "#d8d8d8", &fg, &tmp);
    XAllocNamedColor(d, cm, "#6a6a6a", &dim, &tmp);
    XAllocNamedColor(d, cm, "#ffd040", &acc, &tmp);
    XAllocNamedColor(d, cm, "#000000", &bg, &tmp);

    XFontStruct *f[N], *label = XLoadQueryFont(d, "-*-terminus-medium-r-normal--12-*-*-*-c-60-iso10646-1");
    for (int i = 0; i < N; i++) {
        char name[128];
        snprintf(name, sizeof name, "-*-terminus-medium-r-normal--%d-*-*-*-c-%d-iso10646-1", sizes[i].px, sizes[i].cw * 10);
        f[i] = XLoadQueryFont(d, name);
        if (!f[i]) fprintf(stderr, "eyechart: missing font %s\n", name);
    }
    if (!label) label = f[0];

    int light = 0;
    for (;;) {
        XEvent e; XNextEvent(d, &e);
        if (e.type == KeyPress) {
            KeySym ks = XLookupKeysym(&e.xkey, 0);
            if (ks == XK_q || ks == XK_Escape) break;
            if (ks == XK_space) light = !light;
        } else if (e.type != Expose || e.xexpose.count) continue;

        unsigned long cbg = light ? fg.pixel : bg.pixel, ctext = light ? bg.pixel : fg.pixel;
        XSetForeground(d, gc, cbg);
        XFillRectangle(d, w, gc, 0, 0, W, H);
        /* rows top to bottom, smallest first; total height 186 px + gaps */
        int y = 4, gap = (H - 8 - 186) / (N - 1);
        for (int i = 0; i < N; i++) {
            int px = sizes[i].px, cw = sizes[i].cw;
            char info[64];
            snprintf(info, sizeof info, "%2dpx %3dx%-2d %.1fmm", px, W / cw, H / px, px / PX_PER_MM);
            /* label column (12 px font), baseline aligned with the sample row */
            XSetFont(d, gc, label->fid);
            XSetForeground(d, gc, px == 24 ? acc.pixel : dim.pixel);
            int base = y + f[i]->ascent;
            XDrawString(d, w, gc, 4, base, info, (int)strlen(info));
            /* sample in that size, cut to what fits after the label */
            if (f[i]) {
                XSetFont(d, gc, f[i]->fid);
                XSetForeground(d, gc, ctext);
                int x0 = 4 + 6 * 22, fit = (W - x0 - 4) / cw;
                int len = (int)strlen(sample); if (len > fit) len = fit;
                XDrawString(d, w, gc, x0, base, sample, len);
            }
            y += px + gap;
        }
        XFlush(d);
    }
    XCloseDisplay(d);
    return 0;
}
