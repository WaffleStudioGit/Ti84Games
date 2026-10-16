#include "game.h"

#include <string.h>

/* Fixed point: 256 == 1 pixel. Tuned so a full jump clears a 3-tile rise,
   a running jump clears a 4-tile pit, and a 6-tile pit needs a dash. */
#define FP          256
#define GRAV          58
#define MAX_FALL    1280
#define JUMP_V      -1408
#define MAX_RUN      448
#define RUN_ACC       96
#define AIR_ACC       56
#define FRICTION     110
#define AIR_DRAG       6
#define WALL_SLIDE   294
#define WJ_VX        614
#define WJ_VY       -1344
#define DASH_SP     1280
#define DASH_T         8
#define DASH_GRAV     16
#define SPRING_V    -1920
#define COYOTE         6
#define BUFFER         6
#define LOCKOUT        6

static const char *const floors[FLOORS][ROWS] = {
    { /* 0 short hops */
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        ".P...............*..",
        "###..####..###..####",
        "###..####..###..####",
        "###..####..###..####",
    },
    { /* 1 hold the jump */
        "....................",
        "................*...",
        "...............#####",
        "...............#####",
        "............########",
        "............########",
        ".........###########",
        ".........###########",
        "......##############",
        "......##############",
        "...#################",
        ".P.#################",
        "####################",
        "####################",
        "####################",
    },
    { /* 2 run at the pit */
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        ".P.............*....",
        "########.^^.########",
        "########.^^.########",
        "########.^^.########",
    },
    { /* 3 scaffolds */
        "....................",
        "....................",
        "........*...........",
        ".......---..........",
        "....................",
        "....................",
        ".......---..........",
        "....................",
        "....................",
        ".......---..........",
        "....................",
        "P...................",
        "####################",
        "####################",
        "####################",
    },
    { /* 4 spring */
        "....................",
        "....................",
        "....................",
        "....................",
        ".........*..........",
        "......######........",
        "......######........",
        "....................",
        "....................",
        "....................",
        ".P..................",
        "###S................",
        "####^^^^^^^^^^^^^^..",
        "####^^^^^^^^^^^^^^..",
        "####^^^^^^^^^^^^^^..",
    },
    { /* 5 the shaft */
        "..........*.........",
        "........#...#.......",
        "........#...#.......",
        "........#...#.......",
        "........#...#.......",
        "........#...#.......",
        "........#...#.......",
        "........#...#.......",
        "........#...#.......",
        "........#...#.......",
        "........#...#.......",
        "........#.P.#.......",
        "####################",
        "####################",
        "####################",
    },
    { /* 6 dash the gap */
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        ".P.............*....",
        "#######^^^^^^#######",
        "#######^^^^^^#######",
        "#######^^^^^^#######",
    },
    { /* 7 dash upward */
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "....................",
        "...............*....",
        "..............####..",
        "....................",
        "....................",
        ".P..................",
        "####################",
        "####################",
        "####################",
    },
    { /* 8 the well */
        "....................",
        "....................",
        "....................",
        "..........*.........",
        "........#...#.......",
        "........#...#.......",
        "........#...#.......",
        "........#...#.......",
        "........#...#.......",
        ".P......#...#.......",
        "#########...########",
        "........#...#.......",
        "........#^^^#.......",
        "........#^^^#.......",
        "........#^^^#.......",
    },
    { /* 9 the top */
        "....................",
        "....................",
        "....................",
        "................*...",
        "....................",
        "....................",
        "................S...",
        "............########",
        "............########",
        "............########",
        "............########",
        "............########",
        ".P..........########",
        "#####^^^^###########",
        "#####^^^^###########",
    },
};

#define MAXP 40

typedef struct {
    int x, y, vx, vy, life, col;
} Part;

static int level;
static int mode;
static int phase;
static int x, y, vx, vy, xsub, ysub;
static int facing;
static int coyote, buffer, lockout, cut;
static int dash_t, dash_vx, dash_vy;
static int can_dash;
static int grounded;
static int wall_l, wall_r;
static int ignore_oneway;
static int spawn_x, spawn_y;
static int die_t, clear_t, intro_t, shake_t, squash_t;
static int falls;
static uint32_t frames;
static uint32_t anim;
static int quit;
static Save best;
static Part parts[MAXP];
static int part_i;
static uint32_t rng = 1u;
static int prev_jump, prev_dash, prev_restart, prev_quit, prev_confirm;

static int lcg(void) {
    rng = rng * 1664525u + 1013904223u;
    return (int)((rng >> 16) & 32767u);
}

static int iabs(int v) { return v < 0 ? -v : v; }

static int approach(int v, int target, int amount) {
    if (v < target) {
        v += amount;
        if (v > target) v = target;
    } else if (v > target) {
        v -= amount;
        if (v < target) v = target;
    }
    return v;
}

