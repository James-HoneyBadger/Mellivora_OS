/*=========================================================================
 * OUTBREAK SHIELD - macOS Terminal Port
 * An Educational Vaccination Simulation Game
 * Inspired by The Oregon Trail, themed around public health
 * Idea inspired by a good friend Robin
 *
 * Ported from Mellivora OS (NASM x86 assembly) to a native ANSI terminal app
 * for macOS.
 *
 * Build on macOS:
 *   clang -O2 -std=c99 -Wall -Wextra -o outbreak_mac programs/outbreak_mac.c
 *
 * Run:
 *   ./outbreak_mac
 *=========================================================================*/

#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*-----------------------------------------------------------------------
 * Constants
 *-----------------------------------------------------------------------*/
#define COMMUNITY_SIZE  200
#define MAX_VACCINES    999
#define MAX_SUPPLIES    999
#define MAX_MORALE      100

/* VGA-style colors mapped to ANSI escape sequences */
#define C_BLACK     0x00
#define C_BLUE      0x01
#define C_GREEN     0x02
#define C_CYAN      0x03
#define C_RED       0x04
#define C_MAGENTA   0x05
#define C_BROWN     0x06
#define C_LGRAY     0x07
#define C_DGRAY     0x08
#define C_LBLUE     0x09
#define C_LGREEN    0x0A
#define C_LCYAN     0x0B
#define C_LRED      0x0C
#define C_LMAGENTA  0x0D
#define C_YELLOW    0x0E
#define C_WHITE     0x0F

/* Background colors */
#define BG_BLUE     0x10
#define BG_GREEN    0x20
#define BG_RED      0x40

/* Sound frequencies (terminal bell fallback on macOS) */
#define SND_GOOD    1200
#define SND_BAD     200
#define SND_ALARM   400
#define SND_VICTORY 1600
#define SND_DEATH   100
#define SND_VACCINE 1000

/* Difficulty */
#define OUTBREAK_BASE   8
#define VACCINE_EFFECT  3

/* Defaults aligned with the latest Mellivora version */
#define DEF_VACCINES    35
#define DEF_SUPPLIES    30
#define DEF_MORALE      60
#define DEF_MONTHS      12
#define DEF_DIFF        1

/*-----------------------------------------------------------------------
 * Terminal state
 *-----------------------------------------------------------------------*/
static struct termios g_orig_termios;
static int g_termios_active = 0;

/*-----------------------------------------------------------------------
 * Game state
 *-----------------------------------------------------------------------*/
static unsigned int rand_seed;

/* Core state */
static int gmonth;
static int population;
static int healthy;
static int vaccinated;
static int infected;
static int recovered;
static int dead;

/* Resources */
static int vaccines;
static int supplies;
static int morale;
static int research;
static int actions_left;
static int difficulty;

/* Lifetime stats */
static int total_vaccinated;
static int total_treated;
static int outbreaks_survived;

/* Buildings */
static int hospital_built;
static int lab_built;

/* Temp */
static int event_type;
static int temp_val;
static int temp_val2;

/* Settings */
static int set_vaccines;
static int set_supplies;
static int set_morale;
static int set_months;
static int set_diff;

/*-----------------------------------------------------------------------
 * Strings
 *-----------------------------------------------------------------------*/

/* Terminal-safe block characters */
#define BLOCK_FULL  '#'
#define BLOCK_SHADE '.'
#define BLOCK_DOT   '*'

static const char *month_names[] = {
    "January", "February", "March", "April",
    "May", "June", "July", "August",
    "September", "October", "November", "December"
};

/* Educational tips */
static const char *tips[] = {
    "Vaccines work by training your immune system to recognize threats.",
    "Herd immunity protects those who cannot be vaccinated.",
    "Smallpox was eradicated entirely through vaccination in 1980.",
    "The first vaccine was developed by Edward Jenner in 1796.",
    "Measles vaccination prevents ~2.6 million deaths per year.",
    "Vaccines undergo rigorous safety testing before approval.",
    "Clean water and vaccines are the two greatest public health tools.",
    "Polio cases have decreased by 99% since vaccination began.",
    "Community vaccination rates above 90% create herd immunity.",
    "The WHO estimates vaccines prevent 3.5-5 million deaths yearly."
};

/* How-to lines */
static const char *howto_lines[] = {
    " ",
    "You lead a community of 200 people through a viral outbreak.",
    "Each month you get 2 actions. Customize settings from the menu!",
    " ",
    "ACTIONS:",
    "  [1] Vaccinate  - Use vaccine doses to immunize the healthy",
    "  [2] Treat Sick - Use medical supplies to cure the infected",
    "  [3] Supply Run - Gather more vaccines and medical supplies",
    "  [4] Research   - Work toward building a Hospital & Lab",
    "  [5] Awareness  - Boost morale, may convince people to vaccinate",
    "  [6] Rest       - Skip an action for a small morale boost",
    " ",
    "TIPS:",
    "  * Vaccinated people are immune to infection",
    "  * High morale helps your response stay effective",
    "  * The Hospital gives bonus treatment; the Lab boosts research",
    "  * Random events can help or hinder your progress",
    "  * The virus gets stronger each month -- stay ahead of it!"
};

/*-----------------------------------------------------------------------
 * Terminal helpers - replacements for Mellivora OS syscalls
 *-----------------------------------------------------------------------*/

static void restore_terminal(void) {
    if (g_termios_active && isatty(STDIN_FILENO)) {
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &g_orig_termios);
    }
    printf("\033[0m\033[?25h\033[H\n");
    fflush(stdout);
}

static void handle_signal(int sig) {
    restore_terminal();
    _exit(128 + sig);
}

static void enable_raw_mode(void) {
    struct termios raw;

    if (!isatty(STDIN_FILENO)) {
        return;
    }
    if (tcgetattr(STDIN_FILENO, &g_orig_termios) == -1) {
        return;
    }

    raw = g_orig_termios;
    raw.c_iflag &= ~(ICRNL | IXON);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == 0) {
        g_termios_active = 1;
        atexit(restore_terminal);
    }
}

