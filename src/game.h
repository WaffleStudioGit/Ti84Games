#ifndef SPIRE_GAME_H
#define SPIRE_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define SCREEN_W 320
#define SCREEN_H 240
#define TILE     16
#define COLS     20
#define ROWS     15
#define FLOORS   10
#define PW       8
#define PH       12

/* Palette indices shared by the calculator and the desktop build.
   On the CE, int is 24-bit; every gameplay value fits in 16 bits. */
enum {
    COL_SKY0, COL_SKY1, COL_SKY2, COL_SKY3,
    COL_STONE, COL_STONE_LT, COL_STONE_DK,
    COL_SPIKE, COL_SPIKE_DK,
    COL_CLOAK, COL_CLOAK_LT, COL_CLOAK_DK, COL_SKIN,
    COL_GOLD, COL_GOLD_HOT,
    COL_SPRING, COL_SPRING_DK,
    COL_ONEWAY, COL_ONEWAY_LT,
    COL_DUST, COL_TEXT, COL_TEXT_DIM, COL_BLACK, COL_SCARF, COL_EMBER,
    COL_COUNT
};

typedef struct { uint8_t r, g, b; } Rgb;

typedef struct {
    bool left, right, up, down;
    bool jump, dash, restart, quit, confirm;
} Input;

typedef struct {
    void (*rect)(int x, int y, int w, int h, int col);
    void (*tri)(int x0, int y0, int x1, int y1, int x2, int y2, int col);
    void (*text)(int x, int y, const char *s, int col, int scale);
} Draw;

enum {
    MODE_TITLE = 0,
    MODE_PLAY  = 1,
    MODE_WIN   = 2
};

enum {
    PHASE_PLAY  = 0,
    PHASE_DIE   = 1,
    PHASE_CLEAR = 2
};

typedef struct {
    uint32_t magic;
    uint32_t frames;
    uint16_t falls;
    uint16_t beaten;
} Save;

typedef struct {
    int x, y, vx, vy;
    int level;
    int mode;
    int phase;
    bool grounded;
    bool can_dash;
    bool wall_l, wall_r;
    int goal_x, goal_y;
} Snap;

#define SAVE_MAGIC 0x53504952u

void palette_for(int level, Rgb out[COL_COUNT]);

void game_init(void);
void game_step(const Input *in);
void game_render(const Draw *d);
void game_snap(Snap *s);
char game_tile(int c, int r);
int  game_map_error(void);
int  game_visual_level(void);
int  game_mode(void);
int  game_level(void);
uint32_t game_frames(void);
uint16_t game_falls(void);
bool game_quit_requested(void);
void game_force_next(void);

/* Provided by the platform (calculator appvar, or a file on the desktop). */
bool platform_load(Save *out);
void platform_store(const Save *in);

#endif