static int div_floor(int a, int b) {
    int q = a / b;
    int r = a % b;
    if (r < 0) q -= 1;
    return q;
}

static int consume(int *sub, int v) {
    int acc = *sub + v;
    int pixels = acc / FP;
    int rem = acc % FP;
    if (rem < 0) {
        rem += FP;
        pixels -= 1;
    }
    *sub = rem;
    return pixels;
}

static int lerp(int a, int b, int num, int den) {
    return a + (b - a) * num / den;
}

static void put_rgb(Rgb *c, int r, int g, int b) {
    c->r = (uint8_t)r;
    c->g = (uint8_t)g;
    c->b = (uint8_t)b;
}

void palette_for(int lv, Rgb out[COL_COUNT]) {
    int den = FLOORS - 1;
    if (lv < 0) lv = 0;
    if (lv > den) lv = den;
    put_rgb(&out[COL_SKY0], lerp(8, 78, lv, den), lerp(10, 118, lv, den), lerp(20, 168, lv, den));
    put_rgb(&out[COL_SKY1], lerp(14, 108, lv, den), lerp(18, 148, lv, den), lerp(32, 186, lv, den));
    put_rgb(&out[COL_SKY2], lerp(22, 150, lv, den), lerp(28, 170, lv, den), lerp(48, 196, lv, den));
    put_rgb(&out[COL_SKY3], lerp(34, 186, lv, den), lerp(42, 186, lv, den), lerp(68, 176, lv, den));
    if (lv >= den) {
        put_rgb(&out[COL_SKY2], 168, 156, 132);
        put_rgb(&out[COL_SKY3], 214, 176, 120);
    }
    put_rgb(&out[COL_STONE], 62, 60, 74);
    put_rgb(&out[COL_STONE_LT], 118, 116, 132);
    put_rgb(&out[COL_STONE_DK], 28, 26, 36);
    put_rgb(&out[COL_SPIKE], 204, 64, 72);
    put_rgb(&out[COL_SPIKE_DK], 120, 32, 48);
    put_rgb(&out[COL_CLOAK], 224, 140, 58);
    put_rgb(&out[COL_CLOAK_LT], 255, 196, 120);
    put_rgb(&out[COL_CLOAK_DK], 148, 78, 32);
    put_rgb(&out[COL_SKIN], 240, 208, 176);
    put_rgb(&out[COL_GOLD], 255, 196, 72);
    put_rgb(&out[COL_GOLD_HOT], 255, 244, 210);
    put_rgb(&out[COL_SPRING], 64, 196, 116);
    put_rgb(&out[COL_SPRING_DK], 24, 96, 60);
    put_rgb(&out[COL_ONEWAY], 176, 140, 78);
    put_rgb(&out[COL_ONEWAY_LT], 228, 196, 130);
    put_rgb(&out[COL_DUST], 176, 156, 128);
    put_rgb(&out[COL_TEXT], 244, 240, 232);
    put_rgb(&out[COL_TEXT_DIM], 156, 164, 176);
    put_rgb(&out[COL_BLACK], 8, 8, 12);
    put_rgb(&out[COL_SCARF], 196, 64, 52);
    put_rgb(&out[COL_EMBER], 255, 112, 40);
}

char game_tile(int c, int r) {
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return 0;
    if (mode != MODE_PLAY && mode != MODE_WIN) {
        /* Title still wants the current floor's art only while playing. */
    }
    if (level < 0 || level >= FLOORS) return 0;
    return floors[level][r][c];
}

static char raw(int c, int r) {
    if (c < 0 || c >= COLS || r < 0 || r >= ROWS) return 0;
    return floors[level][r][c];
}

int game_map_error(void) {
    int f, r, c, ps, gs;
    for (f = 0; f < FLOORS; f++) {
        ps = 0;
        gs = 0;
        for (r = 0; r < ROWS; r++) {
            const char *row = floors[f][r];
            if (!row || (int)strlen(row) != COLS) return f * 100 + r;
            for (c = 0; c < COLS; c++) {
                char t = row[c];
                if (t != '.' && t != '#' && t != '^' && t != '-' &&
                    t != 'S' && t != '*' && t != 'P') return f * 100 + r;
                if (t == 'P') ps++;
                if (t == '*') gs++;
            }
        }
        if (ps != 1) return 10000 + f;
        if (gs < 1) return 20000 + f;
    }
    return 0;
}

static int solid_box(int px, int py) {
    int x0, x1, y0, y1, r, c;
    if (py > SCREEN_H + TILE * 2) return 0;
    x0 = div_floor(px, TILE);
    x1 = div_floor(px + PW - 1, TILE);
    y0 = div_floor(py, TILE);
    y1 = div_floor(py + PH - 1, TILE);
    for (r = y0; r <= y1; r++) {
        for (c = x0; c <= x1; c++) {
            char t;
            if (c < 0 || c >= COLS) return 1;
            if (r < 0 || r >= ROWS) continue;
            t = raw(c, r);
            if (t == '#' || t == 'S') return 1;
        }
    }
    return 0;
}

