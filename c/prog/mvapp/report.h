/*
 * report.h - the second source file of the M5 exit-criterion application
 *
 * The application exists to prove M5's exit criterion, and one part of that is
 * being *multi-source*: two translation units and the library, not one file
 * that happens to be big. This is the unit that formats the report.
 */
#ifndef MVAPP_REPORT_H
#define MVAPP_REPORT_H

#include <mv/iface.h>

void report_banner(const char *title);
void report_row(const char *key, const char *value);
void report_footer(int runs);

#endif /* MVAPP_REPORT_H */