static void set_color(int attr) {
    static const int fg_codes[16] = {
        30, 34, 32, 36, 31, 35, 33, 37,
        90, 94, 92, 96, 91, 95, 93, 97
    };
    static const int bg_codes[8] = {40, 44, 42, 46, 41, 45, 43, 47};
    int fg = attr & 0x0F;
    int bg = (attr >> 4) & 0x07;

    if (attr & 0x70)
        printf("\033[0;%d;%dm", fg_codes[fg], bg_codes[bg]);
    else
        printf("\033[0;%dm", fg_codes[fg]);
}

static void set_cursor(int col, int row) {
    printf("\033[%d;%dH", row + 1, col + 1);
}

static void clear_screen(void) {
    printf("\033[2J\033[H");
}

static void print_str(const char *s) {
    fputs(s, stdout);
}

static void put_char(char c) {
    fputc(c, stdout);
}

static int get_char(void) {
    unsigned char ch;

    fflush(stdout);

    if (isatty(STDIN_FILENO)) {
        if (read(STDIN_FILENO, &ch, 1) == 1)
            return ch;
        return 0;
    }

    do {
        int c = getchar();
        if (c == EOF)
            return 0;
        if (c != '\n' && c != '\r')
            return c;
    } while (1);
}

static void do_sleep(int ticks) {
    struct timespec ts;
    long ms = (long)ticks * 55L;

    if (ms < 0)
        ms = 0;

    ts.tv_sec = ms / 1000L;
    ts.tv_nsec = (ms % 1000L) * 1000000L;
    nanosleep(&ts, NULL);
}

static void do_beep(int freq, int ticks) {
    (void)freq;
    fputc('\a', stdout);
    fflush(stdout);
    do_sleep(ticks);
}

/*-----------------------------------------------------------------------
 * PRNG - matches the assembly LCG exactly
 *-----------------------------------------------------------------------*/
static int prng_random(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed >> 16) & 0x7FFF;
}

/*-----------------------------------------------------------------------
 * print_number - print integer as decimal (non-negative)
 *-----------------------------------------------------------------------*/
static void print_number(int n) {
    char buf[16];
    sprintf(buf, "%d", n);
    print_str(buf);
}

/*-----------------------------------------------------------------------
 * clamp_morale
 *-----------------------------------------------------------------------*/
static void clamp_morale(void) {
    if (morale < 0) morale = 0;
    if (morale > MAX_MORALE) morale = MAX_MORALE;
}

/*-----------------------------------------------------------------------
 * pause_message
 *-----------------------------------------------------------------------*/
static void pause_message(void) {
    do_sleep(60);
}

/*-----------------------------------------------------------------------
 * calc_difficulty
 *-----------------------------------------------------------------------*/
static void calc_difficulty_fn(void) {
    int d = gmonth;
    if (set_diff == 0) {        /* Easy */
        d -= 2;
        if (d < 0) d = 0;
    } else if (set_diff == 2) { /* Hard */
        d += 3;
    }
    difficulty = d;
}

/*-----------------------------------------------------------------------
 * calc_infection_rate - returns number of new infections
 *-----------------------------------------------------------------------*/
static int calc_infection_rate(void) {
    int immune_pct, reduction, rate, new_inf, rval;

    /* Immunity percentage */
    immune_pct = (vaccinated + recovered) * 100 / COMMUNITY_SIZE;

    /* Reduction: for each 10% immune, reduce by VACCINE_EFFECT */
    reduction = (immune_pct / 10) * VACCINE_EFFECT;

    /* Difficulty scaling for immunity effectiveness */
    if (set_diff == 1) {
        reduction = (reduction * 3) / 4;   /* Normal: 75% */
    } else if (set_diff == 2) {
        reduction = reduction / 2;         /* Hard: 50% */
    }

    /* Infection rate */
    rate = OUTBREAK_BASE + difficulty - reduction;
    if (set_diff == 2) {
        if (rate < 5) rate = 5;
    } else {
        if (rate < 2) rate = 2;
    }

    /* Apply to healthy */
    new_inf = healthy * rate / 100;

    /* Add randomness +/- 3 */
    rval = prng_random();
    new_inf += (rval % 7) - 3;
    if (set_diff == 2) {
        new_inf += 2;  /* Hard: bias spread slightly upward (-1..+5 net delta) */
    }
    if (new_inf < 0) new_inf = 0;

    return new_inf;
}

/*-----------------------------------------------------------------------
 * print_random_tip
 *-----------------------------------------------------------------------*/
static void print_random_tip(void) {
    int idx = prng_random() % 10;
    print_str(tips[idx]);
}

/*-----------------------------------------------------------------------
 * Play melodies
 *-----------------------------------------------------------------------*/
static void play_title_melody(void) {
    do_beep(523, 4);
    do_beep(659, 4);
    do_beep(784, 4);
    do_beep(1047, 6);
}

static void play_victory_melody(void) {
    do_beep(523, 3);
    do_beep(659, 3);
    do_beep(784, 3);
    do_beep(1047, 3);
    do_beep(784, 3);
    do_beep(1047, 6);
    do_beep(1319, 8);
}

/*-----------------------------------------------------------------------
 * draw_pop_segment - draw 'people' scaled to 66 chars out of 200
 *-----------------------------------------------------------------------*/
static void draw_pop_segment(int people) {
    int chars = (people * 66) / COMMUNITY_SIZE;
    int i;
    for (i = 0; i < chars; i++)
        put_char(BLOCK_FULL);
}

/*-----------------------------------------------------------------------
 * draw_morale_bar - 20-char visual meter
 *-----------------------------------------------------------------------*/
static void draw_morale_bar(void) {
    int filled, empty, i, color;

    if (morale >= 60)
        color = C_LGREEN;
    else if (morale >= 30)
        color = C_YELLOW;
    else
        color = C_LRED;

    set_color(color);

    filled = morale / 4;   /* shr 2 in asm */
    if (filled > 20) filled = 20;

    for (i = 0; i < filled; i++)
        put_char(BLOCK_FULL);

    empty = 20 - filled;
    if (empty > 0) {
        set_color(C_DGRAY);
        for (i = 0; i < empty; i++)
            put_char(BLOCK_SHADE);
    }

    set_color(C_WHITE);
    put_char(' ');
    print_number(morale);
    put_char('%');
}

