/*
 * libctest.c - the C library's own gate
 *
 * Every check here is a promise a program makes about the library rather than
 * about the kernel: that a string function does what the standard says at the
 * edges (an empty needle, a truncated copy that must still be terminated, a
 * tokeniser that keeps its place), that the conversion functions refuse what
 * they cannot convert instead of guessing, that qsort sorts and keeps equal
 * elements in the order they arrived, and that a FILE writes what fwrite was
 * given and fgets gives it back.
 *
 * It prints LIBC_FAIL and the name of the check it lost, and LIBC_OK only when
 * every one of them holds.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <limits.h>
#include <assert.h>
#include <sys/stat.h>

static int fails;

static void ok(int cond, const char *what)
{
    if (!cond) {
        printf("LIBC_FAIL %s\n", what);
        fails++;
    }
}

static int cmp_int(const void *a, const void *b)
{
    int x = *(const int *)a;
    int y = *(const int *)b;

    return x < y ? -1 : (x > y ? 1 : 0);
}

static int cmp_pair(const void *a, const void *b)
{
    /* Sorts on the first field only, so ties show whether the sort is stable. */
    const int *x = (const int *)a;
    const int *y = (const int *)b;

    return x[0] - y[0];
}

int main(void)
{
    char buf[64];

    /* --- strings ------------------------------------------------- */
    {
        char copy[8];

        ok(strlen("hello") == 5, "strlen");
        ok(strnlen("hello", 3) == 3, "strnlen stops at the limit");
        ok(strcmp("abc", "abc") == 0 && strcmp("abc", "abd") < 0 &&
           strcmp("abd", "abc") > 0, "strcmp orders");
        ok(strncmp("abcdef", "abcxyz", 3) == 0, "strncmp compares n bytes");

        memset(copy, 'x', sizeof copy);
        strncpy(copy, "ab", sizeof copy);
        ok(copy[0] == 'a' && copy[1] == 'b' && copy[2] == '\0' &&
           copy[7] == '\0', "strncpy pads with NULs");

        strcpy(buf, "one");
        strcat(buf, "-two");
        strncat(buf, "-three", 3);
        ok(strcmp(buf, "one-two-th") == 0, "strcat and strncat");

        ok(strchr("banana", 'n') == &"banana"[2], "strchr finds the first");
        ok(strrchr("banana", 'n') == &"banana"[4], "strrchr finds the last");
        ok(strchr("banana", 'z') == 0, "strchr reports absence");
        ok(strstr("haystack", "st") == &"haystack"[3], "strstr finds");
        ok(strstr("haystack", "") == &"haystack"[0], "an empty needle matches");
        ok(strstr("hay", "haystack") == 0, "a longer needle does not match");

        ok(strspn("aaabbb", "a") == 3, "strspn");
        ok(strcspn("aaabbb", "b") == 3, "strcspn");
        ok(strpbrk("hello world", " ") == &"hello world"[5], "strpbrk");

        ok(memchr("abcdef", 'd', 6) == &"abcdef"[3], "memchr");
        ok(memchr("abcdef", 'z', 6) == 0, "memchr reports absence");
        ok(strcasecmp("HeLLo", "hello") == 0, "strcasecmp");
        ok(strncasecmp("HELlo", "hello", 3) == 0, "strncasecmp");
        ok(strncasecmp("HELlo", "hello", 5) == 0, "strncasecmp to the end");
        ok(strncasecmp("abc", "abd", 5) < 0, "strncasecmp orders");
    }

    /* --- tokenising ---------------------------------------------- */
    {
        char text[] = "  alpha,beta;;gamma  ";
        char *save = 0;
        char *tok;

        tok = strtok_r(text, " ,;", &save);
        ok(tok != 0 && strcmp(tok, "alpha") == 0, "strtok_r first token");
        tok = strtok_r(0, " ,;", &save);
        ok(tok != 0 && strcmp(tok, "beta") == 0, "strtok_r second token");
        tok = strtok_r(0, " ,;", &save);
        ok(tok != 0 && strcmp(tok, "gamma") == 0, "strtok_r third token");
        ok(strtok_r(0, " ,;", &save) == 0, "strtok_r ends");
    }

    /* --- conversions --------------------------------------------- */
    {
        char *end;

        ok(atoi("  -42xyz") == -42, "atoi skips space and stops at junk");
        ok(atol("2147483647") == 2147483647L, "atol at INT_MAX");

        ok(strtol("0x1f", &end, 0) == 31 && *end == '\0', "strtol hex");
        ok(strtol("017", &end, 0) == 15, "strtol octal by leading zero");
        ok(strtol("-100", 0, 10) == -100, "strtol negative");
        ok(strtol("zz", &end, 10) == 0 && end[0] == 'z', "strtol stops at junk");
        ok(strtoul("4294967295", 0, 10) == 4294967295UL, "strtoul full width");
        ok(abs(-7) == 7 && labs(-9L) == 9L, "abs and labs");
        ok(div(7, 2).quot == 3 && div(7, 2).rem == 1, "div");
    }

    /* --- qsort and bsearch --------------------------------------- */
    {
        int numbers[9] = { 5, 3, 9, 1, 7, 3, 8, 0, 3 };
        int pairs[6][2] = { {2, 0}, {1, 1}, {2, 2}, {0, 3}, {2, 4}, {1, 5} };
        int key = 7;
        int *found;
        int i;

        qsort(numbers, 9, sizeof numbers[0], cmp_int);
        for (i = 1; i < 9; i++) {
            if (numbers[i - 1] > numbers[i]) {
                ok(0, "qsort orders");
                break;
            }
        }
        ok(numbers[0] == 0 && numbers[8] == 9, "qsort puts the ends right");

        found = (int *)bsearch(&key, numbers, 9, sizeof numbers[0], cmp_int);
        ok(found != 0 && *found == 7, "bsearch finds a present key");
        key = 4;
        ok(bsearch(&key, numbers, 9, sizeof numbers[0], cmp_int) == 0,
           "bsearch reports an absent key");

        qsort(pairs, 6, sizeof pairs[0], cmp_pair);
        /* Equal first fields must keep their arrival order: 0,1,1,2,2,2 with
         * the second column in arrival order 3,1,5,0,2,4. */
        ok(pairs[0][0] == 0 && pairs[1][1] == 1 && pairs[2][1] == 5 &&
           pairs[3][1] == 0 && pairs[4][1] == 2 && pairs[5][1] == 4,
           "qsort is stable for equal keys");
    }

    /* --- the heap ------------------------------------------------- */
    {
        char *block = (char *)malloc(100);

        ok(block != 0, "malloc");
        if (block != 0) {
            strcpy(block, "heap");
            block = (char *)realloc(block, 4096);
            ok(block != 0 && strcmp(block, "heap") == 0, "realloc keeps bytes");
            free(block);
        }
        ok(strdup("duplicate") != 0 && strcmp(strdup("duplicate"), "duplicate") == 0,
           "strdup");
    }

    /* --- the formatter ------------------------------------------- */
    {
        char out[128];

        snprintf(out, sizeof out, "%d|%u|%x|%X|%o|%s|%c|%%", -7, 7u, 0xbeefu,
                 0xbeefu, 8u, "s", 'c');
        ok(strcmp(out, "-7|7|beef|BEEF|10|s|c|%") == 0, "the basic conversions");

        snprintf(out, sizeof out, "[%5d][%-5d][%05d][%+d][% d]", 42, 42, 42, 42, 42);
        ok(strcmp(out, "[   42][42   ][00042][+42][ 42]") == 0, "width and flags");

        snprintf(out, sizeof out, "[%.3s][%.5d][%*d][%-*d]", "abcdef", 42, 4, 7, 4, 7);
        ok(strcmp(out, "[abc][00042][   7][7   ]") == 0,
           "precision and a star width");

        snprintf(out, sizeof out, "%#x %#o", 255u, 8u);
        ok(strcmp(out, "0xff 010") == 0, "the # flag");

        ok(snprintf(out, 5, "%s", "abcdefghij") == 10, "snprintf returns the length");
        ok(strcmp(out, "abcd") == 0, "snprintf truncates and terminates");
    }

    /* --- streams -------------------------------------------------- */
    {
        FILE *f;
        static const char text[] = "first\nsecond\nthird\n";
        char first[16];

        {
            int abs_fd;
            int rel_fd;

            /* Absolute and relative names must reach the same file: the kernel
             * resolved only the bare name until this wave. */
            errno = 0;
            abs_fd = open("/libc_test.txt", O_WRONLY | O_CREAT | O_TRUNC);
            ok(abs_fd >= 0, "open of an absolute path creates");
            if (abs_fd >= 0)
                close(abs_fd);
            errno = 0;
            rel_fd = open("libc_test.txt", O_RDONLY);
            ok(rel_fd >= 0, "open of the relative name finds it");
            if (rel_fd >= 0)
                close(rel_fd);
        }
        f = fopen("/libc_test.txt", "w");
        if (f == 0)
            printf("LIBC_FAIL fopen for writing errno=%d\n", errno);
        else
            ok(1, "fopen for writing");
        if (f != 0) {
            ok(fwrite(text, 1, strlen(text), f) == strlen(text), "fwrite");
            ok(fclose(f) == 0, "fclose");
        }

        f = fopen("/libc_test.txt", "r");
        ok(f != 0, "fopen for reading");
        if (f != 0) {
            ok(fgets(first, sizeof first, f) != 0 && strcmp(first, "first\n") == 0,
               "fgets takes one line");
            ok(fgetc(f) == 's', "fgetc continues where fgets stopped");
            ok(ungetc('s', f) == 's', "ungetc");
            ok(fgetc(f) == 's', "the pushed-back byte comes back");
            ok(fseek(f, 0, SEEK_SET) == 0, "fseek");
            ok(fgetc(f) == 'f', "fseek repositions");
            ok(fseek(f, 0, SEEK_END) == 0 && ftell(f) == (long)strlen(text),
               "ftell at the end");
            while (fgetc(f) != EOF)
                ;                       /* drain to EOF */
            ok(feof(f) == 1, "feof after the last byte");
            fclose(f);
        }

        {
            char *line = 0;
            size_t cap = 0;
            long got;
            int lines = 0;

            f = fopen("/libc_test.txt", "r");
            if (f != 0) {
                while ((got = getline(&line, &cap, f)) >= 0) {
                    lines++;
                    (void)got;
                }
                ok(lines == 3, "getline counts the lines");
                free(line);
                fclose(f);
            }
        }

        ok(remove("/libc_test.txt") == 0, "remove");
        ok(fopen("/libc_test.txt", "r") == 0, "the file is gone");
    }

    /* --- perror and strerror ------------------------------------- */
    {
        char *message = strerror(ENOENT);

        ok(strstr(message, "No such file") != 0, "strerror names ENOENT");
        ok(strstr(strerror(65), "65") != 0, "strerror numbers what it does not know");
    }

    /* --- ctype ---------------------------------------------------- */
    ok(isdigit('5') && !isdigit('a'), "isdigit");
    ok(isalpha('a') && !isalpha('5'), "isalpha");
    ok(isspace('\t') && !isspace('x'), "isspace");
    ok(toupper('q') == 'Q' && tolower('Q') == 'q', "toupper and tolower");
    ok(isxdigit('f') && !isxdigit('g'), "isxdigit");
    ok(ispunct(',') && !ispunct('a'), "ispunct");

    /* --- limits.h is what the system enforces --------------------- */
    ok(PATH_MAX == 512 && NAME_MAX == 252, "limits match the filesystem");

    if (fails != 0)
        return 1;
    printf("LIBC_OK\n");
    return 0;
}
