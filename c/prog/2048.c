/*
 * 2048.c - slide numbered tiles; equal tiles merge; reach 2048
 *
 *   2048              play
 *   2048 --selftest   check the slide/merge rules and print G2048_OK
 *
 * Keys: arrow keys, WASD or HJKL move; U undoes the last move; R starts a new
 * game; Q or Esc quits. The board is drawn in VGA text mode with one colour
 * per tile value, so the size of a tile reads before its number does.
 *
 * The rules live in slide_row(), which every direction reuses by reading the
 * board in the right order. --selftest exercises that one function against
 * the cases people get wrong (a row of four equal tiles, a merge that must not
 * chain, a gap between equal tiles), so the guest test suite proves the game
 * logic without a keyboard.
 */
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <mv/args.h>
#include <mv/syscall.h>
#include <mv/types.h>

#define N           4
#define CELL_W      8
#define CELL_H      3
#define BOARD_X     22
#define BOARD_Y     4
#define TARGET      2048

#define KEY_UP      0x80
#define KEY_DOWN    0x81
#define KEY_LEFT    0x82
#define KEY_RIGHT   0x83
#define KEY_ESC     0x1B

/* VGA attributes: background in the high nibble, foreground in the low. */
#define A_CANVAS    0x07
#define A_TITLE     0x0E
#define A_DIM       0x08
#define A_LABEL     0x07
#define A_VALUE     0x0F
#define A_GOOD      0x0A
#define A_BAD       0x0C
#define A_FRAME     0x08

enum dir { D_LEFT, D_RIGHT, D_UP, D_DOWN };

struct game {
    int  cell[N][N];
    int  score;
    int  won;          /* 2048 already announced, so keep playing quietly */
};

static struct game g, undo;
static int has_undo;
static int best;

/* ------------------------------------------------------------------ */
/* Console                                                             */
/* ------------------------------------------------------------------ */

static void con_color(int attr) { mv_syscall(SYS_SETCOLOR, (u32)attr, 0, 0, 0, 0); }
static void con_at(int x, int y) { mv_syscall(SYS_SETCURSOR, (u32)x, (u32)y, 0, 0, 0); }
static void con_clear(void)      { mv_syscall(SYS_CLEAR, 0, 0, 0, 0, 0); }
static int  con_key(void)        { return mv_syscall(SYS_GETCHAR, 0, 0, 0, 0, 0) & 0xFF; }

static void put_at(int x, int y, int attr, const char *s)
{
    con_at(x, y);
    con_color(attr);
    mv_print(s);
}

static void fill(int x, int y, int w, int attr)
{
    char line[81];
    if (w > 80) w = 80;
    memset(line, ' ', (size_t)w);
    line[w] = '\0';
    put_at(x, y, attr, line);
}

static int tile_attr(int v)
{
    switch (v) {
    case 0:    return 0x08;
    case 2:    return 0x70;
    case 4:    return 0x60;
    case 8:    return 0x4F;
    case 16:   return 0x5F;
    case 32:   return 0x1F;
    case 64:   return 0x3F;
    case 128:  return 0x2F;
    case 256:  return 0x6F;
    case 512:  return 0x4E;
    case 1024: return 0x1E;
    case 2048: return 0x2E;
    default:   return 0x0E;
    }
}

/* ------------------------------------------------------------------ */
/* Rules                                                               */
/* ------------------------------------------------------------------ */

/*
 * Slide one line of four toward index 0 and merge equal neighbours once.
 * Returns the points scored; *moved is set when anything changed.
 */
static int slide_row(int row[N], int *moved)
{
    int out[N] = {0, 0, 0, 0};
    int n = 0, points = 0;

    for (int i = 0; i < N; i++) {
        if (row[i] == 0)
            continue;
        if (n > 0 && out[n - 1] == row[i] && out[n - 1] > 0) {
            out[n - 1] *= 2;
            points += out[n - 1];
            out[n - 1] = -out[n - 1];      /* mark: merged, cannot merge again */
        } else {
            out[n++] = row[i];
        }
    }
    for (int i = 0; i < N; i++) {
        if (out[i] < 0)
            out[i] = -out[i];
        if (out[i] != row[i])
            *moved = 1;
        row[i] = out[i];
    }
    return points;
}

/* Read line k of the board in the direction of travel, so slide_row's
 * "toward index 0" is the direction the player pressed. */
static int *line_cell(int b[N][N], enum dir d, int k, int i)
{
    switch (d) {
    case D_LEFT:  return &b[k][i];
    case D_RIGHT: return &b[k][N - 1 - i];
    case D_UP:    return &b[i][k];
    default:      return &b[N - 1 - i][k];
    }
}

