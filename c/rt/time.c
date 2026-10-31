/*
 * time.c - seconds, broken-down time, and the Sett's time-zone offset
 *
 * The conversions are the textbook ones: civil-from-days and days-from-civil
 * over a proleptic Gregorian calendar, so gmtime(0) is 1970-01-01 00:00:00 and
 * mktime(localtime(t)) is t for every t the RTC can report. Written out rather
 * than tabled because a table is one more thing that can be wrong at the edges.
 *
 * The offset comes from the Sett on every localtime() call: an administrator
 * changes it with `sett set \Registry\Machine\System\TimeZone 120` and the next
 * call sees it, which is the whole of "time-zone data" here. An unset key is
 * UTC, not an error - a machine with no configuration is not a broken machine.
 */
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <mv/syscall.h>

#define TZ_KEY "\\Registry\\Machine\\System\\TimeZone"

struct timespec_bits {
    i32 tv_sec;
    i32 tv_nsec;
};

/* ------------------------------------------------------------------ */
/* The two clocks                                                      */
/* ------------------------------------------------------------------ */

time_t time(time_t *out)
{
    struct timespec_bits ts;

    ts.tv_sec = 0;
    ts.tv_nsec = 0;
    if (mv_syscall(SYS_CLOCK_GETTIME, CLOCK_REALTIME_SEC, (u32)&ts, 0, 0, 0) != 0) {
        if (out != NULL) {
            *out = 0;
        }
        return 0;
    }
    if (out != NULL) {
        *out = ts.tv_sec;
    }
    return ts.tv_sec;
}

/* Minutes east of UTC, from the Sett; 0 when the key is absent or malformed. */
static int tz_offset_minutes(void)
{
    char text[32];
    int sign = 1;
    int value = 0;
    const char *p;

    text[0] = '\0';
    if (mv_syscall(SYS_SETT, 0, (u32)TZ_KEY, (u32)text, sizeof text, 0) != 0) {
        return 0;
    }
    text[sizeof text - 1] = '\0';

    p = text;
    while (*p == ' ') {
        p++;
    }
    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }
    if (*p < '0' || *p > '9') {
        return 0;
    }
    while (*p >= '0' && *p <= '9') {
        value = value * 10 + (*p - '0');
        p++;
    }
    return sign * value;
}

/* ------------------------------------------------------------------ */
/* Civil calendar <-> epoch seconds                                    */
/* ------------------------------------------------------------------ */

/* Days since 1970-01-01 for a civil date (year, month 1..12, day). */
static i32 days_from_civil(i32 y, int m, int d)
{
    i32 era;
    unsigned yoe;
    unsigned doy;
    unsigned doe;

    y -= m <= 2;
    era = (y >= 0 ? y : y - 399) / 400;
    yoe = (unsigned)(y - era * 400);
    doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (i32)doe - 719468;
}

/* The inverse: the civil date a day count names. */
static void civil_from_days(i32 z, i32 *year, int *month, int *day)
{
    i32 era;
    unsigned doe;
    unsigned yoe;
    i32 y;
    unsigned doy;
    unsigned mp;

    z += 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = (unsigned)(z - era * 146097);
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = (i32)yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *day = (int)(doy - (153 * mp + 2) / 5 + 1);
    *month = (int)(mp < 10 ? mp + 3 : mp - 9);
    *year = y + (*month <= 2);
}

static void fill_tm(struct tm *tm, i32 secs)
{
    i32 days = secs / 86400;
    i32 rem = secs % 86400;
    i32 year;
    int month;
    int day;

    if (rem < 0) {
        rem += 86400;
        days -= 1;
    }
    civil_from_days(days, &year, &month, &day);

    tm->tm_hour = (int)(rem / 3600);
    tm->tm_min = (int)((rem % 3600) / 60);
    tm->tm_sec = (int)(rem % 60);
    tm->tm_mday = day;
    tm->tm_mon = month - 1;
    tm->tm_year = (int)(year - 1900);
    /* 1970-01-01 was a Thursday. */
    tm->tm_wday = (int)((days % 7 + 11) % 7);
    tm->tm_yday = (int)(days - days_from_civil(year, 1, 1));
    tm->tm_isdst = 0;
}

static struct tm shared_tm;

struct tm *gmtime(const time_t *t)
{
    i32 secs = (t != NULL) ? *t : 0;

    fill_tm(&shared_tm, secs);
    return &shared_tm;
}

struct tm *localtime(const time_t *t)
{
    i32 secs = (t != NULL) ? *t : 0;

    fill_tm(&shared_tm, secs + (i32)tz_offset_minutes() * 60);
    return &shared_tm;
}