/*-----------------------------------------------------------------------
 * draw_timeline - show month progress
 *-----------------------------------------------------------------------*/
static void draw_timeline(void) {
    int m;
    for (m = 1; m <= set_months; m++) {
        if (m < gmonth) {
            set_color(C_LGREEN);
            put_char(BLOCK_FULL);
        } else if (m == gmonth) {
            set_color(C_YELLOW);
            put_char(BLOCK_DOT);
        } else {
            set_color(C_DGRAY);
            put_char(BLOCK_SHADE);
        }
        /* separator */
        set_color(C_DGRAY);
        put_char('-');
    }
    set_color(C_DGRAY);
    print_str(" (J F M A M J J A S O N D)");
}

/*-----------------------------------------------------------------------
 * draw_game_screen - main game display
 *-----------------------------------------------------------------------*/
static void draw_game_screen(void) {
    int i, midx;

    clear_screen();

    /* === TOP BAR === */
    set_color(C_WHITE | BG_BLUE);
    set_cursor(0, 0);
    for (i = 0; i < 80; i++) put_char(' ');

    set_cursor(1, 0);
    print_str(" Month: ");
    midx = (gmonth - 1) % 12;
    print_str(month_names[midx]);

    set_cursor(45, 0);
    print_str("Population: ");
    print_number(population);
    put_char('/');
    print_number(COMMUNITY_SIZE);

    /* === POPULATION BAR === */
    set_color(C_WHITE);
    set_cursor(2, 2);
    print_str("Community Health:");

    set_cursor(2, 3);
    set_color(C_LGREEN);
    draw_pop_segment(vaccinated);
    set_color(C_LCYAN);
    draw_pop_segment(healthy);
    set_color(C_YELLOW);
    draw_pop_segment(recovered);
    set_color(C_LRED);
    draw_pop_segment(infected);
    set_color(C_DGRAY);
    draw_pop_segment(dead);

    /* Legend */
    set_cursor(2, 4);
    set_color(C_LGREEN);  print_str("[Vax] ");
    set_color(C_LCYAN);   print_str("[Healthy] ");
    set_color(C_YELLOW);  print_str("[Recovered] ");
    set_color(C_LRED);    print_str("[Infected] ");
    set_color(C_DGRAY);   print_str("[Dead]");

    /* === STATS PANEL - Left column === */
    set_cursor(2, 6);
    set_color(C_LGREEN);
    print_str("Vaccinated: ");
    print_number(vaccinated);

    set_cursor(2, 7);
    set_color(C_LCYAN);
    print_str("Healthy:    ");
    print_number(healthy);

    set_cursor(2, 8);
    set_color(C_LRED);
    print_str("Infected:   ");
    print_number(infected);

    set_cursor(2, 9);
    set_color(C_YELLOW);
    print_str("Recovered:  ");
    print_number(recovered);

    set_cursor(2, 10);
    set_color(C_DGRAY);
    print_str("Deceased:   ");
    print_number(dead);

    /* Right column - Resources */
    set_cursor(35, 6);
    set_color(C_LGREEN);
    print_str("Vaccines:  ");
    print_number(vaccines);

    set_cursor(35, 7);
    set_color(C_LCYAN);
    print_str("Supplies:  ");
    print_number(supplies);

    set_cursor(35, 8);
    set_color(C_WHITE);
    print_str("Morale: ");
    draw_morale_bar();

    set_cursor(35, 9);
    set_color(C_WHITE);
    print_str("Research: ");
    print_number(research);

    /* Buildings */
    set_cursor(35, 10);
    if (hospital_built) {
        set_color(C_LGREEN);
        print_str("[+Hospital]");
    } else {
        set_color(C_DGRAY);
        print_str("[-Hospital]");
    }
    put_char(' ');
    if (lab_built) {
        set_color(C_LGREEN);
        print_str("[+Lab]");
    } else {
        set_color(C_DGRAY);
        print_str("[-Lab]");
    }

    /* === TIMELINE === */
    set_cursor(2, 12);
    set_color(C_WHITE);
    print_str("Outbreak Timeline: [Month 1----12]");

    set_cursor(2, 13);
    draw_timeline();

    /* Actions remaining */
    set_cursor(2, 15);
    set_color(C_WHITE);
    print_str("Actions remaining: ");
    print_number(actions_left);

    /* Separator */
    set_cursor(0, 16);
    set_color(C_DGRAY);
    print_str("--------------------------------------------------------------------------------");
}

/*-----------------------------------------------------------------------
 * draw_action_menu
 *-----------------------------------------------------------------------*/
static void draw_action_menu(void) {
    set_cursor(2, 17);
    set_color(C_WHITE);
    print_str("Choose your action:");

    set_cursor(4, 18);
    set_color(C_LGREEN);
    print_str("[1] Vaccination Drive  (uses vaccines)");

    set_cursor(4, 19);
    set_color(C_LCYAN);
    print_str("[2] Treat the Sick     (uses supplies)");

    set_cursor(4, 20);
    set_color(C_YELLOW);
    print_str("[3] Supply Run         (gather resources)");

    set_cursor(42, 18);
    set_color(C_LMAGENTA);
    print_str("[4] Research           (build upgrades)");

    set_cursor(42, 19);
    set_color(C_LBLUE);
    print_str("[5] Public Awareness   (boost morale)");

    set_cursor(42, 20);
    set_color(C_LGRAY);
    print_str("[6] Rest               (skip action)");
}

/*-----------------------------------------------------------------------
 * draw_month_summary
 *-----------------------------------------------------------------------*/
