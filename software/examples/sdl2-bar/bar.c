/*
 * sdl2-bar: minimal SDL2 app for the 960x320 bar display; a template for SDL2 apps.
 *
 * A full-screen window at the panel resolution, an equalizer-style animation and
 * keyboard input (the device's keyboard is a USB HID on the PocketBeagle): arrow
 * keys move the cursor block, any key flashes the bar under it, q / Esc quits.
 *
 * In tools/sim it draws on the simulator's X display; on the device it would run
 * with SDL_VIDEODRIVER=kmsdrm straight on the panel. Same code either way.
 */
#include <SDL2/SDL.h>
#include <math.h>
#include <stdio.h>

#define W 960
#define H 320
#define BARS 48

int main(void)
{
    if (SDL_Init(SDL_INIT_VIDEO) != 0) { fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window *win = SDL_CreateWindow("sdl2-bar", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                       W, H, SDL_WINDOW_FULLSCREEN_DESKTOP);
    if (!win) { fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer *r = SDL_CreateRenderer(win, -1, SDL_RENDERER_PRESENTVSYNC);
    if (!r) r = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_RenderSetLogicalSize(r, W, H);   /* draw in panel pixels whatever the window is */

    int sw, sh; SDL_GetWindowSize(win, &sw, &sh);
    printf("sdl2-bar: window %dx%d, driver %s\n", sw, sh, SDL_GetCurrentVideoDriver());
    fflush(stdout);

    float flash[BARS] = {0};
    int cur = BARS / 2, running = 1;
    Uint32 t0 = SDL_GetTicks(), frames = 0, fps_t = t0;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN) {
                SDL_Keycode k = e.key.keysym.sym;
                if (k == SDLK_q || k == SDLK_ESCAPE) running = 0;
                else if (k == SDLK_LEFT)  cur = (cur + BARS - 1) % BARS;
                else if (k == SDLK_RIGHT) cur = (cur + 1) % BARS;
                else flash[cur] = 1.0f;
                printf("key %s\n", SDL_GetKeyName(k)); fflush(stdout);
            }
        }
        float t = (SDL_GetTicks() - t0) / 1000.0f;
        SDL_SetRenderDrawColor(r, 8, 8, 14, 255);
        SDL_RenderClear(r);

        int bw = W / BARS;
        for (int i = 0; i < BARS; i++) {
            float v = 0.5f + 0.25f * sinf(t * 3.1f + i * 0.35f) + 0.2f * sinf(t * 1.7f - i * 0.9f);
            v = v < 0.05f ? 0.05f : v > 1 ? 1 : v;
            int h = (int)(v * (H - 40));
            float hue = (float)i / BARS;
            Uint8 cr = (Uint8)(128 + 127 * sinf(6.283f * hue)), cg = (Uint8)(128 + 127 * sinf(6.283f * hue + 2.1f)),
                  cb = (Uint8)(128 + 127 * sinf(6.283f * hue + 4.2f));
            float f = flash[i];
            SDL_SetRenderDrawColor(r, (Uint8)(cr + (255 - cr) * f), (Uint8)(cg + (255 - cg) * f), (Uint8)(cb + (255 - cb) * f), 255);
            SDL_Rect rc = { i * bw + 2, H - 20 - h, bw - 4, h };
            SDL_RenderFillRect(r, &rc);
            flash[i] *= 0.9f;
        }
        SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
        SDL_Rect c = { cur * bw + 2, H - 14, bw - 4, 8 };
        SDL_RenderFillRect(r, &c);
        SDL_RenderPresent(r);

        frames++;
        if (SDL_GetTicks() - fps_t >= 5000) {
            printf("%.1f fps\n", frames * 1000.0 / (SDL_GetTicks() - fps_t)); fflush(stdout);
            frames = 0; fps_t = SDL_GetTicks();
        }
    }
    SDL_DestroyRenderer(r); SDL_DestroyWindow(win); SDL_Quit();
    return 0;
}
