/*
 * life: Conway's Game of Life on the 960x320 bar (SDL2), 240x80 cells of 4x4 px,
 * wrapping edges. Cells are coloured by age (newborn white -> cyan -> violet), dead
 * cells leave a fading trail. Reseeds when the board goes still or after 60 s.
 * Keys: space reseed, p pause, +/- speed, g glider gun, q/Esc quit.
 */
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 960
#define H 320
#define CS 4
#define GW (W / CS)
#define GH (H / CS)

static unsigned char cur[GH][GW], nxt[GH][GW];
static unsigned short age[GH][GW];
static unsigned char trail[GH][GW];

static void seed(int density)
{
    memset(age, 0, sizeof age);
    for (int y = 0; y < GH; y++)
        for (int x = 0; x < GW; x++)
            cur[y][x] = (rand() % 100) < density;
}

static void gun(int ox, int oy)
{
    static const char *g[] = {
        "........................O...........",
        "......................O.O...........",
        "............OO......OO............OO",
        "...........O...O....OO............OO",
        "OO........O.....O...OO..............",
        "OO........O...O.OO....O.O...........",
        "..........O.....O.......O...........",
        "...........O...O....................",
        "............OO......................",
    };
    for (int y = 0; y < 9; y++)
        for (int x = 0; g[y][x]; x++)
            if (g[y][x] == 'O') cur[(oy + y) % GH][(ox + x) % GW] = 1;
}

static int step(void)
{
    int changed = 0;
    for (int y = 0; y < GH; y++) {
        int ym = (y + GH - 1) % GH, yp = (y + 1) % GH;
        for (int x = 0; x < GW; x++) {
            int xm = (x + GW - 1) % GW, xp = (x + 1) % GW;
            int n = cur[ym][xm] + cur[ym][x] + cur[ym][xp] + cur[y][xm] + cur[y][xp]
                  + cur[yp][xm] + cur[yp][x] + cur[yp][xp];
            int alive = cur[y][x] ? (n == 2 || n == 3) : (n == 3);
            nxt[y][x] = (unsigned char)alive;
            if (alive) { if (age[y][x] < 65535) age[y][x]++; }
            else { if (cur[y][x]) trail[y][x] = 255; age[y][x] = 0; }
            changed += alive != cur[y][x];
        }
    }
    memcpy(cur, nxt, sizeof cur);
    return changed;
}

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO)) { fprintf(stderr, "%s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("life", 0, 0, W, H, SDL_WINDOW_FULLSCREEN_DESKTOP);
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(r, W, H);
    SDL_Texture *tex = SDL_CreateTexture(r, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, GW, GH);
    srand((unsigned)SDL_GetTicks() ^ 0x5eed);
    seed(28);

    int running = 1, paused = 0, delay = 60, still = 0;
    Uint32 last = 0, born = SDL_GetTicks(), gen = 0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) switch (e.key.keysym.sym) {
                case SDLK_q: case SDLK_ESCAPE: running = 0; break;
                case SDLK_SPACE: seed(28); gen = 0; born = SDL_GetTicks(); break;
                case SDLK_p: paused = !paused; break;
                case SDLK_g: gun(rand() % GW, rand() % GH); break;
                case SDLK_PLUS: case SDLK_EQUALS: case SDLK_KP_PLUS: if (delay > 0) delay -= 20; break;
                case SDLK_MINUS: case SDLK_KP_MINUS: delay += 20; break;
            }
        }
        Uint32 now = SDL_GetTicks();
        if (!paused && now - last >= (Uint32)delay) {
            last = now; gen++;
            int ch = step();
            still = ch < 6 ? still + 1 : 0;
            if (still > 40 || now - born > 60000) { seed(28); gen = 0; born = now; still = 0; printf("reseed\n"); fflush(stdout); }
        }
        /* draw cells into a 240x80 RGB565 texture, scaled 4x by the renderer */
        void *px; int pitch;
        SDL_LockTexture(tex, NULL, &px, &pitch);
        for (int y = 0; y < GH; y++) {
            Uint16 *row = (Uint16 *)((Uint8 *)px + y * pitch);
            for (int x = 0; x < GW; x++) {
                int R, G, B;
                if (cur[y][x]) {
                    int a = age[y][x] > 120 ? 120 : age[y][x];
                    if (a < 4) { R = 255; G = 255; B = 255; }
                    else if (a < 40) { R = 40; G = 230 - a * 2; B = 255; }
                    else { R = 120 + (a - 40); G = 60; B = 255 - (a - 40); }
                } else {
                    int t = trail[y][x];
                    R = t / 5; G = t / 3; B = t / 2;
                    if (trail[y][x] > 6) trail[y][x] -= 6; else trail[y][x] = 0;
                }
                row[x] = (Uint16)(((R >> 3) << 11) | ((G >> 2) << 5) | (B >> 3));
            }
        }
        SDL_UnlockTexture(tex);
        SDL_RenderCopy(r, tex, NULL, NULL);
        SDL_RenderPresent(r);
    }
    SDL_Quit();
    return 0;
}
