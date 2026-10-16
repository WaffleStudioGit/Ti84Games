#include "bot.h"

#include <string.h>

typedef struct {
    int hold;
    int jump_cd;
    int dash_cd;
    int pending_dash;
    int stuck;
    int prev_x, prev_y;
    int seek;
    int seek_t;
    int allow_dash;
    int level;
} Bot;

static Bot bot;

static int iabs(int v) { return v < 0 ? -v : v; }

static int standable(char t) {
    return t == '#' || t == 'S' || t == '-';
}

void bot_reset(void) {
    memset(&bot, 0, sizeof bot);
    bot.seek = 1;
    bot.allow_dash = 1;
    bot.level = -1;
}

void bot_allow_dash(int allow) {
    bot.allow_dash = allow;
}

static int in_shaft(int col, int foot) {
    int left = 0;
    int right = 0;
    int c;
    for (c = col - 1; c >= col - 5; c--) {
        if (game_tile(c, foot - 1) == '#') {
            left = 1;
            break;
        }
    }
    for (c = col + 1; c <= col + 5; c++) {
        if (game_tile(c, foot - 1) == '#') {
            right = 1;
            break;
        }
    }
    return left && right;
}

static int gap_width(const Snap *s, int dir, int foot) {
    int gap = 0;
    int c;
    if (dir == 0) return 0;
    for (c = (s->x + PW / 2) / TILE + dir; c >= 0 && c < COLS && gap < 14; c += dir) {
        if (standable(game_tile(c, foot))) break;
        gap++;
    }
    return gap;
}

void bot_input(Input *in) {
    Snap s;
    int dir;
    int cx, foot, col, dx, gap;
    int want_up;
    int shaft;
    int at_edge = 0;
    char below, ahead;

    memset(in, 0, sizeof *in);
    game_snap(&s);
    if (s.mode != MODE_PLAY || s.phase != PHASE_PLAY) return;
    if (s.level != bot.level) {
        bot.level = s.level;
        bot.stuck = 0;
        bot.hold = 0;
        bot.pending_dash = 0;
        bot.seek_t = 0;
    }

    cx = s.x + PW / 2;
    dx = s.goal_x - cx;
    want_up = s.goal_y >= 0 && s.goal_y < s.y - 16;
    foot = (s.y + PH) / TILE;
    if (foot < 0) foot = 0;
    if (foot > ROWS - 1) foot = ROWS - 1;
    col = cx / TILE;
    if (col < 0) col = 0;
    if (col > COLS - 1) col = COLS - 1;
    shaft = in_shaft(col, foot);
    dir = 0;
    {
        int dead = shaft ? 80 : 8;
        if (dx > dead) dir = 1;
        else if (dx < -dead) dir = -1;
    }

    if (s.wall_l && !s.wall_r) {
        in->left = true;
        bot.seek = 1;
        bot.seek_t = 0;
    } else if (s.wall_r && !s.wall_l) {
        in->right = true;
        bot.seek = -1;
        bot.seek_t = 0;
    }
    else if (dir > 0) in->right = true;
    else if (dir < 0) in->left = true;
    else if (want_up && shaft) {
        if (bot.seek > 0) in->right = true;
        else in->left = true;
        bot.seek_t++;
        if (bot.seek_t > 50 && bot.stuck > 12) {
            bot.seek = -bot.seek;
            bot.seek_t = 0;
        }
    }

    if (iabs(s.x - bot.prev_x) < 2 && iabs(s.y - bot.prev_y) < 2) bot.stuck++;
    else bot.stuck = 0;
    bot.prev_x = s.x;
    bot.prev_y = s.y;

    gap = gap_width(&s, dir != 0 ? dir : 1, foot);

    below = game_tile(col, foot);
    if (dir != 0) {
        ahead = game_tile(col + dir, foot);
        if (s.grounded && standable(below) && !standable(ahead)) at_edge = 1;
    }

    if (bot.jump_cd > 0) bot.jump_cd--;
    if (bot.dash_cd > 0) bot.dash_cd--;

    if (bot.hold > 0) {
        if (!s.grounded && s.vy >= 0) bot.hold = 0;
        else {
            bot.hold--;
            in->jump = true;
        }
    }

    if (bot.hold == 0 && bot.jump_cd == 0 && s.grounded) {
        int want = 0;
        int k;
        if (at_edge) want = 1;
        if (want_up && below == '-') want = 1;
        if (!shaft && want_up) {
            int dc;
            for (dc = -1; dc <= 1 && !want; dc++) {
                for (k = 1; k <= 4; k++) {
                    if (standable(game_tile(col + dc, foot - k))) {
                        want = 1;
                        break;
                    }
                }
            }
        }
        if ((s.wall_l || s.wall_r) && want_up) want = 1;
        if (want) {
            bot.hold = 40;
            bot.jump_cd = 6;
            in->jump = true;
            if (bot.allow_dash && at_edge && gap >= 5) bot.pending_dash = 7;
        }
    }

    if (!s.grounded && bot.hold == 0 && bot.jump_cd == 0 &&
        (s.wall_l || s.wall_r) && s.vy > 0 && want_up) {
        bot.hold = 40;
        bot.jump_cd = 4;
        in->jump = true;
    }

    if (bot.pending_dash > 0) {
        bot.pending_dash--;
        if (bot.pending_dash == 0 && !s.grounded && s.can_dash && bot.allow_dash) {
            in->dash = true;
            bot.dash_cd = 20;
        }
    } else if (bot.allow_dash && bot.dash_cd == 0 && s.can_dash && !s.grounded &&
               want_up && s.vy > 80 && iabs(dx) < 40 && gap < 5) {
        in->dash = true;
        in->up = true;
        in->left = false;
        in->right = false;
        bot.dash_cd = 24;
    }

    if (bot.stuck == 50) {
        bot.hold = 40;
        in->jump = true;
        if (bot.allow_dash && s.can_dash) {
            in->dash = true;
            in->up = true;
        }
    }
    if (bot.stuck > 90) {
        bot.seek = -bot.seek;
        bot.stuck = 0;
    }
}
