#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "error.h"

static const char *g_filename = "<unknown>";

void jibl_error_init(const char *filename) {
    g_filename = filename;
}

void jibl_error(int line, const char *fmt, ...) {
    fprintf(stderr, "%s:%d: error: ", g_filename, line);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
    exit(1);
}
