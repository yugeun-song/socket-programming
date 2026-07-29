#ifndef LOG_H
#define LOG_H

void log_write(const char *func, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void log_write_errno(const char *func, const char *what);

#define log_msg(...) log_write(__func__, __VA_ARGS__)
#define log_errno(what) log_write_errno(__func__, (what))

#endif