static void draw_month_summary(void) {
    int midx;

    clear_screen();

    set_color(C_YELLOW);
    set_cursor(20, 0);
    print_str("=== END OF MONTH REPORT ===");

    midx = (gmonth - 1) % 12;
    set_cursor(30, 1);
    set_color(C_WHITE);
    print_str(month_names[midx]);

    set_cursor(5, 3);
    set_color(C_DGRAY);
    print_str("--------------------------------------------");

    /* New infections */
    set_cursor(5, 5);
    set_color(C_LRED);
    print_str("New infections this month: ");
    print_number(temp_val);

    /* Deaths */
    set_cursor(5, 7);
    if (temp_val2 > 0) {
        set_color(C_LRED);
        print_str("Lives lost: ");
        print_number(temp_val2);
        print_str(" people succumbed to the virus.");
        do_beep(SND_DEATH, 5);
    } else {
        set_color(C_LGREEN);
        print_str("No deaths this month!");
    }

    /* Infection rate label */
    set_cursor(5, 9);
    set_color(C_WHITE);
    print_str("Disease pressure is ");

    /* Community bar */
    set_cursor(5, 11);
    set_color(C_WHITE);
    print_str("Community Health:");

    set_cursor(5, 12);
    set_color(C_LGREEN);
    draw_pop_segment(vaccinated);
    set_color(C_LCYAN);
    draw_pop_segment(healthy);
    set_color(C_YELLOW);
    draw_pop_segment(recovered);
    set_color(C_LRED);
    draw_pop_segment(infected);

    /* Morale */
    set_cursor(5, 14);
    set_color(C_WHITE);
    print_str("Morale: ");
    draw_morale_bar();

    /* Educational tip */
    set_cursor(5, 17);
    set_color(C_YELLOW);
    print_str("Did you know? ");
    set_color(C_LGRAY);
    print_random_tip();
}

/*-----------------------------------------------------------------------
 * show_event_screen / print_event_msg
 *-----------------------------------------------------------------------*/
static void show_event_screen(void) {
    set_cursor(5, 19);
    set_color(C_YELLOW);
    print_str("*** BREAKING NEWS ***");
}

static void print_event_msg(const char *msg) {
    set_cursor(5, 20);
    set_color(C_WHITE);
    print_str(msg);
}

/*-----------------------------------------------------------------------
 * draw_final_stats
 *-----------------------------------------------------------------------*/
static void draw_final_stats(void) {
    set_cursor(15, 13);
    set_color(C_LGREEN);
    print_str("Survivors:       ");
    print_number(population);
    put_char('/');
    print_number(COMMUNITY_SIZE);

    set_cursor(15, 14);
    set_color(C_LCYAN);
    print_str("Total vaccinated: ");
    print_number(total_vaccinated);

    set_cursor(15, 15);
    print_str("Total treated:    ");
    print_number(total_treated);

    set_cursor(15, 16);
    set_color(C_LRED);
    print_str("Lives lost:       ");
    print_number(dead);

    set_cursor(15, 17);
    set_color(C_WHITE);
    print_str("Months survived:  ");
    print_number(outbreaks_survived);
}

/*-----------------------------------------------------------------------
 * calc_rating
 *-----------------------------------------------------------------------*/
static void calc_rating(void) {
    int pct;
    set_cursor(15, 19);

    pct = population * 100 / COMMUNITY_SIZE;

    if (pct >= 90) {
        set_color(C_YELLOW);
        print_str("Rating: S - LEGENDARY EPIDEMIOLOGIST! Herd immunity achieved!");
    } else if (pct >= 75) {
        set_color(C_LGREEN);
        print_str("Rating: A - Excellent! Your leadership saved many lives.");
    } else if (pct >= 60) {
        set_color(C_LCYAN);
        print_str("Rating: B - Good effort. The community survived, but at a cost.");
    } else if (pct >= 40) {
        set_color(C_YELLOW);
        print_str("Rating: C - Many were lost. Earlier vaccination could have helped.");
    } else {
        set_color(C_LRED);
        print_str("Rating: D - Devastating losses. Prevention is better than cure.");
    }
}

/*-----------------------------------------------------------------------
 * Forward declarations for game flow
 *-----------------------------------------------------------------------*/
static void title_screen(void);
static void show_howto(void);
static void show_settings(void);
static void new_game(void);
static void game_month_loop(void);
static void game_win(void);
static void game_over(void);
static void exit_game(void);

/*-----------------------------------------------------------------------
 * RANDOM EVENTS
 *-----------------------------------------------------------------------*/
static void trigger_random_event(void) {
    int etype = prng_random() % 8;
    int amt;

    switch (etype) {
    case 0: /* Donation */
        vaccines += 25;
        if (vaccines > MAX_VACCINES) vaccines = MAX_VACCINES;
        supplies += 15;
        if (supplies > MAX_SUPPLIES) supplies = MAX_SUPPLIES;
        morale += 5;
        clamp_morale();
        show_event_screen();
        print_event_msg("A neighboring region donated vaccines and supplies!");
        do_beep(SND_GOOD, 4);
        break;

    case 1: /* Anti-vax rally */
        morale -= 12;
        clamp_morale();
        if (vaccines >= 10) vaccines -= 10;
        show_event_screen();
        print_event_msg("Anti-vaccination rally: misinformation spread, morale dropped.");
        do_beep(SND_BAD, 5);
        break;

    case 2: /* Volunteer */
        morale += 8;
        supplies += 10;
        clamp_morale();
        show_event_screen();
        print_event_msg("Medical volunteers arrived! Supplies and morale boosted.");
        do_beep(SND_GOOD, 3);
        break;

    case 3: /* Mutation */
        amt = vaccinated >> 3;  /* 12.5% */
        if (amt > 0) {
            vaccinated -= amt;
            healthy += amt;
        }
        morale -= 8;
        clamp_morale();
        show_event_screen();
        print_event_msg("The virus mutated! Some vaccinated people lost immunity.");
        do_beep(SND_ALARM, 6);
        break;

    case 4: /* Supply theft */
        vaccines -= vaccines >> 2;  /* lose 25% */
        supplies -= supplies >> 2;
        morale -= 6;
        clamp_morale();
        show_event_screen();
        print_event_msg("Supply warehouse was raided. Lost 25% of vaccines & supplies.");
        do_beep(SND_BAD, 4);
        break;

    case 5: /* Medical team */
        amt = infected;
        if (amt > 10) amt = 10;
        infected -= amt;
        recovered += amt;
        total_treated += amt;
        morale += 6;
        clamp_morale();
        show_event_screen();
        print_event_msg("Emergency medical team treated 10 patients for free!");
        do_beep(SND_GOOD, 4);
        break;

    case 6: /* Quarantine breach */
        amt = infected >> 1;
        if (amt > healthy) amt = healthy;
        healthy -= amt;
        infected += amt;
        morale -= 5;
        clamp_morale();
        show_event_screen();
        print_event_msg("Quarantine breach! Infected people spread the virus further.");
        do_beep(SND_ALARM, 5);
        break;

    case 7: /* Good news */
        morale += 10;
        clamp_morale();
        show_event_screen();
        print_event_msg("Community spirit is high! Everyone is pulling together.");
        do_beep(SND_GOOD, 3);
        break;
    }
}