static int move_board(int b[N][N], enum dir d, int *points)
{
    int moved = 0;
    *points = 0;
    for (int k = 0; k < N; k++) {
        int row[N];
        for (int i = 0; i < N; i++)
            row[i] = *line_cell(b, d, k, i);
        *points += slide_row(row, &moved);
        for (int i = 0; i < N; i++)
            *line_cell(b, d, k, i) = row[i];
    }
    return moved;
}

static int can_move(int b[N][N])
{
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            if (b[y][x] == 0)
                return 1;
            if (x + 1 < N && b[y][x] == b[y][x + 1])
                return 1;
            if (y + 1 < N && b[y][x] == b[y + 1][x])
                return 1;
        }
    return 0;
}

static int max_tile(int b[N][N])
{
    int m = 0;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++)
            if (b[y][x] > m)
                m = b[y][x];
    return m;
}

static void spawn(int b[N][N])
{
    int free_cells[N * N], n = 0;
    for (int i = 0; i < N * N; i++)
        if (b[i / N][i % N] == 0)
            free_cells[n++] = i;
    if (n == 0)
        return;
    int pick = free_cells[rand() % n];
    b[pick / N][pick % N] = (rand() % 10 == 0) ? 4 : 2;
}

static void new_game(void)
{
    memset(&g, 0, sizeof g);
    has_undo = 0;
    spawn(g.cell);
    spawn(g.cell);
}

/* ------------------------------------------------------------------ */
/* Drawing                                                             */
/* ------------------------------------------------------------------ */

static void draw_tile(int col, int row)
{
    int v = g.cell[row][col];
    int attr = tile_attr(v);
    int x = BOARD_X + col * (CELL_W + 1);
    int y = BOARD_Y + row * (CELL_H + 1);
    char text[CELL_W + 1];

    for (int r = 0; r < CELL_H; r++)
        fill(x, y + r, CELL_W, attr);
    if (v == 0) {
        put_at(x + CELL_W / 2, y + 1, attr, ".");
        return;
    }
    int len = snprintf(text, sizeof text, "%d", v);
    put_at(x + (CELL_W - len) / 2, y + 1, attr, text);
}

static void draw_panel(const char *label, int value, int x)
{
    char buf[16];
    snprintf(buf, sizeof buf, "%7d", value);
    put_at(x, 1, A_DIM, label);
    put_at(x, 2, A_VALUE, buf);
}

static void draw_status(const char *msg, int attr)
{
    fill(0, 21, 80, A_CANVAS);
    put_at(BOARD_X, 21, attr, msg);
}

static void draw(void)
{
    if (g.score > best)
        best = g.score;

    put_at(BOARD_X, 1, A_TITLE, "2 0 4 8");
    put_at(BOARD_X, 2, A_DIM, "join the tiles");
    draw_panel("  SCORE", g.score, BOARD_X + 19);
    draw_panel("   BEST", best, BOARD_X + 28);

    for (int row = 0; row < N; row++)
        for (int col = 0; col < N; col++)
            draw_tile(col, row);

    put_at(BOARD_X - 2, 23, A_DIM,
           "arrows/WASD move   U undo   R new game   Q quit");
    con_at(79, 24);
}

/* ------------------------------------------------------------------ */
/* Self-test                                                           */
/* ------------------------------------------------------------------ */

static int expect_row(const char *name, int in0, int in1, int in2, int in3,
                      int out0, int out1, int out2, int out3,
                      int want_points, int want_moved)
{
    int row[N] = {in0, in1, in2, in3};
    int moved = 0;
    int points = slide_row(row, &moved);
    int ok = row[0] == out0 && row[1] == out1 && row[2] == out2 &&
             row[3] == out3 && points == want_points && moved == want_moved;
    char line[96];
    snprintf(line, sizeof line, "  %s  %-26s [%d %d %d %d] +%d\n",
             ok ? "PASS" : "FAIL", name, row[0], row[1], row[2], row[3], points);
    mv_print(line);
    return ok;
}

