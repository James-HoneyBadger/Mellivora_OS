/* utf.h - the Unicode boundary a program from the parity target expects.
 *
 * EXEC.md section 5.1 puts UTF-16 at the ABI boundary and UTF-8 internally: a
 * program written for that ecosystem calls the wide entry points, and the
 * system underneath speaks one encoding. This header is where the two meet,
 * so only this file has to know about surrogates.
 *
 * Conversion is a real conversion, not a cast: a code point outside the BMP
 * becomes a surrogate pair going out, and a pair is recombined coming in. A
 * lone surrogate - which the target platform allows in a string and no
 * validator accepts - becomes U+FFFD rather than being passed through.
 */
#ifndef MV_UTF_H
#define MV_UTF_H

#include <mv/types.h>

#define MV_UTF8_MAX 4u                  /* bytes one code point can take */
#define MV_REPLACEMENT 0xFFFDu

/* Wide string sizes, in units, needed to hold a UTF-8 string. The +1 is the
 * terminator, so a caller can subtract it to size an allocation. */
u32 mv_utf16_len(const char *utf8);

/* Convert a NUL-terminated UTF-8 string into `out`, which holds `cap` UTF-16
 * units. Always NUL-terminates when cap is not zero, and returns the number of
 * units written (not counting the terminator). Truncation never splits a
 * surrogate pair. */
u32 mv_utf8_to_utf16(const char *utf8, u16 *out, u32 cap);

/* Convert a NUL-terminated UTF-16 string into `out`, `cap` bytes. Returns the
 * bytes written, not counting the terminator. */
u32 mv_utf16_to_utf8(const u16 *wide, char *out, u32 cap);

/* Decode one code point from UTF-8. Returns the code point and stores how many
 * bytes it took in *len; a malformed sequence yields MV_REPLACEMENT and one
 * byte, so a decoder always makes progress. */
u32 mv_utf8_decode(const char *s, u32 *len);

/* Append one code point to a UTF-8 buffer. Returns the bytes written. */
u32 mv_utf8_encode(u32 cp, char *out);

/* The boundary's own print: a UTF-16 string, converted and written.
 * This is the entry point a wide-character program calls. */
void mv_printw(const u16 *wide);

/* The same for one wide character. */
void mv_putwc(u32 cp);

#endif /* MV_UTF_H */