/*-----------------------------------------------------------------------
 * MONTH END
 *-----------------------------------------------------------------------*/
static void month_end(void) {
    int new_inf, deaths, death_rate, rec, rval;

    /* === INFECTION PHASE === */
    new_inf = calc_infection_rate();
    if (new_inf > healthy) new_inf = healthy;
    temp_val = new_inf;
    healthy -= new_inf;
    infected += new_inf;

    /* === DEATH PHASE === */
    if (infected > 0) {
        death_rate = 10;
        if (morale < 30) death_rate += 5;
        if (hospital_built) {
            death_rate -= 3;
            if (death_rate < 2) death_rate = 2;
        }
        deaths = infected * death_rate / 100;
        if (deaths <= 0 && infected > 10) deaths = 1;
        if (deaths > 0) {
            temp_val2 = deaths;
            infected -= deaths;
            dead += deaths;
            population -= deaths;
            /* Morale hit */
            {
                int mhit = deaths >> 1;
                if (mhit > 0) {
                    morale -= mhit;
                    clamp_morale();
                }
            }
        } else {
            temp_val2 = 0;
        }
    } else {
        temp_val2 = 0;
    }

    /* === NATURAL RECOVERY === */
    if (set_diff == 0) {
        rec = infected * 25 / 100;
    } else if (set_diff == 2) {
        rec = infected * 8 / 100;
    } else {
        rec = infected * 15 / 100;
    }
    if (rec > infected) rec = infected;
    if (rec > 0) {
        infected -= rec;
        recovered += rec;
    }

    /* === MORALE DECAY === */
    if (infected > 20) {
        morale -= 2;
        clamp_morale();
    }

    /* Show month summary */
    draw_month_summary();

    /* === RANDOM EVENT === */
    rval = prng_random() % 100;
    if (rval < 45) {
        trigger_random_event();
    }

    /* Advance month */
    gmonth++;
    outbreaks_survived++;

    /* Wait for player */
    set_cursor(18, 23);
    set_color(C_DGRAY);
    print_str("Press any key to continue...");
    get_char();
}

/*-----------------------------------------------------------------------
 * ACTIONS
 *-----------------------------------------------------------------------*/
static void action_vaccinate(void) {
    int count;

    if (vaccines <= 0) {
        draw_game_screen();
        set_cursor(2, 22);
        set_color(C_LRED);
        print_str("No vaccine doses available! Do a supply run.");
        do_beep(SND_BAD, 3);
        pause_message();
        draw_game_screen();
        return;
    }

    count = vaccines;
    if (count > 20) count = 20;
    if (count > healthy) count = healthy;

    if (count <= 0) {
        draw_game_screen();
        set_cursor(2, 22);
        set_color(C_YELLOW);
        print_str("Everyone is already vaccinated or immune!");
        pause_message();
        draw_game_screen();
        return;
    }

    temp_val = count;
    vaccines -= count;
    healthy -= count;
    vaccinated += count;
    total_vaccinated += count;
    morale += 3;
    clamp_morale();

    draw_game_screen();
    set_cursor(2, 22);
    set_color(C_LGREEN);
    print_str("Vaccination drive successful! Immunized: ");
    print_number(temp_val);
    print_str(" people.");

    do_beep(SND_VACCINE, 3);
    do_beep(1200, 2);

    actions_left--;
    pause_message();
    draw_game_screen();
}

static void action_treat(void) {
    int count, bonus;

    if (infected <= 0) {
        draw_game_screen();
        set_cursor(2, 22);
        set_color(C_LGREEN);
        print_str("Great news -- no one is currently infected!");
        pause_message();
        draw_game_screen();
        return;
    }
    if (supplies <= 0) {
        draw_game_screen();
        set_cursor(2, 22);
        set_color(C_LRED);
        print_str("No medical supplies left! Do a supply run.");
        do_beep(SND_BAD, 3);
        pause_message();
        draw_game_screen();
        return;
    }

    count = supplies;
    if (set_diff == 2) {
        if (count > 10) count = 10;
    } else {
        if (count > 15) count = 15;
    }
    if (count > infected) count = infected;

    temp_val = count;
    supplies -= count;
    infected -= count;
    recovered += count;
    total_treated += count;

    /* Hospital bonus */
    if (hospital_built) {
        bonus = infected;
        if (set_diff == 2) {
            if (bonus > 3) bonus = 3;
        } else {
            if (bonus > 5) bonus = 5;
        }
        infected -= bonus;
        recovered += bonus;
        total_treated += bonus;
        temp_val += bonus;
    }

    draw_game_screen();
    set_cursor(2, 22);
    set_color(C_LCYAN);
    print_str("Medical treatment administered! Cured: ");
    print_number(temp_val);
    print_str(" patients.");

    morale += 2;
    clamp_morale();

    do_beep(SND_GOOD, 2);

    actions_left--;
    pause_message();
    draw_game_screen();
}

