// hanoi.c - Tower of Hanoi solver for Mellivora OS TCC
// Prints moves to solve the puzzle with N disks
// Compile: tcc hanoi.c hanoi
// Run:     hanoi

int n;
int from;
int to;
int aux;
int total;

// Print a digit (0-9)
int print_digit;
int print_num;
int print_div;
int print_started;

// Stack for iterative solution
// Each entry: n, from, to, aux, state
int stack_n;
int stack_from;
int stack_to;
int stack_aux;
int stack_state;
int sp;

// Arrays for stack (max 100 deep)
int sn0;  int sn1;  int sn2;  int sn3;  int sn4;
int sn5;  int sn6;  int sn7;  int sn8;  int sn9;
int sn10; int sn11; int sn12; int sn13; int sn14;
int sf0;  int sf1;  int sf2;  int sf3;  int sf4;
int sf5;  int sf6;  int sf7;  int sf8;  int sf9;
int sf10; int sf11; int sf12; int sf13; int sf14;
int st0;  int st1;  int st2;  int st3;  int st4;
int st5;  int st6;  int st7;  int st8;  int st9;
int st10; int st11; int st12; int st13; int st14;
int sa0;  int sa1;  int sa2;  int sa3;  int sa4;
int sa5;  int sa6;  int sa7;  int sa8;  int sa9;
int sa10; int sa11; int sa12; int sa13; int sa14;

int move_count;

int main() {
    // Print header
    putchar('T');
    putchar('o');
    putchar('w');
    putchar('e');
    putchar('r');
    putchar(' ');
    putchar('o');
    putchar('f');
    putchar(' ');
    putchar('H');
    putchar('a');
    putchar('n');
    putchar('o');
    putchar('i');
    putchar(' ');
    putchar('(');
    putchar('4');
    putchar(' ');
    putchar('d');
    putchar('i');
    putchar('s');
    putchar('k');
    putchar('s');
    putchar(')');
    putchar(10);

    n = 4;
    move_count = 0;

    // Solve iteratively: 2^n - 1 moves
    // Using binary counter method
    total = 1;
    from = 0;
    while (from < n) {
        total = total * 2;
        from = from + 1;
    }
    total = total - 1;

    from = 1;
    while (from <= total) {
        move_count = move_count + 1;

        // Determine which disk moves using bit tricks
        // Disk = position of lowest set bit of 'from'
        // Find lowest set bit position
        aux = from;
        to = 0;
        while (((aux / 2) * 2) == aux) {
            aux = aux / 2;
            to = to + 1;
        }

        // Print move number
        print_num = move_count;
        print_started = 0;
        print_div = 100;
        while (print_div > 0) {
            print_digit = print_num / print_div;
            if (print_digit > 0) {
                print_started = 1;
            }
            if (print_started == 1) {
                putchar(48 + print_digit);
            }
            print_num = print_num - (print_digit * print_div);
            print_div = print_div / 10;
        }
        if (print_started == 0) {
            putchar('0');
        }

        putchar('.');
        putchar(' ');
        putchar('D');
        putchar('i');
        putchar('s');
        putchar('k');
        putchar(' ');
        putchar(49 + to);
        putchar(10);

        from = from + 1;
    }

    // Print total
    putchar(10);
    putchar('T');
    putchar('o');
    putchar('t');
    putchar('a');
    putchar('l');
    putchar(':');
    putchar(' ');

    print_num = total;
    print_started = 0;
    print_div = 100;
    while (print_div > 0) {
        print_digit = print_num / print_div;
        if (print_digit > 0) {
            print_started = 1;
        }
        if (print_started == 1) {
            putchar(48 + print_digit);
        }
        print_num = print_num - (print_digit * print_div);
        print_div = print_div / 10;
    }
    if (print_started == 0) {
        putchar('0');
    }

    putchar(' ');
    putchar('m');
    putchar('o');
    putchar('v');
    putchar('e');
    putchar('s');
    putchar(10);

    return 0;
}
