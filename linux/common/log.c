#include "common/log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void log_write(const char *func, const char *fmt, ...)
{
    va_list ap;

    fprintf(stderr, "%s(): ", func);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    fflush(stderr);
}

void log_write_errno(const char *func, const char *what)
{
    int saved = errno;

    fprintf(stderr, "%s(): %s: %s\n", func, what, strerror(saved));
    fflush(stderr);
}
