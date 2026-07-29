#ifndef DEADLINE_H
#define DEADLINE_H

#include <signal.h>
#include <time.h>

#define DEADLINE_FOREVER (-1)

struct deadline {
    struct timespec at;
    int timeout_ms;
};

int deadline_start(struct deadline *dl, int timeout_ms);
int deadline_left_ms(const struct deadline *dl);
int deadline_expired(const struct deadline *dl);

int poll_until(int fd, short events, const struct deadline *dl, const sigset_t *mask);

#endif