#define _GNU_SOURCE

#include "common/log.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define LOG_LINE_MAX_HELPER 512

void log_write_helper(const char *const func, const char *const fmt, ...)
{
    char body[LOG_LINE_MAX_HELPER];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    fprintf(stderr, "%s: %s(): %s\n", program_invocation_short_name, func, body);
}

void log_write_errno_helper(const char *const func, const char *const failed_call)
{
    char error_buf[LOG_LINE_MAX_HELPER];
    int saved_errno = errno;
    const char *const error_text = strerror_r(saved_errno, error_buf, sizeof(error_buf));

    fprintf(stderr, "%s: %s(): %s: %s\n", program_invocation_short_name, func, failed_call,
            error_text);

    errno = saved_errno;
}

void log_write_result_helper(const char *const fmt, ...)
{
    char body[LOG_LINE_MAX_HELPER];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    fprintf(stdout, "%s: %s\n", program_invocation_short_name, body);
    fflush(stdout);
}