static int oneway_hit(int px, int py, int prev_feet) {
    int x0, x1, y0, y1, r, c;
    if (ignore_oneway) return 0;
    x0 = div_floor(px, TILE);
    x1 = div_floor(px + PW - 1, TILE);
    y0 = div_floor(py, TILE);
    y1 = div_floor(py + PH - 1, TILE);
    for (r = y0; r <= y1; r++) {
        if (r < 0 || r >= ROWS) continue;
        for (c = x0; c <= x1; c++) {
            int top;
            if (c < 0 || c >= COLS) continue;
            if (raw(c, r) != '-') continue;
            top = r * TILE;
            if (prev_feet <= top && py + PH > top) return 1;
        }
    }
    return 0;
}

static char surface_under(int px, int py) {
    int probe_y = py + 1;
    int x0 = div_floor(px, TILE);
    int x1 = div_floor(px + PW - 1, TILE);
    int r0 = div_floor(probe_y + PH - 1, TILE);
    int c;
    char found = 0;
    for (c = x0; c <= x1; c++) {
        char t;
        if (c < 0 || c >= COLS || r0 < 0 || r0 >= ROWS) continue;
        t = raw(c, r0);
        if (t == 'S') return 'S';
        if (t == '#') found = '#';
        else if (t == '-' && found != '#') found = '-';
    }
    return found;
}

static void puff(int px, int py, int col, int n, int upward) {
    int i;
    for (i = 0; i < n; i++) {
        Part *p = &parts[part_i];
        part_i = (part_i + 1) % MAXP;
        p->x = px * FP;
        p->y = py * FP;
        p->vx = (lcg() % 500) - 250;
        p->vy = -upward - (lcg() % 280);
        p->life = 8 + (lcg() % 14);
        p->col = col;
    }
}

static void update_parts(void) {
    int i;
    for (i = 0; i < MAXP; i++) {
        if (parts[i].life <= 0) continue;
        parts[i].vy += 28;
        parts[i].x += parts[i].vx;
        parts[i].y += parts[i].vy;
        parts[i].life--;
    }
}

static void sense_walls(void) {
    wall_l = solid_box(x - 1, y) && !solid_box(x, y);
    wall_r = solid_box(x + 1, y) && !solid_box(x, y);
}

static void respawn(void) {
    x = spawn_x;
    y = spawn_y;
    vx = vy = xsub = ysub = 0;
    facing = 1;
    grounded = 0;
    can_dash = 1;
    dash_t = 0;
    coyote = 0;
    buffer = 0;
    lockout = 0;
    cut = 0;
    ignore_oneway = 0;
    phase = PHASE_PLAY;
    if (solid_box(x, y)) {
        int i;
        for (i = 0; i < 24 && solid_box(x, y); i++) y--;
    }
}

static void load_level(int n, int show_intro) {
    int r, c;
    level = n;
    spawn_x = TILE;
    spawn_y = TILE;
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            if (floors[level][r][c] == 'P') {
                spawn_x = c * TILE + (TILE - PW) / 2;
                spawn_y = r * TILE + (TILE - PH);
            }
        }
    }
    respawn();
    intro_t = show_intro ? 48 : 0;
}

static void start_run(void) {
    falls = 0;
    frames = 0;
    mode = MODE_PLAY;
    load_level(0, 1);
}

static int better_run(void) {
    if (!best.beaten) return 1;
    if (falls < best.falls) return 1;
    if (falls == best.falls && frames < best.frames) return 1;
    return 0;
}

static void finish_run(void) {
    mode = MODE_WIN;
    if (better_run()) {
        best.magic = SAVE_MAGIC;
        best.frames = frames;
        best.falls = (uint16_t)falls;
        best.beaten = 1;
        platform_store(&best);
    }
}

static void advance(void) {
    if (level + 1 >= FLOORS) finish_run();
    else load_level(level + 1, 1);
}

static void kill_player(void) {
    if (phase != PHASE_PLAY) return;
    phase = PHASE_DIE;
    die_t = 22;
    shake_t = 8;
    falls++;
    puff(x + PW / 2, y + PH / 2, COL_EMBER, 10, 220);
    dash_t = 0;
}

static void do_jump(void) {
    if (!grounded && (wall_l || wall_r)) {
        int dir = (wall_l && !wall_r) ? 1 : (wall_r && !wall_l) ? -1 : facing;
        vx = dir * WJ_VX;
        vy = WJ_VY;
        facing = dir > 0 ? 1 : -1;
        lockout = LOCKOUT;
        can_dash = 1;
    } else {
        vy = JUMP_V;
    }
    grounded = 0;
    coyote = 0;
    buffer = 0;
    cut = 0;
    dash_t = 0;
    squash_t = 0;
}

