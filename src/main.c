#include <fileioc.h>
#include <graphx.h>
#include <keypadc.h>
#include <time.h>

#include "game.h"

bool platform_load(Save *out) {
    uint8_t h = ti_Open("SPIRESV", "r");
    size_t n;
    if (!h) return false;
    n = ti_Read(out, sizeof *out, 1, h);
    ti_Close(h);
    return n == 1;
}

void platform_store(const Save *in) {
    uint8_t h = ti_Open("SPIRESV", "w");
    if (!h) return;
    ti_Write(in, sizeof *in, 1, h);
    ti_SetArchiveStatus(true, h);
    ti_Close(h);
}

static int pal_level = -2;
static uint16_t palette[256];

static void ensure_palette(void) {
    int lv = game_visual_level();
    Rgb rgb[COL_COUNT];
    int i;
    if (lv == pal_level) return;
    pal_level = lv;
    palette_for(lv, rgb);
    for (i = 0; i < 256; i++) palette[i] = 0;
    for (i = 0; i < COL_COUNT; i++)
        palette[i] = gfx_RGBTo1555(rgb[i].r, rgb[i].g, rgb[i].b);
    gfx_SetPalette(palette, sizeof palette, 0);
}

static void ce_rect(int x, int y, int w, int h, int col) {
    if (w <= 0 || h <= 0) return;
    gfx_SetColor((uint8_t)col);
    gfx_FillRectangle(x, y, w, h);
}

static void ce_tri(int x0, int y0, int x1, int y1, int x2, int y2, int col) {
    gfx_SetColor((uint8_t)col);
    gfx_FillTriangle(x0, y0, x1, y1, x2, y2);
}

static void ce_text(int x, int y, const char *s, int col, int scale) {
    if (scale < 1) scale = 1;
    gfx_SetTextScale((uint8_t)scale, (uint8_t)scale);
    gfx_SetTextFGColor((uint8_t)col);
    gfx_PrintStringXY(s, x, y);
}

static const Draw draw = {
    ce_rect,
    ce_tri,
    ce_text
};

static Input read_input(void) {
    Input in;
    in.left = kb_IsDown(kb_KeyLeft);
    in.right = kb_IsDown(kb_KeyRight);
    in.up = kb_IsDown(kb_KeyUp);
    in.down = kb_IsDown(kb_KeyDown);
    in.jump = kb_IsDown(kb_Key2nd);
    in.dash = kb_IsDown(kb_KeyAlpha);
    in.restart = kb_IsDown(kb_KeyDel);
    in.quit = kb_IsDown(kb_KeyClear);
    in.confirm = kb_IsDown(kb_KeyEnter);
    return in;
}

int main(void) {
    const clock_t slice = CLOCKS_PER_SEC / 60;
    kb_DisableOnLatch();
    gfx_Begin();
    gfx_SetDrawBuffer();
    gfx_SetTextTransparentColor(0);
    gfx_SetTextBGColor(0);
    gfx_SetTextConfig(gfx_text_clip);
    game_init();

    while (!game_quit_requested()) {
        Input in;
        clock_t start = clock();
        kb_Scan();
        if (kb_On) break;
        in = read_input();
        game_step(&in);
        ensure_palette();
        game_render(&draw);
        gfx_SwapDraw();
        while ((clock() - start) < slice) {
        }
    }

    gfx_End();
    return 0;
}
