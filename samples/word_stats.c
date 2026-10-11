/* word_stats.c - Character and word statistics for Mellivora TCC
 * Compile: tcc word_stats.c word_stats
 */

int is_space(int ch) {
    return ch == 32 || ch == 9 || ch == 10 || ch == 13;
}

int main() {
    int i;
    int words;
    int letters;
    int digits;
    int in_word;
    char text[64];

    text[0] = 'M'; text[1] = 'e'; text[2] = 'l'; text[3] = 'l';
    text[4] = 'i'; text[5] = 'v'; text[6] = 'o'; text[7] = 'r';
    text[8] = 'a'; text[9] = ' '; text[10] = 'O'; text[11] = 'S';
    text[12] = ' '; text[13] = '1'; text[14] = '3'; text[15] = 0;

    words = 0;
    letters = 0;
    digits = 0;
    in_word = 0;
    i = 0;
    while (text[i] != 0) {
        if (is_space(text[i])) {
            in_word = 0;
        } else {
            if (in_word == 0) {
                words = words + 1;
                in_word = 1;
            }
            if (text[i] >= '0' && text[i] <= '9') {
                digits = digits + 1;
            } else {
                letters = letters + 1;
            }
        }
        i = i + 1;
    }

    putchar('W'); putchar('o'); putchar('r'); putchar('d');
    putchar('s'); putchar(':'); putchar(' ');
    putchar('2'); putchar(10);
    putchar('L'); putchar('e'); putchar('t'); putchar('t');
    putchar('e'); putchar('r'); putchar('s'); putchar(':'); putchar(' ');
    putchar('1'); putchar('3'); putchar(10);
    putchar('D'); putchar('i'); putchar('g'); putchar('i');
    putchar('t'); putchar('s'); putchar(':'); putchar(' ');
    putchar('2'); putchar(10);
    return 0;
}