static void begin_dash(const Input *in) {
    int dx = 0;
    int dy = 0;
    int sp;
    if (in->right && !in->left) dx = 1;
    if (in->left && !in->right) dx = -1;
    if (in->down && !in->up) dy = 1;
    if (in->up && !in->down) dy = -1;
    if (!dx && !dy) dx = facing;
    /* A sideways dash keeps the jump. Up and down replace vertical speed. */
    if (dx && !dy) {
        dash_vx = dx * DASH_SP;
        dash_vy = vy;
    } else if (dx && dy) {
        sp = DASH_SP * 5 / 7;
        dash_vx = dx * sp;
        dash_vy = dy * sp;
    } else {
        dash_vx = 0;
        dash_vy = dy * DASH_SP;
    }
    dash_t = DASH_T;
    can_dash = 0;
    grounded = 0;
    if (dx) facing = dx > 0 ? 1 : -1;
    puff(x + PW / 2, y + PH / 2, COL_EMBER, 6, 80);
}

static int move_x(int dist) {
    int step = dist > 0 ? 1 : -1;
    int n = iabs(dist);
    int i;
    for (i = 0; i < n; i++) {
        if (!solid_box(x + step, y)) {
            x += step;
            continue;
        }
        if (vy >= 0) {
            int s;
            int stepped = 0;
            for (s = 1; s <= 4; s++) {
                if (!solid_box(x + step, y - s) && !solid_box(x, y - s)) {
                    x += step;
                    y -= s;
                    stepped = 1;
                    break;
                }
            }
            if (stepped) continue;
        }
        vx = 0;
        xsub = 0;
        return 1;
    }
    return 0;
}

static int move_y(int dist) {
    int step = dist > 0 ? 1 : -1;
    int n = iabs(dist);
    int i;
    for (i = 0; i < n; i++) {
        int prev_feet = y + PH;
        if (step < 0) {
            if (solid_box(x, y - 1)) {
                int s, nudged = 0;
                for (s = 1; s <= 4 && !nudged; s++) {
                    if (!solid_box(x + s, y - 1) && !solid_box(x + s, y)) {
                        x += s;
                        nudged = 1;
                    } else if (!solid_box(x - s, y - 1) && !solid_box(x - s, y)) {
                        x -= s;
                        nudged = 1;
                    }
                }
                if (!nudged) {
                    vy = 0;
                    ysub = 0;
                    return 1;
                }
            }
            y -= 1;
            continue;
        }
        if (solid_box(x, y + 1) || oneway_hit(x, y + 1, prev_feet)) {
            vy = 0;
            ysub = 0;
            return 1;
        }
        y += 1;
    }
    return 0;
}

static int spike_hit(void) {
    int px = x + 1;
    int py = y + 1;
    int pw = PW - 2;
    int ph = PH - 2;
    int x0, x1, y0, y1, r, c;
    if (pw <= 0 || ph <= 0) return 0;
    x0 = div_floor(px, TILE);
    x1 = div_floor(px + pw - 1, TILE);
    y0 = div_floor(py, TILE);
    y1 = div_floor(py + ph - 1, TILE);
    for (r = y0; r <= y1; r++) {
        for (c = x0; c <= x1; c++) {
            if (c < 0 || c >= COLS || r < 0 || r >= ROWS) continue;
            if (raw(c, r) == '^') return 1;
        }
    }
    return 0;
}

static int goal_hit(void) {
    int x0 = div_floor(x, TILE);
    int x1 = div_floor(x + PW - 1, TILE);
    int y0 = div_floor(y, TILE);
    int y1 = div_floor(y + PH - 1, TILE);
    int r, c;
    for (r = y0; r <= y1; r++) {
        for (c = x0; c <= x1; c++) {
            if (c < 0 || c >= COLS || r < 0 || r >= ROWS) continue;
            if (raw(c, r) == '*') return 1;
        }
    }
    return 0;
}