time_t mktime(struct tm *tm)
{
    i32 days;
    i32 secs;

    if (tm == NULL) {
        return 0;
    }
    days = days_from_civil((i32)tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);
    secs = days * 86400 + (i32)tm->tm_hour * 3600 + (i32)tm->tm_min * 60 + tm->tm_sec;
    secs -= (i32)tz_offset_minutes() * 60;
    /* POSIX lets mktime normalise the caller's struct; the fields a program
     * reads back are the ones it would get from localtime(secs). */
    fill_tm(tm, secs + (i32)tz_offset_minutes() * 60);
    return secs;
}

double difftime(time_t a, time_t b)
{
    return (double)(a - b);
}

/* ------------------------------------------------------------------ */
/* strftime, the C locale's fields                                     */
/* ------------------------------------------------------------------ */

static const char *const day_name[7] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char *const day_full[7] = {
    "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
static const char *const mon_name[12] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
static const char *const mon_full[12] = {
    "January", "February", "March", "April", "May", "June",
    "July", "August", "September", "October", "November", "December"
};

struct out {
    char *buf;
    size_t size;
    size_t used;
    int overflow;
};

static void put(struct out *o, char c)
{
    if (o->used + 1 >= o->size) {
        o->overflow = 1;
        return;
    }
    o->buf[o->used++] = c;
}

static void put_text(struct out *o, const char *s)
{
    while (*s != '\0') {
        put(o, *s++);
    }
}

static void put_num(struct out *o, i32 value, int width, char pad)
{
    char digits[12];
    int n = 0;
    int i;

    if (value < 0) {
        put(o, '-');
        value = -value;
    }
    do {
        digits[n++] = (char)('0' + value % 10);
        value /= 10;
    } while (value != 0 && n < (int)sizeof digits);
    for (i = n; i < width; i++) {
        put(o, pad);
    }
    while (n > 0) {
        put(o, digits[--n]);
    }
}

/* "%+02d:%02d" for the zone, e.g. "UTC+02:00". No name exists without a
 * database, and inventing one would be the kind of lie this tree avoids. */
static void put_zone(struct out *o)
{
    int minutes = tz_offset_minutes();
    int mag = minutes < 0 ? -minutes : minutes;

    put_text(o, "UTC");
    put(o, minutes < 0 ? '-' : '+');
    put_num(o, mag / 60, 2, '0');
    put(o, ':');
    put_num(o, mag % 60, 2, '0');
}

size_t strftime(char *buf, size_t size, const char *fmt, const struct tm *tm)
{
    struct out o;
    const char *p;

    if (buf == NULL || size == 0) {
        return 0;
    }
    o.buf = buf;
    o.size = size;
    o.used = 0;
    o.overflow = 0;

    for (p = fmt; p != NULL && *p != '\0'; p++) {
        if (*p != '%') {
            put(&o, *p);
            continue;
        }
        p++;
        switch (*p) {
        case 'Y': put_num(&o, tm->tm_year + 1900, 4, '0'); break;
        case 'y': put_num(&o, (tm->tm_year + 1900) % 100, 2, '0'); break;
        case 'm': put_num(&o, tm->tm_mon + 1, 2, '0'); break;
        case 'd': put_num(&o, tm->tm_mday, 2, '0'); break;
        case 'e': put_num(&o, tm->tm_mday, 2, ' '); break;
        case 'H': put_num(&o, tm->tm_hour, 2, '0'); break;
        case 'I': {
            int h = tm->tm_hour % 12;
            put_num(&o, h == 0 ? 12 : h, 2, '0');
            break;
        }
        case 'M': put_num(&o, tm->tm_min, 2, '0'); break;
        case 'S': put_num(&o, tm->tm_sec, 2, '0'); break;
        case 'j': put_num(&o, tm->tm_yday + 1, 3, '0'); break;
        case 'a': put_text(&o, day_name[tm->tm_wday % 7]); break;
        case 'A': put_text(&o, day_full[tm->tm_wday % 7]); break;
        case 'b': put_text(&o, mon_name[tm->tm_mon % 12]); break;
        case 'B': put_text(&o, mon_full[tm->tm_mon % 12]); break;
        case 'p': put_text(&o, tm->tm_hour < 12 ? "AM" : "PM"); break;
        case 'Z': put_zone(&o); break;
        case '%': put(&o, '%'); break;
        case '\0':
            put(&o, '%');
            p--;
            break;
        default:
            put(&o, *p);
            break;
        }
    }
    buf[o.used] = '\0';
    return o.overflow ? 0 : o.used;
}
