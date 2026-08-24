#ifndef LOG_H_HELPER
#define LOG_H_HELPER

void log_write_helper(const char *func, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void log_write_errno_helper(const char *func, const char *failed_call);
void log_write_result_helper(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#define LOG_MSG_HELPER(...) log_write_helper(__func__, __VA_ARGS__)
#define LOG_ERRNO_HELPER(failed_call) log_write_errno_helper(__func__, (failed_call))
#define LOG_RESULT_HELPER(...) log_write_result_helper(__VA_ARGS__)

#endif