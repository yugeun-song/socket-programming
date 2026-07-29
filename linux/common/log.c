#define _GNU_SOURCE

#include "common/log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LOG_LINE_MAX 512

void log_write(const char *func, const char *fmt, ...)
{
    char body[LOG_LINE_MAX];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    fprintf(stderr, "%s: %s(): %s\n", program_invocation_short_name, func, body);
}

void log_write_errno(const char *func, const char *what)
{
    char buf[LOG_LINE_MAX];
    int saved_errno = errno;
    const char *text;

    text = strerror_r(saved_errno, buf, sizeof(buf));
    fprintf(stderr, "%s: %s(): %s: %s\n", program_invocation_short_name, func, what, text);

    errno = saved_errno;
}