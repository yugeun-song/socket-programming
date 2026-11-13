#ifndef COMMON_LOG_H
#define COMMON_LOG_H

void log_write_helper(const char *const func,
                      const char *const fmt, ...) __attribute__((format(printf, 2, 3)));
void log_write_errno_helper(const char *const func, const char *const failed_call);
void log_write_result_helper(const char *const fmt, ...) __attribute__((format(printf, 1, 2)));

#define LOG_MSG_HELPER(...) log_write_helper(__func__, __VA_ARGS__)
#define LOG_ERRNO_HELPER(failed_call) log_write_errno_helper(__func__, (failed_call))
#define LOG_RESULT_HELPER(...) log_write_result_helper(__VA_ARGS__)

#endif /* COMMON_LOG_H */