static void action_supply_run(void) {
    int vgain, sgain, risk;

    vgain = (prng_random() % 20) + 10;  /* 10-29 */
    vaccines += vgain;
    temp_val = vgain;

    sgain = (prng_random() % 15) + 8;   /* 8-22 */
    supplies += sgain;
    temp_val2 = sgain;

    if (vaccines > MAX_VACCINES) vaccines = MAX_VACCINES;
    if (supplies > MAX_SUPPLIES) supplies = MAX_SUPPLIES;

    /* 15% infection risk */
    risk = prng_random() % 100;
    if (risk < 15 && healthy >= 2) {
        healthy -= 2;
        infected += 2;
        morale -= 3;
        clamp_morale();

        draw_game_screen();
        set_cursor(2, 22);
        set_color(C_YELLOW);
        print_str("Supply run complete, but 2 workers got infected!");
        do_beep(SND_ALARM, 4);
        actions_left--;
        pause_message();
        draw_game_screen();
        return;
    }

    draw_game_screen();
    set_cursor(2, 22);
    set_color(C_LGREEN);
    print_str("Supply run successful! Got: ");
    print_number(temp_val);
    print_str(" vaccines and ");
    print_number(temp_val2);
    print_str(" medical supplies.");

    do_beep(SND_GOOD, 2);

    actions_left--;
    pause_message();
    draw_game_screen();
}

static void action_research(void) {
    int pts;

    pts = (prng_random() % 10) + 5;  /* 5-14 */
    if (lab_built) pts += 5;
    research += pts;
    temp_val = pts;

    /* Check unlocks */
    if (!hospital_built && research >= 30) {
        hospital_built = 1;
        draw_game_screen();
        set_cursor(2, 22);
        set_color(C_YELLOW);
        print_str("*** HOSPITAL BUILT! Treatment capacity increased! ***");
        do_beep(SND_VICTORY, 5);
        morale += 10;
        clamp_morale();
        actions_left--;
        pause_message();
        draw_game_screen();
        return;
    }

    if (hospital_built && !lab_built && research >= 70) {
        lab_built = 1;
        draw_game_screen();
        set_cursor(2, 22);
        set_color(C_YELLOW);
        print_str("*** RESEARCH LAB BUILT! Research gains boosted! ***");
        do_beep(SND_VICTORY, 5);
        morale += 10;
        clamp_morale();
        actions_left--;
        pause_message();
        draw_game_screen();
        return;
    }

    draw_game_screen();
    set_cursor(2, 22);
    set_color(C_LCYAN);
    print_str("Research progress: +");
    print_number(temp_val);
    print_str(" points.");

    do_beep(600, 2);

    actions_left--;
    pause_message();
    draw_game_screen();
}

static void action_awareness(void) {
    int r;

    morale += 8;
    clamp_morale();

    r = prng_random() % 100;
    if (r < 40 && healthy >= 5 && vaccines >= 5) {
        healthy -= 5;
        vaccinated += 5;
        vaccines -= 5;
        total_vaccinated += 5;

        draw_game_screen();
        set_cursor(2, 22);
        set_color(C_LGREEN);
        print_str("Campaign success! 5 people voluntarily got vaccinated!");
    } else {
        draw_game_screen();
        set_cursor(2, 22);
        set_color(C_LCYAN);
        print_str("Public awareness campaign boosted community morale!");
    }

    do_beep(SND_GOOD, 2);
    actions_left--;
    pause_message();
    draw_game_screen();
}

static void action_rest(void) {
    morale += 2;
    clamp_morale();
    actions_left--;
    draw_game_screen();
    set_cursor(2, 22);
    set_color(C_LGRAY);
    print_str("The team rests and regroups. Morale slightly improved.");
    pause_message();
    draw_game_screen();
}

/*-----------------------------------------------------------------------
 * GAME FLOW
 *-----------------------------------------------------------------------*/

static void exit_game(void) {
    set_color(C_LGRAY);
    clear_screen();
    exit(0);
}

static void game_win(void) {
    int ch;
    clear_screen();
    play_victory_melody();

    set_color(C_LGREEN);
    set_cursor(15, 2);
    print_str("*** OUTBREAK CONTAINED! YOU DID IT! ***");

    /* Trophy art */
    set_color(C_YELLOW);
    set_cursor(28, 4);  print_str("     ___________");
    set_cursor(28, 5);  print_str("    '._==_==_=_.'");
    set_cursor(28, 6);  print_str("    .-\\:      /-.");
    set_cursor(28, 7);  print_str("   | (|:.-)(-.|) |");
    set_cursor(28, 8);  print_str("    '-|:.):( |.-'");

    set_color(C_WHITE);
    set_cursor(15, 10);
    print_str("--- Final Statistics ---");

    draw_final_stats();
    calc_rating();

    set_cursor(20, 21);
    set_color(C_LCYAN);
    print_str("Play again? (Y/N)");

    for (;;) {
        ch = get_char();
        if (ch == 'y' || ch == 'Y') { title_screen(); return; }
        if (ch == 'n' || ch == 'N' || ch == 27) exit_game();
    }
}

static void game_over(void) {
    int ch;
    clear_screen();

    do_beep(SND_DEATH, 15);

    set_color(C_LRED);
    set_cursor(20, 2);
    print_str("*** THE OUTBREAK WAS LOST ***");

    /* Skull art */
    set_color(C_WHITE);
    set_cursor(32, 4);  print_str("    _____");
    set_cursor(32, 5);  print_str("   /     \\");
    set_cursor(32, 6);  print_str("  | () () |");
    set_cursor(32, 7);  print_str("   \\  ^  /");
    set_cursor(32, 8);  print_str("    |||||");

    set_color(C_LGRAY);
    set_cursor(8, 10);
    print_str("The community could not survive the Ratel Fever outbreak.");

    set_color(C_WHITE);
    set_cursor(15, 12);
    print_str("--- Final Statistics ---");
    draw_final_stats();

    set_cursor(5, 20);
    set_color(C_YELLOW);
    print_str("Remember: Vaccines are our strongest tool against outbreaks.");

    set_cursor(20, 22);
    set_color(C_LCYAN);
    print_str("Play again? (Y/N)");

    for (;;) {
        ch = get_char();
        if (ch == 'y' || ch == 'Y') { title_screen(); return; }
        if (ch == 'n' || ch == 'N' || ch == 27) exit_game();
    }
}

