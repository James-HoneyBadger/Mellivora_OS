// echo.c - Character echo program for Mellivora OS TCC
// Reads characters and echoes them back, exit with ESC
// Compile: tcc echo.c echo_c
// Run:     echo_c

int ch;

int main() {
    putchar('T');
    putchar('y');
    putchar('p');
    putchar('e');
    putchar(' ');
    putchar('t');
    putchar('e');
    putchar('x');
    putchar('t');
    putchar(' ');
    putchar('(');
    putchar('E');
    putchar('S');
    putchar('C');
    putchar(' ');
    putchar('t');
    putchar('o');
    putchar(' ');
    putchar('q');
    putchar('u');
    putchar('i');
    putchar('t');
    putchar(')');
    putchar(':');
    putchar(10);

    ch = 0;
    while (ch != 27) {
        ch = getchar();
        if (ch != 27) {
            putchar(ch);
        }
    }
    putchar(10);
    return 0;
}
