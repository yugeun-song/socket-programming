#define _GNU_SOURCE

#include "common/deadline.h"

#include <limits.h>
#include <poll.h>

#define MS_PER_SEC 1000
#define NS_PER_MS 1000000L
#define NS_PER_SEC 1000000000L

int deadline_start(struct deadline *dl, int timeout_ms)
{
    dl->timeout_ms = timeout_ms;
    dl->at.tv_sec = 0;
    dl->at.tv_nsec = 0;

    if (timeout_ms < 0) {
        return 0;
    }

    if (clock_gettime(CLOCK_MONOTONIC, &dl->at) < 0) {
        return -1;
    }

    dl->at.tv_sec += timeout_ms / MS_PER_SEC;
    dl->at.tv_nsec += (long)(timeout_ms % MS_PER_SEC) * NS_PER_MS;
    if (dl->at.tv_nsec >= NS_PER_SEC) {
        dl->at.tv_nsec -= NS_PER_SEC;
        dl->at.tv_sec += 1;
    }
    return 0;
}

int deadline_left_ms(const struct deadline *dl)
{
    struct timespec now;
    long long left_ns;
    long long left_ms;

    if (dl->timeout_ms < 0) {
        return DEADLINE_FOREVER;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        return 0;
    }

    left_ns = (long long)(dl->at.tv_sec - now.tv_sec) * NS_PER_SEC;
    left_ns += (long long)dl->at.tv_nsec - (long long)now.tv_nsec;

    if (left_ns <= 0) {
        return 0;
    }

    left_ms = (left_ns + NS_PER_MS - 1) / NS_PER_MS;
    if (left_ms > INT_MAX) {
        return INT_MAX;
    }
    return (int)left_ms;
}

int deadline_expired(const struct deadline *dl)
{
    return deadline_left_ms(dl) == 0;
}

int poll_until(int fd, short events, const struct deadline *dl, const sigset_t *mask)
{
    struct pollfd pfd = { 0 };
    struct timespec ts;
    int left;

    pfd.fd = fd;
    pfd.events = events;

    left = deadline_left_ms(dl);
    if (left == DEADLINE_FOREVER) {
        return ppoll(&pfd, 1, NULL, mask);
    }

    ts.tv_sec = left / MS_PER_SEC;
    ts.tv_nsec = (long)(left % MS_PER_SEC) * NS_PER_MS;
    return ppoll(&pfd, 1, &ts, mask);
}