static void game_month_loop(void) {
    int ch;

    for (;;) {
        /* Check win */
        if (gmonth > set_months) {
            game_win();
            return;
        }

        /* Check lose */
        if (healthy + vaccinated + recovered <= 0) {
            game_over();
            return;
        }

        actions_left = 2;
        calc_difficulty_fn();
        draw_game_screen();

        /* Action loop */
        while (actions_left > 0) {
            draw_action_menu();

            for (;;) {
                ch = get_char();
                if (ch == '1') { action_vaccinate(); break; }
                if (ch == '2') { action_treat(); break; }
                if (ch == '3') { action_supply_run(); break; }
                if (ch == '4') { action_research(); break; }
                if (ch == '5') { action_awareness(); break; }
                if (ch == '6') { action_rest(); break; }
                if (ch == 27) {
                    /* Confirm quit */
                    draw_game_screen();
                    set_cursor(15, 22);
                    set_color(C_YELLOW);
                    print_str("Quit the game? Your community needs you! (Y/N)");
                    ch = get_char();
                    if (ch == 'y' || ch == 'Y') exit_game();
                    draw_game_screen();
                    break;
                }
            }
        }

        month_end();
    }
}

static void new_game(void) {
    do_beep(SND_GOOD, 3);

    gmonth = 1;
    population = COMMUNITY_SIZE;
    healthy = COMMUNITY_SIZE;
    vaccinated = 0;
    infected = 0;
    recovered = 0;
    dead = 0;
    vaccines = set_vaccines;
    supplies = set_supplies;
    morale = set_morale;
    research = 0;
    actions_left = 2;
    total_vaccinated = 0;
    total_treated = 0;
    outbreaks_survived = 0;
    hospital_built = 0;
    lab_built = 0;
    difficulty = 0;
    event_type = 0;

    game_month_loop();
}

/*-----------------------------------------------------------------------
 * Custom settings submenu
 *-----------------------------------------------------------------------*/
static void custom_settings(void) {
    int ch;

    clear_screen();

    set_color(C_YELLOW);
    set_cursor(18, 0);
    print_str("=== CUSTOM SETTINGS ===");

    /* Vaccines */
    set_cursor(5, 3);
    set_color(C_LGREEN);
    print_str("Starting Vaccines: [1] 15  [2] 30  [3] 50  [4] 75");
    for (;;) {
        ch = get_char();
        if (ch == '1') { set_vaccines = 15; break; }
        if (ch == '2') { set_vaccines = 30; break; }
        if (ch == '3') { set_vaccines = 50; break; }
        if (ch == '4') { set_vaccines = 75; break; }
    }

    /* Supplies */
    do_beep(SND_GOOD, 1);
    set_cursor(5, 5);
    set_color(C_LCYAN);
    print_str("Starting Supplies: [1] 15  [2] 30  [3] 50  [4] 70");
    for (;;) {
        ch = get_char();
        if (ch == '1') { set_supplies = 15; break; }
        if (ch == '2') { set_supplies = 30; break; }
        if (ch == '3') { set_supplies = 50; break; }
        if (ch == '4') { set_supplies = 70; break; }
    }

    /* Morale */
    do_beep(SND_GOOD, 1);
    set_cursor(5, 7);
    set_color(C_YELLOW);
    print_str("Starting Morale:   [1] 40% [2] 60% [3] 80% [4] 100%");
    for (;;) {
        ch = get_char();
        if (ch == '1') { set_morale = 40; break; }
        if (ch == '2') { set_morale = 60; break; }
        if (ch == '3') { set_morale = 80; break; }
        if (ch == '4') { set_morale = 100; break; }
    }

    /* Months */
    do_beep(SND_GOOD, 1);
    set_cursor(5, 9);
    set_color(C_LMAGENTA);
    print_str("Game Length:       [1] 6   [2] 12  [3] 18  [4] 24 months");
    for (;;) {
        ch = get_char();
        if (ch == '1') { set_months = 6; break; }
        if (ch == '2') { set_months = 12; break; }
        if (ch == '3') { set_months = 18; break; }
        if (ch == '4') { set_months = 24; break; }
    }

    /* Difficulty */
    do_beep(SND_GOOD, 1);
    set_cursor(5, 11);
    set_color(C_LRED);
    print_str("Difficulty:        [1] Easy  [2] Normal  [3] Hard");
    for (;;) {
        ch = get_char();
        if (ch == '1') { set_diff = 0; break; }
        if (ch == '2') { set_diff = 1; break; }
        if (ch == '3') { set_diff = 2; break; }
    }

    do_beep(SND_VICTORY, 3);

    set_cursor(10, 14);
    set_color(C_LGREEN);
    print_str("Settings saved! Returning to settings overview...");

    pause_message();
    clear_screen();
}

/*-----------------------------------------------------------------------
 * SETTINGS SCREEN
 *-----------------------------------------------------------------------*/
