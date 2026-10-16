#include "bot.h"
#include "game.h"

#include <stdio.h>
#include <string.h>

static int loads;

bool platform_load(Save *out) {
    memset(out, 0, sizeof *out);
    loads++;
    return false;
}

void platform_store(const Save *in) {
    (void)in;
}

static void dump(void) {
    Snap s;
    int r, c;
    game_snap(&s);
    printf("level %d mode %d phase %d pos %d,%d v %d,%d ground %d dash %d walls %d%d goal %d,%d\n",
           s.level, s.mode, s.phase, s.x, s.y, s.vx, s.vy,
           s.grounded ? 1 : 0, s.can_dash ? 1 : 0,
           s.wall_l ? 1 : 0, s.wall_r ? 1 : 0, s.goal_x, s.goal_y);
    for (r = 0; r < ROWS; r++) {
        for (c = 0; c < COLS; c++) {
            char t = game_tile(c, r);
            int pc = (s.x + PW / 2) / TILE;
            int pr = (s.y + PH / 2) / TILE;
            if (c == pc && r == pr) t = '@';
            if (!t) t = ' ';
            fputc(t, stdout);
        }
        fputc('\n', stdout);
    }
}

/* Run until win or until a floor takes too long. Returns the floor index
   we were on when we stopped, or FLOORS if the climb was finished. */
static int climb(int allow_dash, int budget, int verbose) {
    int guard = 0;
    int watch;
    int spent = 0;
    Input in;

    game_init();
    bot_reset();
    bot_allow_dash(allow_dash);
    memset(&in, 0, sizeof in);
    in.confirm = true;
    game_step(&in);

    watch = game_level();
    while (game_mode() != MODE_WIN && guard < 80000) {
        memset(&in, 0, sizeof in);
        if (game_mode() == MODE_TITLE) in.confirm = true;
        else bot_input(&in);
        game_step(&in);
        guard++;
        if (game_level() != watch) {
            if (verbose) {
                printf("  floor %d cleared in %d steps (falls %u)\n",
                       watch + 1, spent, game_falls());
            }
            watch = game_level();
            spent = 0;
        } else if (++spent > budget) {
            if (verbose) {
                printf("  stuck on floor %d after %d steps, falls %u\n",
                       watch + 1, spent, game_falls());
                dump();
            }
            return watch;
        }
    }
    if (game_mode() == MODE_WIN) {
        if (verbose) printf("  floor %d cleared in %d steps (falls %u)\n",
                            watch + 1, spent, game_falls());
        return FLOORS;
    }
    return watch;
}

int main(void) {
    int err = game_map_error();
    int reached, bare;

    if (err) {
        printf("map error %d\n", err);
        return 1;
    }
    printf("maps ok\n");
    printf("climb with dash:\n");
    reached = climb(1, 3600, 1);
    if (reached != FLOORS) {
        printf("FAIL dash climb stopped on floor %d\n", reached + 1);
        return 1;
    }
    printf("time %u frames, falls %u\n", game_frames(), game_falls());

    printf("climb without dash:\n");
    bare = climb(0, 3600, 1);
    /* Floors 1-6 are solvable without a dash. The pit on floor 7 is not. */
    if (bare < 6) {
        printf("FAIL no-dash climb died early on floor %d\n", bare + 1);
        return 1;
    }
    if (bare > 6) {
        printf("FAIL floor 7 can be cleared without a dash (reached %d)\n", bare + 1);
        return 1;
    }
    printf("no-dash stops on floor %d\n", bare + 1);
    printf("ok\n");
    return 0;
}
