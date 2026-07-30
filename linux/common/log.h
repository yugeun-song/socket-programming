#ifndef LOG_H
#define LOG_H

void log_write(const char *func, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void log_write_errno(const char *func, const char *what);

#define LOG_MSG(...) log_write(__func__, __VA_ARGS__)
#define LOG_ERRNO(what) log_write_errno(__func__, (what))

#endif