/*
 * time.h - seconds, broken-down time, and the one time zone this system has
 *
 * Two clocks behind one face: CLOCK_REALTIME is the RTC's wall-clock seconds
 * and CLOCK_MONOTONIC is ticks since boot. A program asks for the second one
 * when it means "how long did this take", because wall-clock time can be set
 * backwards and a duration measured on it can come out negative.
 *
 * Time zones are an *offset*, not a database: localtime() reads the offset in
 * minutes from the Sett at \Registry\Machine\System\TimeZone and adds it to
 * UTC. No tzdata means no DST rule and no historical zones, so tm_isdst is
 * always 0 and the value is whatever the administrator stored. That is the
 * whole contract, written down rather than implied - see docs/POSIX.md.
 */
#ifndef ETIME_H
#define ETIME_H

#include <mv/types.h>
#include <stddef.h>

/* 32-bit seconds: the same range the RTC and every file timestamp carry. */
typedef i32 time_t;

struct tm {
    int tm_sec;     /* 0..60 */
    int tm_min;     /* 0..59 */
    int tm_hour;    /* 0..23, after the offset has been applied */
    int tm_mday;    /* 1..31 */
    int tm_mon;     /* 0..11 */
    int tm_year;    /* since 1900 */
    int tm_wday;    /* 0..6, Sunday = 0 */
    int tm_yday;    /* 0..365 */
    int tm_isdst;   /* always 0: no DST rule exists without tzdata */
};

#define CLOCK_REALTIME_SEC  0
#define CLOCK_MONOTONIC_SEC 1

/* Seconds since the epoch; NULL is allowed (POSIX allows a protected value). */
time_t time(time_t *t);

/* Broken-down UTC, and broken-down local time (UTC plus the Sett's offset).
 * Both return a pointer to a per-call buffer that the next call overwrites. */
struct tm *gmtime(const time_t *t);
struct tm *localtime(const time_t *t);

/* The inverse of localtime(): the seconds a broken-down local time names. */
time_t mktime(struct tm *tm);

/* Difference in seconds between two times. POSIX says double; this runtime
 * has no floating point, so the difference is the 32-bit second count and a
 * difference outside that range saturates (docs/POSIX.md, "what does not
 * conform"). */
time_t difftime(time_t a, time_t b);

/*
 * The C locale's strftime: %Y %y %m %d %e %H %I %M %S %j %a %A %b %B %p %Z %%
 * Everything else is copied through with the '%' dropped, because a formatter
 * that silently forgets a field is worse than one that shows something.
 */
size_t strftime(char *buf, size_t size, const char *fmt, const struct tm *tm);

#endif /* ETIME_H */
