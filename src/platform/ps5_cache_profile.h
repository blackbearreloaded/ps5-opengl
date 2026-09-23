/* Development-only cache publication attribution; no code in profile0 builds. */
#ifndef PS5_CACHE_PROFILE_H
#define PS5_CACHE_PROFILE_H
#ifdef PS5_DRAW_PROFILE
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

/* ponytail: source-line indexing avoids allocation/lookup in this diagnostic.
 * Increase the bound if a call site reaches it; never omit the actual flush. */
static struct { uint64_t calls, bytes, cycles; } ps5_cache_sites[16384];
static uint64_t ps5_cache_total;
static uint64_t ps5_cache_clock(void)
{
    uint32_t lo, hi;
    __asm__ volatile("lfence; rdtsc" : "=a"(lo), "=d"(hi) : : "memory");
    return (uint64_t)hi << 32 | lo;
}
static void ps5_cache_record(unsigned line, size_t bytes, uint64_t start)
{
    uint64_t cycles = ps5_cache_clock() - start;
    if (line >= sizeof(ps5_cache_sites) / sizeof(ps5_cache_sites[0]))
        line = 0;
    __atomic_fetch_add(&ps5_cache_sites[line].calls, 1, __ATOMIC_RELAXED);
    __atomic_fetch_add(&ps5_cache_sites[line].bytes, bytes, __ATOMIC_RELAXED);
    __atomic_fetch_add(&ps5_cache_sites[line].cycles, cycles, __ATOMIC_RELAXED);
    if ((__atomic_add_fetch(&ps5_cache_total, 1, __ATOMIC_RELAXED) & 0x3ffff) != 0)
        return;
    for (unsigned i = 0; i < sizeof(ps5_cache_sites) / sizeof(ps5_cache_sites[0]); ++i) {
        uint64_t calls = __atomic_load_n(&ps5_cache_sites[i].calls, __ATOMIC_RELAXED);
        if (calls)
            printf("[ps5-cache-site] file=%s line=%u calls=%" PRIu64
                   " bytes=%" PRIu64 " cycles=%" PRIu64 "\n", __BASE_FILE__, i, calls,
                   __atomic_load_n(&ps5_cache_sites[i].bytes, __ATOMIC_RELAXED),
                   __atomic_load_n(&ps5_cache_sites[i].cycles, __ATOMIC_RELAXED));
    }
}
#define PS5_CACHE_MEASURE(fn, address, bytes) do { \
    const void *cache_address = (address); \
    size_t cache_bytes = (bytes); \
    uint64_t cache_start = ps5_cache_clock(); \
    (fn)(cache_address, cache_bytes); \
    ps5_cache_record(__LINE__, cache_bytes, cache_start); \
} while (0)
#endif
#endif