static int selftest(void)
{
    int ok = 1;
    mv_print("2048 self-test\n");
    ok &= expect_row("four equal merge in pairs", 2, 2, 2, 2,  4, 4, 0, 0,  8, 1);
    ok &= expect_row("a merge does not chain",    4, 4, 8, 0,  8, 8, 0, 0,  8, 1);
    ok &= expect_row("a gap does not block",      2, 0, 0, 2,  4, 0, 0, 0,  4, 1);
    ok &= expect_row("unequal tiles only slide",  0, 2, 0, 4,  2, 4, 0, 0,  0, 1);
    ok &= expect_row("a packed row does not move",2, 4, 8, 16, 2, 4, 8, 16, 0, 0);
    ok &= expect_row("three equal: leading pair", 8, 8, 8, 0,  16, 8, 0, 0, 16, 1);

    int b[N][N] = {{2, 4, 2, 4}, {4, 2, 4, 2}, {2, 4, 2, 4}, {4, 2, 4, 2}};
    int dead = !can_move(b);
    mv_print(dead ? "  PASS  a full board without pairs is over\n"
                  : "  FAIL  a full board without pairs is over\n");
    ok &= dead;

    int c[N][N] = {{2, 0, 0, 0}, {2, 0, 0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
    int points = 0;
    int moved = move_board(c, D_UP, &points);
    int up = moved && c[0][0] == 4 && c[1][0] == 0 && points == 4;
    mv_print(up ? "  PASS  up merges a column\n" : "  FAIL  up merges a column\n");
    ok &= up;

    int r[N][N] = {{2, 2, 0, 0}, {0}, {0}, {0}};
    moved = move_board(r, D_RIGHT, &points);
    int right = moved && r[0][3] == 4 && r[0][0] == 0 && points == 4;
    mv_print(right ? "  PASS  right merges toward the right edge\n"
                   : "  FAIL  right merges toward the right edge\n");
    ok &= right;

    mv_print(ok ? "G2048_OK\n" : "G2048_FAIL\n");
    return ok ? 0 : 1;
}

/* ------------------------------------------------------------------ */
/* Game loop                                                           */
/* ------------------------------------------------------------------ */

static int key_to_dir(int k, enum dir *d)
{
    switch (k) {
    case KEY_LEFT:  case 'a': case 'A': case 'h': case 'H': *d = D_LEFT;  return 1;
    case KEY_RIGHT: case 'd': case 'D': case 'l': case 'L': *d = D_RIGHT; return 1;
    case KEY_UP:    case 'w': case 'W': case 'k': case 'K': *d = D_UP;    return 1;
    case KEY_DOWN:  case 's': case 'S': case 'j': case 'J': *d = D_DOWN;  return 1;
    default: return 0;
    }
}

static int play(void)
{
    srand((unsigned)mv_syscall(SYS_GETTIME, 0, 0, 0, 0, 0));
    con_color(A_CANVAS);
    con_clear();
    new_game();
    draw_status("Merge equal tiles. Reach 2048.", A_LABEL);
    draw();

    for (;;) {
        int k = con_key();
        enum dir d;

        if (k == 'q' || k == 'Q' || k == KEY_ESC)
            break;
        if (k == 'r' || k == 'R') {
            new_game();
            draw_status("New game.", A_LABEL);
            draw();
            continue;
        }
        if (k == 'u' || k == 'U') {
            if (has_undo) {
                g = undo;
                has_undo = 0;
                draw_status("Move undone.", A_LABEL);
            } else {
                draw_status("Nothing to undo - one step is kept.", A_DIM);
            }
            draw();
            continue;
        }
        if (!key_to_dir(k, &d))
            continue;

        struct game before = g;
        int points;
        if (!move_board(g.cell, d, &points)) {
            draw_status("That way is blocked.", A_DIM);
            con_at(79, 24);
            continue;
        }
        undo = before;
        has_undo = 1;
        g.score += points;
        spawn(g.cell);

        if (!g.won && max_tile(g.cell) >= TARGET) {
            g.won = 1;
            draw_status("You made 2048! Keep going for a higher score.", A_GOOD);
        } else if (!can_move(g.cell)) {
            draw_status("No moves left.  R: new game   U: undo   Q: quit", A_BAD);
        } else if (points > 0) {
            char msg[40];
            snprintf(msg, sizeof msg, "+%d", points);
            draw_status(msg, A_GOOD);
        } else {
            draw_status("", A_LABEL);
        }
        draw();
    }

    char bye[64];
    snprintf(bye, sizeof bye, "Final score %d, best tile %d.\n",
             g.score, max_tile(g.cell));
    fill(0, 23, 80, A_CANVAS);
    put_at(0, 23, A_LABEL, bye);
    return 0;
}

int main(void)
{
    static char args[MV_ARGS_BUFSZ];
    char *argv[MV_ARGS_MAX];
    int argc = mv_args("2048", args, sizeof args, argv, MV_ARGS_MAX);

    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--selftest") == 0)
            return selftest();
    return play();
}
