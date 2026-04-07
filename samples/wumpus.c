// wumpus.c - Hunt the Wumpus for Mellivora OS TCC
// A simplified version of the classic game
// Compile: tcc wumpus.c wumpus
// Run:     wumpus

int player;
int wumpus;
int pit1;
int pit2;
int bats;
int arrows;
int alive;
int won;
int ch;
int next;
int seed;
int temp;
int i;

// Room connections (simplified 8-room cave)
// Each room connects to 3 others
// Room 0: 1, 3, 7
// Room 1: 0, 2, 4
// Room 2: 1, 3, 5
// Room 3: 0, 2, 6
// Room 4: 1, 5, 7
// Room 5: 2, 4, 6
// Room 6: 3, 5, 7
// Room 7: 0, 4, 6
int c0a; int c0b; int c0c;
int c1a; int c1b; int c1c;
int c2a; int c2b; int c2c;
int c3a; int c3b; int c3c;
int c4a; int c4b; int c4c;
int c5a; int c5b; int c5c;
int c6a; int c6b; int c6c;
int c7a; int c7b; int c7c;

int adj1; int adj2; int adj3;

int main() {
    // Setup connections
    c0a=1; c0b=3; c0c=7;
    c1a=0; c1b=2; c1c=4;
    c2a=1; c2b=3; c2c=5;
    c3a=0; c3b=2; c3c=6;
    c4a=1; c4b=5; c4c=7;
    c5a=2; c5b=4; c5c=6;
    c6a=3; c6b=5; c6c=7;
    c7a=0; c7b=4; c7c=6;

    // Print title
    putchar('H');putchar('u');putchar('n');putchar('t');
    putchar(' ');putchar('t');putchar('h');putchar('e');
    putchar(' ');putchar('W');putchar('u');putchar('m');
    putchar('p');putchar('u');putchar('s');putchar('!');
    putchar(10);putchar(10);

    // Initialize
    seed = 37;
    player = 0;
    wumpus = 5;
    pit1 = 3;
    pit2 = 6;
    bats = 2;
    arrows = 3;
    alive = 1;
    won = 0;

    while (alive == 1) {
        // Get adjacent rooms
        if (player==0) { adj1=c0a; adj2=c0b; adj3=c0c; }
        if (player==1) { adj1=c1a; adj2=c1b; adj3=c1c; }
        if (player==2) { adj1=c2a; adj2=c2b; adj3=c2c; }
        if (player==3) { adj1=c3a; adj2=c3b; adj3=c3c; }
        if (player==4) { adj1=c4a; adj2=c4b; adj3=c4c; }
        if (player==5) { adj1=c5a; adj2=c5b; adj3=c5c; }
        if (player==6) { adj1=c6a; adj2=c6b; adj3=c6c; }
        if (player==7) { adj1=c7a; adj2=c7b; adj3=c7c; }

        // Show location
        putchar('R');putchar('o');putchar('o');putchar('m');
        putchar(' ');putchar(48+player);putchar(10);

        // Warnings
        if ((adj1==wumpus) | (adj2==wumpus) | (adj3==wumpus)) {
            putchar('Y');putchar('o');putchar('u');putchar(' ');
            putchar('s');putchar('m');putchar('e');putchar('l');
            putchar('l');putchar(' ');putchar('a');putchar(' ');
            putchar('W');putchar('u');putchar('m');putchar('p');
            putchar('u');putchar('s');putchar('!');putchar(10);
        }
        if ((adj1==pit1) | (adj2==pit1) | (adj3==pit1) | (adj1==pit2) | (adj2==pit2) | (adj3==pit2)) {
            putchar('Y');putchar('o');putchar('u');putchar(' ');
            putchar('f');putchar('e');putchar('e');putchar('l');
            putchar(' ');putchar('a');putchar(' ');
            putchar('d');putchar('r');putchar('a');putchar('f');
            putchar('t');putchar(10);
        }
        if ((adj1==bats) | (adj2==bats) | (adj3==bats)) {
            putchar('Y');putchar('o');putchar('u');putchar(' ');
            putchar('h');putchar('e');putchar('a');putchar('r');
            putchar(' ');
            putchar('b');putchar('a');putchar('t');putchar('s');
            putchar(10);
        }

        // Show exits
        putchar('E');putchar('x');putchar('i');putchar('t');
        putchar('s');putchar(':');putchar(' ');
        putchar(48+adj1);putchar(' ');
        putchar(48+adj2);putchar(' ');
        putchar(48+adj3);putchar(10);

        // Show arrows
        putchar('A');putchar('r');putchar('r');putchar('o');
        putchar('w');putchar('s');putchar(':');putchar(' ');
        putchar(48+arrows);putchar(10);

        // Get action
        putchar('M');putchar('o');putchar('v');putchar('e');
        putchar(' ');putchar('o');putchar('r');putchar(' ');
        putchar('S');putchar('h');putchar('o');putchar('o');
        putchar('t');putchar('?');putchar(' ');

        ch = getchar();
        putchar(10);

        if ((ch == 109) | (ch == 77)) {
            // 'm' or 'M' - move
            putchar('W');putchar('h');putchar('i');putchar('c');
            putchar('h');putchar(' ');putchar('r');putchar('o');
            putchar('o');putchar('m');putchar('?');putchar(' ');
            ch = getchar();
            putchar(10);
            next = ch - 48;
            if ((next==adj1) | (next==adj2) | (next==adj3)) {
                player = next;
                // Check hazards
                if (player == wumpus) {
                    putchar('T');putchar('h');putchar('e');putchar(' ');
                    putchar('W');putchar('u');putchar('m');putchar('p');
                    putchar('u');putchar('s');putchar(' ');
                    putchar('g');putchar('o');putchar('t');putchar(' ');
                    putchar('y');putchar('o');putchar('u');putchar('!');
                    putchar(10);
                    alive = 0;
                }
                if ((player==pit1) | (player==pit2)) {
                    putchar('F');putchar('e');putchar('l');putchar('l');
                    putchar(' ');putchar('i');putchar('n');putchar(' ');
                    putchar('a');putchar(' ');putchar('p');putchar('i');
                    putchar('t');putchar('!');putchar(10);
                    alive = 0;
                }
                if (player == bats) {
                    putchar('B');putchar('a');putchar('t');putchar('s');
                    putchar('!');putchar(10);
                    // Random room
                    seed = ((seed * 7) + 3) - ((((seed * 7) + 3) / 8) * 8);
                    if (seed < 0) { seed = 0 - seed; }
                    player = seed;
                }
            } else {
                putchar('N');putchar('o');putchar(' ');
                putchar('e');putchar('x');putchar('i');putchar('t');
                putchar(10);
            }
        }
        if ((ch == 115) | (ch == 83)) {
            // 's' or 'S' - shoot
            if (arrows > 0) {
                putchar('W');putchar('h');putchar('i');putchar('c');
                putchar('h');putchar(' ');putchar('r');putchar('o');
                putchar('o');putchar('m');putchar('?');putchar(' ');
                ch = getchar();
                putchar(10);
                next = ch - 48;
                arrows = arrows - 1;
                if (next == wumpus) {
                    putchar('Y');putchar('o');putchar('u');putchar(' ');
                    putchar('g');putchar('o');putchar('t');putchar(' ');
                    putchar('t');putchar('h');putchar('e');putchar(' ');
                    putchar('W');putchar('u');putchar('m');putchar('p');
                    putchar('u');putchar('s');putchar('!');putchar(10);
                    won = 1;
                    alive = 0;
                } else {
                    putchar('M');putchar('i');putchar('s');putchar('s');
                    putchar('!');putchar(10);
                    if (arrows == 0) {
                        putchar('N');putchar('o');putchar(' ');
                        putchar('a');putchar('r');putchar('r');
                        putchar('o');putchar('w');putchar('s');
                        putchar('!');putchar(10);
                        alive = 0;
                    }
                }
            }
        }
    }

    putchar(10);
    if (won == 1) {
        putchar('Y');putchar('o');putchar('u');putchar(' ');
        putchar('W');putchar('i');putchar('n');putchar('!');
    } else {
        putchar('G');putchar('a');putchar('m');putchar('e');
        putchar(' ');putchar('O');putchar('v');putchar('e');
        putchar('r');
    }
    putchar(10);

    return 0;
}