static void show_settings(void) {
    static const char *diff_names[] = { "Easy", "Normal", "Hard" };
    int ch;

    clear_screen();

    for (;;) {
        set_cursor(0, 0);

        set_color(C_YELLOW);
        set_cursor(22, 0);
        print_str("=== GAME SETTINGS ===");

        set_cursor(10, 2);
        set_color(C_LGRAY);
        print_str("Choose a preset or customize individual settings.");

        /* Setting 1: Vaccines */
        set_cursor(5, 4);
        set_color(C_LGREEN);
        print_str("[1] Starting Vaccines:  ");
        set_color(C_WHITE);
        print_number(set_vaccines);

        /* Setting 2: Supplies */
        set_cursor(5, 6);
        set_color(C_LCYAN);
        print_str("[2] Starting Supplies:  ");
        set_color(C_WHITE);
        print_number(set_supplies);

        /* Setting 3: Morale */
        set_cursor(5, 8);
        set_color(C_YELLOW);
        print_str("[3] Starting Morale:    ");
        set_color(C_WHITE);
        print_number(set_morale);
        put_char('%');

        /* Setting 4: Months */
        set_cursor(5, 10);
        set_color(C_LMAGENTA);
        print_str("[4] Game Length:         ");
        set_color(C_WHITE);
        print_number(set_months);
        print_str(" months");

        /* Setting 5: Difficulty */
        set_cursor(5, 12);
        set_color(C_LRED);
        print_str("[5] Difficulty:          ");
        if (set_diff == 0)
            set_color(C_LGREEN);
        else if (set_diff == 1)
            set_color(C_YELLOW);
        else
            set_color(C_LRED);
        print_str(diff_names[set_diff]);

        /* Separator */
        set_cursor(5, 14);
        set_color(C_DGRAY);
        print_str("--------------------------------------------");

        /* Presets */
        set_cursor(5, 16);
        set_color(C_WHITE);
        print_str("Quick Presets:");

        set_cursor(7, 17);
        set_color(C_LGREEN);
        print_str("[1] Easy   - 60 vaccines, 50 supplies, 80% morale");

        set_cursor(7, 18);
        set_color(C_YELLOW);
        print_str("[2] Normal - 35 vaccines, 30 supplies, 60% morale");

        set_cursor(7, 19);
        set_color(C_LRED);
        print_str("[3] Hard   - 15 vaccines, 15 supplies, 40% morale");

        set_cursor(7, 20);
        set_color(C_LCYAN);
        print_str("[4] Custom - Set each value individually");

        set_cursor(12, 22);
        set_color(C_DGRAY);
        print_str("Press ESC to return to title screen.");

        ch = get_char();

        if (ch == '1') {
            set_vaccines = 60; set_supplies = 50;
            set_morale = 80; set_months = 12; set_diff = 0;
            do_beep(SND_GOOD, 2);
            clear_screen();
        } else if (ch == '2') {
            set_vaccines = DEF_VACCINES; set_supplies = DEF_SUPPLIES;
            set_morale = DEF_MORALE; set_months = DEF_MONTHS; set_diff = DEF_DIFF;
            do_beep(SND_GOOD, 2);
            clear_screen();
        } else if (ch == '3') {
            set_vaccines = 15; set_supplies = 15;
            set_morale = 40; set_months = 12; set_diff = 2;
            do_beep(SND_ALARM, 2);
            clear_screen();
        } else if (ch == '4') {
            custom_settings();
        } else if (ch == 27) {
            return;
        }
    }
}

/*-----------------------------------------------------------------------
 * HOW TO PLAY
 *-----------------------------------------------------------------------*/
static void show_howto(void) {
    int i;

    clear_screen();

    set_color(C_YELLOW);
    set_cursor(25, 0);
    print_str("=== HOW TO PLAY ===");

    set_color(C_WHITE);
    for (i = 0; i < 18; i++) {
        set_cursor(2, i + 1);
        print_str(howto_lines[i]);
    }

    set_cursor(20, 22);
    set_color(C_LCYAN);
    print_str("Press any key to return...");

    get_char();
}

/*-----------------------------------------------------------------------
 * TITLE SCREEN
 *-----------------------------------------------------------------------*/
static void title_screen(void) {
    int ch;

    clear_screen();

    /* Top border */
    set_color(C_YELLOW);
    set_cursor(10, 0);
    print_str("+------------------------------------------------------------+");

    /* Title */
    set_cursor(18, 2);
    set_color(C_LRED);
    print_str("* OUTBREAK SHIELD *");

    set_cursor(15, 3);
    set_color(C_WHITE);
    print_str("A Vaccination Simulation Game");

    /* Subtitle */
    set_cursor(12, 5);
    set_color(C_LCYAN);
    print_str("Can you protect your community from Ratel Fever?");

    /* Virus art */
    set_color(C_LRED);
    set_cursor(30, 7);   print_str("    .::::.");
    set_cursor(30, 8);   print_str("  .::o]]:o::.");
    set_cursor(30, 9);   print_str(" .:]:::::::]::.");
    set_cursor(30, 10);  print_str("  .::o]]:o::.");
    set_cursor(30, 11);  print_str("    '::::'");

    /* Syringe art */
    set_color(C_LGREEN);
    set_cursor(8, 8);    print_str("  ____");
    set_cursor(8, 9);    print_str(" |====|-->");
    set_cursor(8, 10);   print_str("  ~~~~");

    /* Shield art */
    set_color(C_LCYAN);
    set_cursor(56, 8);   print_str("  /IIIII\\");
    set_cursor(56, 9);   print_str(" | +++ |");
    set_cursor(56, 10);  print_str("  \\ + /");
    set_cursor(56, 11);  print_str("   \\_/");

    /* Bottom border */
    set_color(C_YELLOW);
    set_cursor(10, 13);
    print_str("+------------------------------------------------------------+");

    /* Story */
    set_color(C_LGRAY);
    set_cursor(5, 15);
    print_str("The year is 2031. A deadly virus called Ratel Fever has");
    set_cursor(5, 16);
    print_str("emerged. You are Dr. Pryor, chief epidemiologist. Lead your");
    set_cursor(5, 17);
    print_str("community through months of deadly outbreak.");

    /* Menu */
    set_color(C_WHITE);
    set_cursor(20, 19); print_str("[1] Begin Outbreak Response");
    set_cursor(20, 20); print_str("[2] How to Play");
    set_cursor(20, 21); print_str("[3] Settings");
    set_cursor(20, 22); print_str("[4] Quit");

    play_title_melody();

    /* Footer */
    set_cursor(14, 23);
    set_color(C_DGRAY);
    print_str("Vaccines save lives. Knowledge is your best weapon.");

    for (;;) {
        ch = get_char();
        if (ch == '1') { new_game(); return; }
        if (ch == '2') { show_howto(); title_screen(); return; }
        if (ch == '3') { show_settings(); title_screen(); return; }
        if (ch == '4' || ch == 27) exit_game();
    }
}

/*-----------------------------------------------------------------------
 * MAIN - Console setup and entry
 *-----------------------------------------------------------------------*/
int main(void) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    enable_raw_mode();

    printf("\033]0;Outbreak Shield - macOS Terminal Port\007");
    printf("\033[?25l");
    fflush(stdout);

    /* Seed PRNG */
    rand_seed = (unsigned int)time(NULL);

    /* Initialize settings */
    set_vaccines = DEF_VACCINES;
    set_supplies = DEF_SUPPLIES;
    set_morale = DEF_MORALE;
    set_months = DEF_MONTHS;
    set_diff = DEF_DIFF;

    title_screen();

    return 0;
}