static void fmt_uint(char *out, unsigned v) {
    char rev[8];
    int n = 0;
    int i = 0;
    if (v == 0) {
        out[0] = '0';
        out[1] = 0;
        return;
    }
    while (v && n < 8) {
        rev[n++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (n) out[i++] = rev[--n];
    out[i] = 0;
}

static void fmt_time(char *out, uint32_t fr) {
    unsigned sec = (unsigned)(fr / 60u);
    unsigned m = sec / 60u;
    unsigned s = sec % 60u;
    char ms[8];
    int i = 0;
    fmt_uint(ms, m);
    while (ms[i]) {
        out[i] = ms[i];
        i++;
    }
    out[i++] = ':';
    out[i++] = (char)('0' + (s / 10));
    out[i++] = (char)('0' + (s % 10));
    out[i] = 0;
}

void game_init(void) {
    Save tmp;
    memset(parts, 0, sizeof parts);
    memset(&best, 0, sizeof best);
    if (platform_load(&tmp) && tmp.magic == SAVE_MAGIC) best = tmp;
    mode = MODE_TITLE;
    level = 0;
    phase = PHASE_PLAY;
    quit = 0;
    anim = 0;
    falls = 0;
    frames = 0;
    prev_jump = prev_dash = prev_restart = prev_quit = prev_confirm = 0;
    part_i = 0;
}

void game_force_next(void) {
    if (mode != MODE_PLAY || phase != PHASE_PLAY) return;
    phase = PHASE_CLEAR;
    clear_t = 1;
}

void game_step(const Input *in) {
    int jump_edge, dash_edge, restart_edge, quit_edge, confirm_edge;
    int pixels, blocked;
    int prev_vy;
    char surf;
    int dropped;

    anim++;
    jump_edge = in->jump && !prev_jump;
    dash_edge = in->dash && !prev_dash;
    restart_edge = in->restart && !prev_restart;
    quit_edge = in->quit && !prev_quit;
    confirm_edge = in->confirm && !prev_confirm;
    prev_jump = in->jump;
    prev_dash = in->dash;
    prev_restart = in->restart;
    prev_quit = in->quit;
    prev_confirm = in->confirm;

    update_parts();
    if (shake_t > 0) shake_t--;

    if (quit_edge) {
        if (mode == MODE_TITLE) quit = 1;
        else mode = MODE_TITLE;
        return;
    }

    if (mode == MODE_TITLE) {
        if (confirm_edge) start_run();
        return;
    }
    if (mode == MODE_WIN) {
        if (confirm_edge) start_run();
        return;
    }

    if (restart_edge) {
        load_level(level, 0);
        return;
    }

    if (phase != PHASE_PLAY) {
        if (phase != PHASE_CLEAR) frames++;
        if (phase == PHASE_DIE) {
            if (--die_t <= 0) respawn();
        } else if (--clear_t <= 0) {
            advance();
        }
        return;
    }

    frames++;
    if (intro_t > 0) intro_t--;
    if (squash_t > 0) squash_t--;
    if (ignore_oneway > 0) ignore_oneway--;

    sense_walls();
    if (in->left && !in->right) facing = -1;
    if (in->right && !in->left) facing = 1;

    if (jump_edge) buffer = BUFFER;
    else if (buffer > 0) buffer--;

    dropped = 0;
    surf = surface_under(x, y);
    if (in->down && jump_edge && grounded && surf == '-') {
        ignore_oneway = 18;
        y += 2;
        vy = 90;
        grounded = 0;
        coyote = 0;
        buffer = 0;
        dropped = 1;
    }

    if (dash_edge && can_dash && dash_t == 0) begin_dash(in);

    if (dash_t > 0) {
        int cancel = 0;
        if (!dropped && buffer && (wall_l || wall_r || grounded || coyote)) {
            do_jump();
            cancel = 1;
        }
        if (!cancel) {
            vx = dash_vx;
            vy = dash_vy;
            dash_vy += DASH_GRAV;
            dash_t--;
            if (dash_t == 0) {
                if (vx > MAX_RUN) vx = MAX_RUN;
                if (vx < -MAX_RUN) vx = -MAX_RUN;
            }
        }
    } else {
        int target = 0;
        int acc;
        if (lockout > 0) {
            lockout--;
            vx = approach(vx, 0, AIR_DRAG);
        } else {
            if (in->right && !in->left) target = MAX_RUN;
            if (in->left && !in->right) target = -MAX_RUN;
            acc = grounded ? RUN_ACC : AIR_ACC;
            if (target == 0) acc = grounded ? FRICTION : AIR_DRAG;
            vx = approach(vx, target, acc);
        }
        if (!dropped && buffer && (grounded || coyote || wall_l || wall_r)) do_jump();
        if (!in->jump && vy < 0 && !cut) {
            vy = vy * 2 / 5;
            cut = 1;
        }
        if (!grounded && ((wall_l && in->left) || (wall_r && in->right))) {
            if (vy > WALL_SLIDE) vy = WALL_SLIDE;
            can_dash = 1;
        }
        if (!grounded) {
            vy += GRAV;
            if (vy > MAX_FALL) vy = MAX_FALL;
        }
    }

    if (grounded) coyote = COYOTE;
    else if (coyote > 0) coyote--;

    prev_vy = vy;
    pixels = consume(&xsub, vx);
    blocked = move_x(pixels);
    if (dash_t > 0 && blocked) dash_t = 0;

    pixels = consume(&ysub, vy);
    blocked = move_y(pixels);
    if (dash_t > 0 && blocked) dash_t = 0;

    if (y < -48) {
        y = -48;
        if (vy < 0) vy = 0;
    }

    sense_walls();
    if ((wall_l || wall_r) && dash_t == 0) can_dash = 1;

    surf = 0;
    if (vy >= 0 && (solid_box(x, y + 1) || oneway_hit(x, y + 1, y + PH))) {
        surf = surface_under(x, y);
        if (!surf) surf = '#';
    }
    if (surf == 'S') {
        vy = SPRING_V;
        ysub = 0;
        grounded = 0;
        can_dash = 1;
        coyote = 0;
        cut = 1;
        puff(x + PW / 2, y + PH, COL_SPRING, 8, 260);
    } else if (surf) {
        if (!grounded && prev_vy > 280) {
            squash_t = 5;
            puff(x + PW / 2, y + PH, COL_DUST, 4, 60);
        }
        grounded = 1;
        can_dash = 1;
        vy = 0;
        ysub = 0;
        cut = 0;
    } else {
        grounded = 0;
    }

    if (grounded && iabs(vx) > 220 && (anim % 5u) == 0)
        puff(x + PW / 2, y + PH - 1, COL_DUST, 1, 30);

    if (spike_hit() || y > SCREEN_H) {
        kill_player();
        return;
    }
    if (goal_hit()) {
        phase = PHASE_CLEAR;
        clear_t = 26;
        puff(x + PW / 2, y, COL_GOLD, 12, 300);
    }
}

static void draw_text(const Draw *d, int tx, int ty, const char *s, int col, int scale) {
    d->text(tx + 1, ty + 1, s, COL_BLACK, scale);
    d->text(tx, ty, s, col, scale);
}

static void sky(const Draw *d) {
    d->rect(0, 0, SCREEN_W, 60, COL_SKY0);
    d->rect(0, 60, SCREEN_W, 60, COL_SKY1);
    d->rect(0, 120, SCREEN_W, 60, COL_SKY2);
    d->rect(0, 180, SCREEN_W, 60, COL_SKY3);
}

static void specks(const Draw *d) {
    int i;
    int n = 8 + level;
    if (n > 16) n = 16;
    for (i = 0; i < n; i++) {
        int sy = (int)((anim * (1u + (unsigned)(i % 3)) + (unsigned)i * 37u) % (unsigned)SCREEN_H);
        int sx = (i * 47 + 13) % SCREEN_W;
        int col = (i % 4 == 0) ? COL_EMBER : COL_TEXT_DIM;
        sy = SCREEN_H - 1 - sy;
        d->rect(sx, sy, 1, 1, col);
    }
}

static void draw_tile(const Draw *d, int c, int r, int ox, int oy) {
    char t = raw(c, r);
    int tx = c * TILE + ox;
    int ty = r * TILE + oy;
    int edge_t, edge_b, edge_l, edge_r;
    if (t == '#' ) {
        edge_t = raw(c, r - 1) != '#';
        edge_b = raw(c, r + 1) != '#';
        edge_l = raw(c - 1, r) != '#';
        edge_r = raw(c + 1, r) != '#';
        d->rect(tx, ty, TILE, TILE, COL_STONE);
        if (edge_t) d->rect(tx, ty, TILE, 2, COL_STONE_LT);
        if (edge_b) d->rect(tx, ty + TILE - 2, TILE, 2, COL_STONE_DK);
        if (edge_l) d->rect(tx, ty, 2, TILE, COL_STONE_LT);
        if (edge_r) d->rect(tx + TILE - 2, ty, 2, TILE, COL_STONE_DK);
        if (edge_t || edge_l || ((c + r) & 1))
            d->rect(tx + 7, ty + 7, 2, 2, COL_STONE_DK);
        return;
    }
    if (t == '^') {
        int i;
        for (i = 0; i < 3; i++) {
            int sx = tx + 1 + i * 5;
            d->tri(sx, ty + 14, sx + 4, ty + 14, sx + 2, ty + 3, COL_SPIKE);
            d->tri(sx + 1, ty + 14, sx + 3, ty + 14, sx + 2, ty + 8, COL_SPIKE_DK);
        }
        return;
    }
    if (t == '-') {
        d->rect(tx, ty, TILE, 3, COL_ONEWAY);
        d->rect(tx, ty, TILE, 1, COL_ONEWAY_LT);
        d->rect(tx + 3, ty + 5, 2, 2, COL_ONEWAY);
        d->rect(tx + 8, ty + 5, 2, 2, COL_ONEWAY);
        d->rect(tx + 12, ty + 5, 2, 2, COL_ONEWAY);
        return;
    }
    if (t == 'S') {
        int bob = (int)((anim / 6u) & 1u);
        d->rect(tx, ty + 13, TILE, 3, COL_SPRING_DK);
        d->rect(tx + 2, ty + 10, 12, 2, COL_SPRING);
        d->rect(tx + 4, ty + 7, 8, 2, COL_SPRING);
        d->rect(tx + 5, ty + 4 - bob, 6, 2, COL_SPRING);
        d->rect(tx + 2, ty + 1 - bob, 12, 2, COL_GOLD_HOT);
        return;
    }
    if (t == '*') {
        int bob = (int)((anim / 5u) % 3u);
        int cx = tx + 8;
        int tip = ty + 1 - bob;
        d->tri(cx, tip, tx + 3, ty + 15, tx + 13, ty + 15, COL_GOLD);
        d->tri(cx, tip + 5, tx + 6, ty + 15, tx + 10, ty + 15, COL_GOLD_HOT);
        d->rect(cx - 1, tip + 4, 2, 2, COL_TEXT);
    }
}

static void draw_player(const Draw *d, int px, int py, int ox, int oy, int ghost) {
    int dx = px + ox;
    int dy = py + oy;
    int scarf_x;
    int step;
    int col = ghost ? COL_CLOAK_DK : COL_CLOAK;
    int col_dk = ghost ? COL_STONE : COL_CLOAK_DK;
    int sway;
    if (squash_t > 0 && !ghost) {
        dy += 2;
    }
    sway = (int)((anim / 4u) & 1u);
    scarf_x = facing > 0 ? dx - 3 : dx + PW - 1;
    d->rect(scarf_x, dy + 4 + sway, 4, 2, ghost ? COL_STONE_DK : COL_SCARF);
    d->rect(dx + 1, dy, 6, 5, col_dk);
    if (!ghost) d->rect(dx + 2, dy + 2, 3, 2, COL_SKIN);
    if (!ghost) d->rect(dx + (facing > 0 ? 4 : 2), dy + 2, 1, 1, COL_BLACK);
    d->rect(dx + 1, dy + 5, 6, 4, col);
    if (!ghost) d->rect(dx + 1, dy + 5, 6, 1, COL_CLOAK_LT);
    step = (grounded && iabs(vx) > 80) ? (int)((anim / 4u) & 1u) : 0;
    d->rect(dx + 1, dy + 9, 2, 3 + (squash_t ? 0 : step), col_dk);
    d->rect(dx + 5, dy + 9, 2, squash_t ? 2 : 4 - step, col_dk);
}

static void draw_world(const Draw *d, int ox, int oy) {
    int r, c, i;
    sky(d);
    specks(d);
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) draw_tile(d, c, r, ox, oy);
    }
    if (dash_t > 0) {
        draw_player(d, x - (vx > 0 ? 6 : -2), y, ox, oy, 1);
    }
    if (!(phase == PHASE_DIE && ((die_t / 2) & 1)))
        draw_player(d, x, y, ox, oy, 0);
    for (i = 0; i < MAXP; i++) {
        int px, py;
        if (parts[i].life <= 0) continue;
        px = parts[i].x / FP + ox;
        py = parts[i].y / FP + oy;
        d->rect(px, py, 2, 2, parts[i].col);
    }
}

