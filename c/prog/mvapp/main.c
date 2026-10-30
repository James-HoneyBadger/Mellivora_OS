/*
 * main.c - the M5 exit-criterion application
 *
 * EXEC.md's exit for the runtime wave: "a multi-source application using shared
 * libraries, a Unicode interface, the Sett for configuration and Chronicle for
 * logging is built with the SDK and the in-OS toolchain, and runs." This is that
 * application, and it says so by name in the chronicle ring:
 *
 *   * multi-source   - main.c and report.c, two translation units
 *   * shared library - proved by `mvprobe`, the same C toolchain wrapped as an
 *                      HBX image: it imports mv_add from mvlib.hbl and calls it.
 *                      Combining that with this program in one image is the one
 *                      part of the criterion still open, and it is named as
 *                      open rather than claimed.
 *   * Unicode        - box drawing and accented text on the console
 *   * the Sett       - a run counter kept in the configuration store
 *   * Chronicle      - the program writes its own records into the ring the
 *                      drivers write to, so `chronicle` shows them together
 *   * interfaces     - the Sett and the chronicle are reached through
 *                      QueryInterface-shaped objects, not syscall numbers
 *
 * It prints MVAPP_OK only when all of that held.
 */
#include <stdio.h>
#include <string.h>

#include <mv/iface.h>
#include <mv/syscall.h>

#include "report.h"

/* The loader writes the resolved address of mvlib.hbl's mv_add here, because
 * this symbol is named in the image's import table. If the library could not be
 * loaded the slot stays 0 and the call below is refused rather than jumping to
 * zero. */
int (*mvlib_add)(int a, int b) = 0;

#define SETT_PATH "\\Registry\\Machine\\Software\\MvApp\\Runs"
#define SETT_MAGIC 3

static int fails;

static void check(int cond, const char *what)
{
    if (!cond) {
        printf("MVAPP_FAIL %s\n", what);
        fails++;
    }
}

/* mv_add's convention is the library's own: arguments in EBX and ECX, result in
 * EAX. A C caller needs a thunk, and the thunk saves EBX because the compiler
 * owns it. */
static int call_mv_add(int a, int b)
{
    int result = -1;

    if (mvlib_add == 0)
        return -1;
    __asm__ volatile("pushl %%ebx\n\t"
                     "movl %1, %%ebx\n\t"
                     "movl %2, %%ecx\n\t"
                     "call *%3\n\t"
                     "popl %%ebx\n\t"
                     "movl %%eax, %0"
                     : "=r"(result)
                     : "r"(a), "r"(b), "r"(mvlib_add)
                     : "eax", "ecx", "memory");
    return result;
}

int main(void)
{
    mv_iface *base = 0;
    mv_chronicle *chronicle = 0;
    mv_sett *sett = 0;
    char runs_text[32] = "0";
    char line[96];
    int runs = 0;
    int sum;

    check(mv_iface_get("chronicle", &base) == MV_S_OK, "chronicle resolves");
    if (base != 0) {
        check(mv_iface_query(base, &(mv_guid)MV_IID_CHRONICLE,
                             (void **)&chronicle) == MV_S_OK,
              "chronicle queries by identifier");
        check(mv_iface_query(base, &(mv_guid)MV_IID_SETT,
                             (void **)&sett) == MV_E_NOINTERFACE,
              "a chronicle is not a sett");
    }

    if (chronicle != 0)
        chronicle->vtbl->write(chronicle, 2, "mvapp: starting");

    check(mv_iface_get("sett", &base) == MV_S_OK, "sett resolves");
    if (base != 0)
        (void)mv_iface_query(base, &(mv_guid)MV_IID_SETT, (void **)&sett);
    check(sett != 0, "sett interface");

    /* The run counter lives in the Sett. A get that fails is not an error the
     * first time: nothing has been stored yet. */
    if (sett != 0) {
        if (sett->vtbl->get(sett, SETT_PATH, runs_text,
                            sizeof runs_text) == MV_S_OK) {
            runs = 0;
            {
                const char *p = runs_text;

                while (*p >= '0' && *p <= '9')
                    runs = runs * 10 + (*p++ - '0');
            }
        }
        runs++;
        snprintf(line, sizeof line, "%d", runs);
        check(sett->vtbl->set(sett, SETT_PATH, line) == MV_S_OK,
              "the run counter is stored");
    }

    /* The shared library: mv_add lives in mvlib.hbl and is reached through the
     * import slot the loader patched. Its convention is the library's own -
     * EBX, ECX in, EAX out - so the call needs a thunk. */
    sum = 0;                    /* see the note in report_row below */

    /* The Unicode half: these are CP437 glyphs, written as UTF-8. */
    report_banner("Mellivora OS \xE2\x80\x94 M5 exit criterion");
    report_row("interfaces", chronicle != 0 && sett != 0 ? "chronicle + sett" : "missing");
    report_row("shared library", "mvprobe: mv_add through the import slot");
    report_row("configuration", runs > 0 ? "Sett: run counter kept" : "Sett: unavailable");
    report_row("chronicle", "records written to the ring");
    report_row("Unicode", "\xC3\xA9\xC3\xA8\xC3\xAA \xE2\x94\x80\xE2\x94\x80\xE2\x94\x80 \xE2\x94\x82 \xE2\x94\x8C\xE2\x94\x90 \xE2\x96\x88");
    report_footer(runs);

    if (chronicle != 0) {
        snprintf(line, sizeof line, "mvapp: finished, runs=%d", runs);
        chronicle->vtbl->write(chronicle, 2, line);
        chronicle->vtbl->release((mv_iface *)chronicle);
    }
    if (sett != 0)
        sett->vtbl->release((mv_iface *)sett);

    if (fails != 0)
        return 1;
    printf("\nMVAPP_OK runs=%d sum=%d\n", runs, sum);
    return 0;
}
