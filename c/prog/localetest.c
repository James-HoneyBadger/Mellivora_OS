/*
 * localetest.c - the locale, collation and time-zone contract, as a test
 *
 * docs/POSIX.md says this system has one locale (the C locale), that strcoll
 * sorts by byte order, and that a time zone is an offset the administrator
 * stores rather than a database. Each of those is checked here, including the
 * parts that only a person would otherwise notice: localtime() differs from
 * gmtime() by exactly the stored offset, and %Z says what that offset is.
 *
 * Prints LOCALE_OK when all of it held. The stored offset is restored to what
 * it was, so running the test does not move the machine's clock.
 */
#include <stdio.h>
#include <string.h>
#include <locale.h>
#include <time.h>
#include <mv/syscall.h>

#define TZ_KEY "\\Registry\\Machine\\System\\TimeZone"

#define FAIL(w) do { printf("LOCALE_FAIL %s\n", w); fails++; } while (0)

static int fails;

static int sett_write(const char *text)
{
    return mv_syscall(SYS_SETT, 1, (u32)TZ_KEY, (u32)text, strlen(text) + 1, 0);
}

static int sett_read(char *buf, u32 size)
{
    buf[0] = '\0';
    return mv_syscall(SYS_SETT, 0, (u32)TZ_KEY, (u32)buf, size, 0);
}

int main(void)
{
    char saved[32];
    char line[64];
    time_t epoch = 0;           /* 1970-01-01 00:00:00 UTC, a fixed reference */
    struct tm utc;
    struct tm local;
    struct tm round;
    time_t back;
    struct tm *p;

    /* --- the locale is C, and only C --- */
    if (setlocale(LC_ALL, "C") == NULL) {
        FAIL("setlocale(C)");
    }
    if (setlocale(LC_ALL, "en_US.UTF-8") != NULL) {
        FAIL("setlocale accepted a locale this system does not have");
    }
    printf("locale ok\n");

    /* --- collation is byte order, which is "B" before "a" --- */
    if (strcoll("B", "a") >= 0) {
        FAIL("strcoll is not byte order");
    }
    if (strcmp(strxfrm(line, "caf", sizeof line) == 3 ? line : "x", "caf") != 0) {
        FAIL("strxfrm did not copy the string unchanged");
    }
    printf("collation ok\n");

    /* --- gmtime: the epoch is 1970-01-01 00:00:00, a Thursday --- */
    p = gmtime(&epoch);
    utc = *p;
    if (utc.tm_year != 70 || utc.tm_mon != 0 || utc.tm_mday != 1 ||
        utc.tm_hour != 0 || utc.tm_wday != 4 || utc.tm_yday != 0) {
        FAIL("gmtime(0) is not 1970-01-01");
    }
    printf("gmtime ok\n");

    /* --- difftime is the second count between two times, in either order --- */
    if (difftime(epoch, -60) != 60 || difftime(-60, epoch) != -60) {
        FAIL("difftime is not the difference in seconds");
    }
    printf("difftime ok\n");

    /* --- the stored offset moves localtime by exactly that much --- */
    saved[0] = '\0';
    (void)sett_read(saved, sizeof saved);

    if (sett_write("120") != 0) {
        FAIL("store the offset");
    } else {
        p = localtime(&epoch);
        local = *p;
        if (local.tm_hour != 2 || local.tm_mday != 1) {
            FAIL("localtime did not apply +120 minutes");
        }
        strftime(line, sizeof line, "%Y-%m-%d %H:%M %Z", &local);
        if (strcmp(line, "1970-01-01 02:00 UTC+02:00") != 0) {
            printf("LOCALE_FAIL strftime gave '%s'\n", line);
            fails++;
        }
        /* mktime is the inverse, offset and all. */
        round = local;
        back = mktime(&round);
        if (back != epoch) {
            FAIL("mktime(localtime(t)) is not t");
        }
        printf("timezone ok\n");
    }

    /* --- a negative offset crosses the day boundary correctly --- */
    if (sett_write("-300") != 0) {
        FAIL("store a negative offset");
    } else {
        p = localtime(&epoch);
        local = *p;
        if (local.tm_hour != 19 || local.tm_mday != 31 || local.tm_mon != 11 ||
            local.tm_year != 69) {
            FAIL("localtime did not apply -300 minutes");
        }
        printf("negative offset ok\n");
    }

    /* Put the machine back the way it was. */
    if (saved[0] != '\0') {
        (void)sett_write(saved);
    }

    if (fails != 0) {
        return 1;
    }
    printf("LOCALE_OK\n");
    return 0;
}