static void hud(const Draw *d) {
    char num[8];
    char tim[12];
    int bar;
    fmt_uint(num, (unsigned)(level + 1));
    fmt_time(tim, frames);
    if (intro_t > 20)
        draw_text(d, 8, 6, num, COL_TEXT, 2);
    else
        draw_text(d, 8, 6, num, COL_TEXT, 1);
    draw_text(d, 262, 6, tim, COL_TEXT, 1);
    if (falls > 0) {
        fmt_uint(num, (unsigned)falls);
        draw_text(d, 262, 16, num, COL_SCARF, 1);
    }
    bar = ((level + (phase == PHASE_CLEAR ? 1 : 0)) * SCREEN_W) / FLOORS;
    d->rect(0, SCREEN_H - 3, SCREEN_W, 3, COL_STONE_DK);
    if (bar > 0) d->rect(0, SCREEN_H - 3, bar, 3, COL_GOLD);
}

static void render_title(const Draw *d) {
    char line[16];
    sky(d);
    specks(d);
    d->rect(0, 188, SCREEN_W, 52, COL_STONE_DK);
    d->rect(36, 176, 72, 16, COL_STONE);
    d->rect(36, 176, 72, 2, COL_STONE_LT);
    d->rect(132, 184, 28, 8, COL_STONE);
    d->rect(132, 184, 28, 2, COL_STONE_LT);
    d->tri(196, 168, 188, 188, 204, 188, COL_SPIKE);
    d->tri(210, 172, 204, 188, 216, 188, COL_SPIKE);
    d->rect(230, 180, 16, 4, COL_SPRING_DK);
    d->rect(232, 176, 12, 2, COL_SPRING);
    d->rect(234, 172, 8, 2, COL_SPRING);
    {
        int bob = (int)((anim / 5u) % 3u);
        int cx = 292;
        int ty = 150 - bob;
        d->tri(cx, ty, cx - 6, ty + 22, cx + 6, ty + 22, COL_GOLD);
        d->tri(cx, ty + 8, cx - 3, ty + 22, cx + 3, ty + 22, COL_GOLD_HOT);
    }
    facing = 1;
    vx = 0;
    grounded = 1;
    squash_t = 0;
    draw_player(d, 62, 164, 0, 0, 0);

    draw_text(d, 24, 18, "SPIRE", COL_TEXT, 2);
    draw_text(d, 24, 42, "CLIMB", COL_TEXT_DIM, 1);
    draw_text(d, 24, 68, "ARROWS", COL_TEXT_DIM, 1);
    draw_text(d, 100, 68, "MOVE", COL_TEXT, 1);
    draw_text(d, 24, 82, "2ND", COL_TEXT_DIM, 1);
    draw_text(d, 100, 82, "JUMP", COL_TEXT, 1);
    draw_text(d, 24, 96, "ALPHA", COL_TEXT_DIM, 1);
    draw_text(d, 100, 96, "DASH", COL_TEXT, 1);
    draw_text(d, 24, 110, "DEL", COL_TEXT_DIM, 1);
    draw_text(d, 100, 110, "RETRY", COL_TEXT, 1);
    draw_text(d, 24, 124, "CLEAR", COL_TEXT_DIM, 1);
    draw_text(d, 100, 124, "LEAVE", COL_TEXT, 1);
    draw_text(d, 24, 146, "ENTER", COL_GOLD, 1);

    if (best.beaten) {
        draw_text(d, 200, 18, "BEST", COL_TEXT_DIM, 1);
        fmt_time(line, best.frames);
        draw_text(d, 200, 32, line, COL_TEXT, 1);
        fmt_uint(line, best.falls);
        draw_text(d, 200, 46, line, COL_SCARF, 1);
        draw_text(d, 220, 46, "FALLS", COL_TEXT_DIM, 1);
    }
}

