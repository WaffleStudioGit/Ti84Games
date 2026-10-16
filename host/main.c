#include "bot.h"
#include "game.h"

#include <SDL.h>
#include <SDL_ttf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_Window *win;
static SDL_Renderer *ren;
static TTF_Font *fonts[4];
static Rgb pal[COL_COUNT];
static int pal_level = -2;
static const char *font_path = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";

static void ensure_palette(void) {
    int lv = game_visual_level();
    if (lv == pal_level) return;
    pal_level = lv;
    palette_for(lv, pal);
}

static void h_rect(int x, int y, int w, int h, int col) {
    SDL_Rect r;
    if (w <= 0 || h <= 0) return;
    if (col < 0 || col >= COL_COUNT) return;
    SDL_SetRenderDrawColor(ren, pal[col].r, pal[col].g, pal[col].b, 255);
    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    SDL_RenderFillRect(ren, &r);
}

static void h_tri(int x0, int y0, int x1, int y1, int x2, int y2, int col) {
    SDL_Vertex v[3];
    SDL_Color c;
    int i;
    if (col < 0 || col >= COL_COUNT) return;
    c.r = pal[col].r;
    c.g = pal[col].g;
    c.b = pal[col].b;
    c.a = 255;
    v[0].position.x = (float)x0;
    v[0].position.y = (float)y0;
    v[1].position.x = (float)x1;
    v[1].position.y = (float)y1;
    v[2].position.x = (float)x2;
    v[2].position.y = (float)y2;
    for (i = 0; i < 3; i++) {
        v[i].color = c;
        v[i].tex_coord.x = 0;
        v[i].tex_coord.y = 0;
    }
    SDL_RenderGeometry(ren, NULL, v, 3, NULL, 0);
}

static void h_text(int x, int y, const char *s, int col, int scale) {
    SDL_Color c;
    SDL_Surface *surf;
    SDL_Texture *tex;
    SDL_Rect dst;
    if (!s || !s[0]) return;
    if (scale < 1) scale = 1;
    if (scale > 3) scale = 3;
    if (!fonts[scale]) return;
    c.r = pal[col].r;
    c.g = pal[col].g;
    c.b = pal[col].b;
    c.a = 255;
    surf = TTF_RenderText_Blended(fonts[scale], s, c);
    if (!surf) return;
    tex = SDL_CreateTextureFromSurface(ren, surf);
    dst.x = x;
    dst.y = y;
    dst.w = surf->w;
    dst.h = surf->h;
    SDL_RenderCopy(ren, tex, NULL, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

static const Draw draw = { h_rect, h_tri, h_text };

static char save_path[512];

static void save_path_init(void) {
    const char *home = getenv("HOME");
    if (!home) home = "/tmp";
    snprintf(save_path, sizeof save_path, "%s/.spire-save", home);
}

bool platform_load(Save *out) {
    FILE *f = fopen(save_path, "rb");
    size_t n;
    memset(out, 0, sizeof *out);
    if (!f) return false;
    n = fread(out, sizeof *out, 1, f);
    fclose(f);
    return n == 1;
}

void platform_store(const Save *in) {
    FILE *f = fopen(save_path, "wb");
    if (!f) return;
    fwrite(in, sizeof *in, 1, f);
    fclose(f);
}

static Input read_keys(const Uint8 *ks) {
    Input in;
    memset(&in, 0, sizeof in);
    in.left = ks[SDL_SCANCODE_LEFT] != 0;
    in.right = ks[SDL_SCANCODE_RIGHT] != 0;
    in.up = ks[SDL_SCANCODE_UP] != 0;
    in.down = ks[SDL_SCANCODE_DOWN] != 0;
    in.jump = ks[SDL_SCANCODE_Z] || ks[SDL_SCANCODE_SPACE];
    in.dash = ks[SDL_SCANCODE_X] || ks[SDL_SCANCODE_LSHIFT];
    in.restart = ks[SDL_SCANCODE_R] || ks[SDL_SCANCODE_BACKSPACE];
    in.quit = ks[SDL_SCANCODE_ESCAPE];
    in.confirm = ks[SDL_SCANCODE_RETURN] || ks[SDL_SCANCODE_KP_ENTER];
    return in;
}

static void shot(int index) {
    char path[64];
    SDL_Surface *surf;
    int w, h;
    SDL_GetRendererOutputSize(ren, &w, &h);
    surf = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (!surf) return;
    SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, surf->pixels, surf->pitch);
    snprintf(path, sizeof path, "/tmp/spire-%02d.bmp", index);
    SDL_SaveBMP(surf, path);
    SDL_FreeSurface(surf);
    printf("wrote %s\n", path);
}

int main(int argc, char **argv) {
    int demo = 0;
    int shots = 0;
    int i;
    int running = 1;
    int shot_i = 0;
    int prev_level = -1;
    int title_frames = 0;
    int win_frames = 0;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--demo")) demo = 1;
        if (!strcmp(argv[i], "--shot")) shots = 1;
    }

    save_path_init();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }
    if (TTF_Init() != 0) {
        fprintf(stderr, "%s\n", TTF_GetError());
        return 1;
    }
    fonts[1] = TTF_OpenFont(font_path, 10);
    fonts[2] = TTF_OpenFont(font_path, 22);
    fonts[3] = TTF_OpenFont(font_path, 28);
    if (!fonts[1] || !fonts[2]) {
        fprintf(stderr, "%s\n", TTF_GetError());
        return 1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    win = SDL_CreateWindow("SPIRE  —  arrows move   Z jump   X dash",
                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                           960, 720, SDL_WINDOW_SHOWN);
    ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    SDL_RenderSetLogicalSize(ren, SCREEN_W, SCREEN_H);
    SDL_RenderSetIntegerScale(ren, SDL_TRUE);

    game_init();
    if (demo) bot_reset();

    while (running && !game_quit_requested()) {
        uint32_t frame_start = SDL_GetTicks();
        SDL_Event e;
        Input in;
        const Uint8 *ks;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = 0;
            if (e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_F1 && !demo)
                game_force_next();
        }
        ks = SDL_GetKeyboardState(NULL);
        if (demo) {
            memset(&in, 0, sizeof in);
            if (game_mode() == MODE_TITLE) {
                title_frames++;
                if (title_frames == 25) in.confirm = true;
            } else if (game_mode() == MODE_WIN) {
                win_frames++;
                if (win_frames > 30) break;
            } else {
                bot_input(&in);
            }
        } else {
            in = read_keys(ks);
        }
        game_step(&in);
        ensure_palette();
        game_render(&draw);
        SDL_RenderPresent(ren);

        if (shots) {
            int lv = game_mode() == MODE_PLAY ? game_level() : -1;
            if (game_mode() == MODE_TITLE && shot_i == 0) {
                shot(shot_i++);
            }
            if (lv != prev_level && game_mode() == MODE_PLAY) {
                prev_level = lv;
                shot(shot_i++);
            }
            if (game_mode() == MODE_WIN && win_frames == 8) shot(shot_i++);
        }

        {
            uint32_t elapsed = SDL_GetTicks() - frame_start;
            if (elapsed < 16) {
                SDL_Delay(16 - elapsed);
                while (SDL_GetTicks() - frame_start < 16) {
                }
            }
        }
        if (demo && shot_i > 40) break;
    }

    if (ren) SDL_DestroyRenderer(ren);
    if (win) SDL_DestroyWindow(win);
    for (i = 0; i < 4; i++) if (fonts[i]) TTF_CloseFont(fonts[i]);
    TTF_Quit();
    SDL_Quit();
    return 0;
}
