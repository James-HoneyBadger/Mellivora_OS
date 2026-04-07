// calc.c - Integer calculator for Mellivora OS TCC
// Evaluates simple integer expressions with + - * /
// Reads expression character by character, supports multi-digit numbers
// Compile: tcc calc.c calc
// Run:     calc

int ch;
int num;
int result;
int op;
int done;
int neg;
int digit;
int tmp;

// Print a decimal number
int pr_num;
int pr_d;
int pr_rem;
int pr_started;

int print_dec() {
    pr_d = pr_num;
    if (pr_d < 0) {
        putchar(45); // '-'
        pr_d = 0 - pr_d;
    }
    if (pr_d == 0) {
        putchar(48);
        return 0;
    }

    // Find largest power of 10
    pr_started = 0;

    // Handle up to 1000000000
    pr_rem = 1000000000;
    while (pr_rem > 0) {
        tmp = pr_d / pr_rem;
        if (tmp > 0) {
            pr_started = 1;
        }
        if (pr_started == 1) {
            putchar(48 + tmp);
        }
        pr_d = pr_d - (tmp * pr_rem);
        pr_rem = pr_rem / 10;
    }

    return 0;
}

// Print a string char by char
int print_s0;
int print_s1;
int print_s2;
int print_s3;
int print_s4;
int print_s5;
int print_s6;
int print_s7;
int print_s8;
int print_s9;
int print_s10;
int print_s11;
int print_s12;
int print_s13;
int print_s14;
int print_s15;
int print_len;
int print_i;

int print_str() {
    print_i = 0;
    while (print_i < print_len) {
        ch = 0;
        if (print_i==0) { ch=print_s0; }
        if (print_i==1) { ch=print_s1; }
        if (print_i==2) { ch=print_s2; }
        if (print_i==3) { ch=print_s3; }
        if (print_i==4) { ch=print_s4; }
        if (print_i==5) { ch=print_s5; }
        if (print_i==6) { ch=print_s6; }
        if (print_i==7) { ch=print_s7; }
        if (print_i==8) { ch=print_s8; }
        if (print_i==9) { ch=print_s9; }
        if (print_i==10) { ch=print_s10; }
        if (print_i==11) { ch=print_s11; }
        if (print_i==12) { ch=print_s12; }
        if (print_i==13) { ch=print_s13; }
        if (print_i==14) { ch=print_s14; }
        if (print_i==15) { ch=print_s15; }
        if (ch > 0) { putchar(ch); }
        print_i = print_i + 1;
    }
    return 0;
}

// Read a number from input
int read_number() {
    num = 0;
    neg = 0;
    if (ch == 45) {    // '-'
        neg = 1;
        ch = getchar();
    }
    while ((ch >= 48) && (ch <= 57)) {
        digit = ch - 48;
        num = (num * 10) + digit;
        ch = getchar();
    }
    if (neg == 1) {
        num = 0 - num;
    }
    return 0;
}

int main() {
    // Print banner
    // "Calc - Integer Calculator"
    print_s0=67; print_s1=97; print_s2=108; print_s3=99;
    print_s4=32; print_s5=45; print_s6=32;
    print_s7=73; print_s8=110; print_s9=116;
    print_s10=101; print_s11=103; print_s12=101;
    print_s13=114;
    print_len = 14;
    print_str();
    putchar(10);

    // "Type expression (e.g. 12+34) or q to quit"
    // "Enter expression:"
    // Simplified: "> " prompt

    done = 0;
    while (done == 0) {
        // Print prompt "> "
        putchar(62);
        putchar(32);

        // Read first char
        ch = getchar();

        // Check for quit
        if (ch == 113) {    // 'q'
            done = 1;
        }
        if (ch == 81) {     // 'Q'
            done = 1;
        }

        if (done == 0) {
            // Skip spaces
            while (ch == 32) {
                ch = getchar();
            }

            // Read first number
            read_number();
            result = num;

            // Process operators
            while ((ch != 10) && (ch != 13)) {
                // Skip spaces
                while (ch == 32) {
                    ch = getchar();
                }

                // Read operator
                op = ch;
                if ((op != 43) && (op != 45) && (op != 42) && (op != 47)) {
                    // Not +, -, *, /
                    // Print "Error"
                    print_s0=69; print_s1=114; print_s2=114;
                    print_s3=111; print_s4=114;
                    print_len = 5;
                    print_str();
                    putchar(10);
                    // Consume rest of line
                    while ((ch != 10) && (ch != 13)) {
                        ch = getchar();
                    }
                    op = 0;
                } else {
                    ch = getchar();

                    // Skip spaces
                    while (ch == 32) {
                        ch = getchar();
                    }

                    // Read second number
                    read_number();

                    // Apply operator
                    if (op == 43) {          // '+'
                        result = result + num;
                    }
                    if (op == 45) {          // '-'
                        result = result - num;
                    }
                    if (op == 42) {          // '*'
                        result = result * num;
                    }
                    if (op == 47) {          // '/'
                        if (num == 0) {
                            // "Div by 0"
                            print_s0=68; print_s1=105; print_s2=118;
                            print_s3=32; print_s4=98; print_s5=121;
                            print_s6=32; print_s7=48;
                            print_len = 8;
                            print_str();
                            putchar(10);
                            op = 0;
                            // consume rest of line
                            while ((ch != 10) && (ch != 13)) {
                                ch = getchar();
                            }
                        } else {
                            result = result / num;
                        }
                    }
                }
            }

            // Print result if valid
            if (op != 0) {
                // "= "
                putchar(61);
                putchar(32);
                pr_num = result;
                print_dec();
                putchar(10);
            }
        }
    }

    // "Bye!"
    print_s0=66; print_s1=121; print_s2=101; print_s3=33;
    print_len = 4;
    print_str();
    putchar(10);

    return 0;
}