static void render_win(const Draw *d) {
    char line[16];
    sky(d);
    specks(d);
    draw_text(d, 24, 28, "TOP", COL_GOLD, 2);
    fmt_time(line, frames);
    draw_text(d, 24, 64, line, COL_TEXT, 2);
    fmt_uint(line, (unsigned)falls);
    draw_text(d, 24, 96, line, COL_SCARF, 1);
    draw_text(d, 48, 96, "FALLS", COL_TEXT_DIM, 1);
    if (best.beaten) {
        draw_text(d, 24, 124, "BEST", COL_TEXT_DIM, 1);
        fmt_time(line, best.frames);
        draw_text(d, 24, 138, line, COL_TEXT, 1);
        fmt_uint(line, best.falls);
        draw_text(d, 80, 138, line, COL_SCARF, 1);
    }
    draw_text(d, 24, 176, "ENTER", COL_GOLD, 1);
    draw_text(d, 88, 176, "AGAIN", COL_TEXT, 1);
    draw_text(d, 24, 192, "CLEAR", COL_TEXT_DIM, 1);
    draw_text(d, 88, 192, "LEAVE", COL_TEXT, 1);
    d->rect(0, SCREEN_H - 3, SCREEN_W, 3, COL_GOLD);
}

void game_render(const Draw *d) {
    int ox = 0;
    int oy = 0;
    if (mode == MODE_TITLE) {
        render_title(d);
        return;
    }
    if (mode == MODE_WIN) {
        render_win(d);
        return;
    }
    if (shake_t > 0) {
        static const int sx[8] = {2, -2, 1, -1, 2, -1, 0, 1};
        ox = sx[shake_t & 7];
        oy = sx[(shake_t + 3) & 7];
    }
    draw_world(d, ox, oy);
    hud(d);
}

void game_snap(Snap *s) {
    int r, c;
    s->x = x;
    s->y = y;
    s->vx = vx;
    s->vy = vy;
    s->level = level;
    s->mode = mode;
    s->phase = phase;
    s->grounded = grounded ? true : false;
    s->can_dash = can_dash ? true : false;
    s->wall_l = wall_l ? true : false;
    s->wall_r = wall_r ? true : false;
    s->goal_x = -1;
    s->goal_y = -1;
    if (mode == MODE_PLAY && level >= 0 && level < FLOORS) {
        for (r = 0; r < ROWS; r++) {
            for (c = 0; c < COLS; c++) {
                if (floors[level][r][c] == '*') {
                    s->goal_x = c * TILE + TILE / 2;
                    s->goal_y = r * TILE + TILE / 2;
                }
            }
        }
    }
}

int game_visual_level(void) {
    if (mode == MODE_WIN) return FLOORS - 1;
    if (mode == MODE_TITLE) return 0;
    return level;
}

int game_mode(void) { return mode; }
int game_level(void) { return level; }
uint32_t game_frames(void) { return frames; }
uint16_t game_falls(void) { return (uint16_t)falls; }
bool game_quit_requested(void) { return quit ? true : false; }
