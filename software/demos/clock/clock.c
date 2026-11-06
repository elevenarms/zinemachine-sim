/*
 * clock: a big pixel clock for the bar (SDL2). HH:MM:SS in the 8x8 font scaled to
 * 160 px tall, blocky LED-style pixels with a glow, date and a seconds sweep
 * underneath. Keys: c colour, q/Esc quit.
 */
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>
#include <time.h>
#include "../../fbdemo/font8x8_basic.h"

#define W 960
#define H 320

static const SDL_Color pal[] = { {255, 80, 40, 255}, {60, 255, 120, 255}, {80, 170, 255, 255}, {255, 210, 60, 255}, {240, 240, 255, 255} };

static void text(SDL_Renderer *r, const char *s, int x, int y, int px, SDL_Color c, int lit_only)
{
    for (; *s; s++, x += 8 * px) {
        const char *g = font8x8_basic[(unsigned char)*s & 127];
        for (int gy = 0; gy < 8; gy++)
            for (int gx = 0; gx < 8; gx++) {
                int on = (g[gy] >> gx) & 1;
                if (!on && lit_only) continue;
                SDL_Rect q = { x + gx * px + 1, y + gy * px + 1, px - 2, px - 2 };
                if (on) SDL_SetRenderDrawColor(r, c.r, c.g, c.b, 255);
                else SDL_SetRenderDrawColor(r, c.r / 14, c.g / 14, c.b / 14, 255);   /* unlit "LED" */
                SDL_RenderFillRect(r, &q);
            }
    }
}

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("clock", 0, 0, W, H, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(r, W, H);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    int ci = 1, running = 1;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_q || k == SDLK_ESCAPE) running = 0;
                if (k == SDLK_c) ci = (ci + 1) % 5;
            }
        }
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        struct tm tm; localtime_r(&ts.tv_sec, &tm);
        double frac = ts.tv_nsec / 1e9;
        char hms[16], date[40];
        int blink = frac < 0.5;
        snprintf(hms, sizeof hms, "%02d%c%02d%c%02d", tm.tm_hour, blink ? ':' : ' ', tm.tm_min, blink ? ':' : ' ', tm.tm_sec);
        strftime(date, sizeof date, "%a %d %b %Y", &tm);
        for (char *p = date; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;

        SDL_SetRenderDrawColor(r, 4, 4, 8, 255);
        SDL_RenderClear(r);
        SDL_Color c = pal[ci];
        int px = 13;                                   /* 8 chars * 8 * 13 = 832 px wide, 104 tall */
        int x0 = (W - 8 * 8 * px) / 2, y0 = 40;
        /* glow: a few dim, offset passes, then the lit pixels */
        for (int k = 3; k >= 1; k--) {
            SDL_Color gcol = { c.r, c.g, c.b, 255 };
            gcol.r = (Uint8)(c.r / (k * 4)); gcol.g = (Uint8)(c.g / (k * 4)); gcol.b = (Uint8)(c.b / (k * 4));
            text(r, hms, x0 - k, y0 - k, px, gcol, 1);
            text(r, hms, x0 + k, y0 + k, px, gcol, 1);
        }
        text(r, hms, x0, y0, px, c, 0);
        int dpx = 4;
        text(r, date, (W - (int)strlen(date) * 8 * dpx) / 2, 190, dpx, (SDL_Color){c.r / 2 + 60, c.g / 2 + 60, c.b / 2 + 60, 255}, 1);

        /* seconds sweep: 60 ticks across the bar, current one tracks the fraction */
        double pos = tm.tm_sec + frac;
        for (int i = 0; i < 60; i++) {
            int x = 40 + i * (W - 80) / 59;
            double d = fabs(i - pos); if (d > 30) d = 60 - d;
            double a = d < 1 ? 1 - d : (i < pos ? 0.35 : 0.12);
            SDL_SetRenderDrawColor(r, (Uint8)(c.r * a), (Uint8)(c.g * a), (Uint8)(c.b * a), 255);
            SDL_Rect q = { x - 3, 260 - (i % 5 == 0 ? 12 : 6), 6, i % 5 == 0 ? 24 : 12 };
            SDL_RenderFillRect(r, &q);
        }
        SDL_RenderPresent(r);
    }
    SDL_Quit();
    return 0;
}
