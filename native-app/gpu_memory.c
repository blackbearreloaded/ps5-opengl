// PS5 OpenGL - OpenGL implementation for PlayStation 5.
// Copyright (C) 2026 BlackBearReloaded
// SPDX-License-Identifier: GPL-3.0-or-later

/* Optional native-test telemetry. Preserve allocator results. Module-internal
 * allocations and CPU mmap are not covered. Diagnostic calls are serialized so
 * another thread cannot reuse an address before its release is recorded. */
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>

int32_t __real_sceKernelAllocateDirectMemory(int64_t, int64_t, size_t, size_t, int, int64_t *);
int32_t __real_sceKernelMapDirectMemory(void **, size_t, int, int, int64_t, size_t);
int32_t __real_sceKernelReleaseDirectMemory(int64_t, size_t);
int __real_munmap(void *, size_t);

enum { DIRECT = 1, MAPPING = 2 };
/* ponytail: bounded diagnostic table, not an allocator. Overflow invalidates
 * evidence; increase this bound only for a measured diagnostic workload. */
static struct { uint64_t key; size_t bytes; unsigned kind; } entries[4096];
static struct { size_t bytes, peak, count; } totals[3];
static size_t failures, invalid;
static atomic_flag guard = ATOMIC_FLAG_INIT;

static void lock(void) {
    while (atomic_flag_test_and_set_explicit(&guard, memory_order_acquire)) {}
}
static void unlock(void) {
    atomic_flag_clear_explicit(&guard, memory_order_release);
}
static bool overlaps(uint64_t a, size_t an, uint64_t b, size_t bn) {
    return a <= b ? b - a < an : a - b < bn;
}

static void record(unsigned kind, uint64_t key, size_t bytes, bool add, int status) {
    const unsigned count = sizeof(entries) / sizeof(entries[0]);
    unsigned exact = count, empty = count;
    bool overlap = false;
    for (unsigned i = 0; i < count; ++i) {
        if (!entries[i].kind && empty == count) empty = i;
        if (entries[i].kind != kind) continue;
        if (entries[i].key == key && entries[i].bytes == bytes) exact = i;
        overlap |= overlaps(key, bytes, entries[i].key, entries[i].bytes);
    }
    if (status) {
        if (add || kind == DIRECT || overlap) ++failures;
    } else if (!add && exact != count) {
        totals[kind].bytes -= entries[exact].bytes;
        --totals[kind].count;
        entries[exact].kind = 0;
    } else if (add) {
        if (!bytes || bytes > UINT64_MAX - key || overlap || empty == count ||
            bytes > SIZE_MAX - totals[kind].bytes) {
            ++invalid;
        } else {
            entries[empty].key = key;
            entries[empty].bytes = bytes;
            entries[empty].kind = kind;
            totals[kind].bytes += bytes;
            ++totals[kind].count;
            if (totals[kind].bytes > totals[kind].peak)
                totals[kind].peak = totals[kind].bytes;
        }
    } else if (kind == DIRECT || overlap) {
        /* Partial/untracked direct releases cannot be called balanced. Ignore
         * unrelated CPU munmap, but flag partial unmaps of a tracked mapping. */
        ++invalid;
    }
}

int32_t __wrap_sceKernelAllocateDirectMemory(int64_t first, int64_t last,
    size_t bytes, size_t alignment, int type, int64_t *physical) {
    lock();
    int32_t result = __real_sceKernelAllocateDirectMemory(
        first, last, bytes, alignment, type, physical);
    record(DIRECT, result == 0 && physical ? (uint64_t)*physical : UINT64_MAX,
           bytes, true, result);
    unlock();
    return result;
}
int32_t __wrap_sceKernelMapDirectMemory(void **address, size_t bytes, int protection,
    int flags, int64_t physical, size_t alignment) {
    lock();
    int32_t result = __real_sceKernelMapDirectMemory(
        address, bytes, protection, flags, physical, alignment);
    record(MAPPING, result == 0 && address && *address && *address != MAP_FAILED
        ? (uintptr_t)*address : UINT64_MAX, bytes, true, result);
    unlock();
    return result;
}
int32_t __wrap_sceKernelReleaseDirectMemory(int64_t physical, size_t bytes) {
    lock();
    int32_t result = __real_sceKernelReleaseDirectMemory(physical, bytes);
    record(DIRECT, (uint64_t)physical, bytes, false, result);
    unlock();
    return result;
}
int __wrap_munmap(void *address, size_t bytes) {
    lock();
    int result = __real_munmap(address, bytes);
    record(MAPPING, (uintptr_t)address, bytes, false, result);
    unlock();
    return result;
}

void pss_opengl_gpu_snapshot(const char *phase, unsigned sample) {
    size_t direct, direct_peak, allocations, mapped, mapped_peak, mappings, failed, inconclusive;
    lock();
    direct = totals[DIRECT].bytes; direct_peak = totals[DIRECT].peak;
    allocations = totals[DIRECT].count;
    mapped = totals[MAPPING].bytes; mapped_peak = totals[MAPPING].peak;
    mappings = totals[MAPPING].count; failed = failures; inconclusive = invalid;
    unlock();
    printf("[pss-opengl-gpu-memory] phase=%s sample=%u direct_bytes=%zu direct_peak=%zu "
           "allocations=%zu mapped_bytes=%zu mapped_peak=%zu mappings=%zu failures=%zu invalid=%zu\n",
           phase, sample, direct, direct_peak, allocations, mapped, mapped_peak, mappings,
           failed, inconclusive);
}
