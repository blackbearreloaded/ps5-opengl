// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

// Shared bounded wall-clock pacing and nearest-rank statistics; not GPU timers.
#include <cmath>
#include <cerrno>
#include <time.h>
#include <unistd.h>

static double bench_seconds()
{
    timespec now = {};
    return clock_gettime(CLOCK_MONOTONIC, &now) == 0
        ? now.tv_sec + now.tv_nsec * 1e-9 : -1.0;
}

static bool bench_wait(double deadline)
{
    double now = bench_seconds();
    unsigned attempts = 0;
    while (now >= 0 && now < deadline) {
        const double remaining = deadline - now;
        if (remaining > 1.0 || ++attempts > 16) return false;
        if (usleep(static_cast<unsigned>(std::ceil(remaining * 1e6))) != 0 && errno != EINTR)
            return false;
        const double next = bench_seconds();
        if (next < now) return false;
        now = next;
    }
    return now >= 0;
}

static int bench_compare(const void* a, const void* b)
{
    const double x = *static_cast<const double*>(a), y = *static_cast<const double*>(b);
    return (x > y) - (x < y);
}

static double bench_percentile(const double* sorted, unsigned count, unsigned percent)
{
    return sorted[(static_cast<size_t>(count) * percent + 99) / 100 - 1];
}
