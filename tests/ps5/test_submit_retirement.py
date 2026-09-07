#!/usr/bin/env python3
"""Host-only failure injection into the real synchronous submit/cleanup path."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "src/platform/ps5_agc_native_runtime.c").read_text()
start = source.find("static void runtime_require_retirement(")
guard = source[start:source.index("\n}\n", start) + 3] if start >= 0 else ""
start = source.index("        int submit_rc = agc.submit(&submit);")
submit = source[start:source.index("#ifdef PS5_DRAW_BATCH_PROBE\n        batch_wait_ns", start)]
start = source.index("    if (memory)\n        work_unmap_rc")
cleanup = source[start:source.index("#if defined(AGC_RUNTIME_PACKAGES)", start)]
return_code = source[source.rindex("    return "):source.rindex("\n}")]
code = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#define AGC_RUNTIME_PACKAGES 1
#define PS5_NATIVE_TITLE_RUNTIME 1
#define PS5_PROFILE_MARK(i) ((void)0)
static struct counters { unsigned submits, suspends, waits, unmaps, releases, destructors; } *seen;
static volatile uint32_t marker;
static int submit_error, suspend_error, unmap_error, release_error;
static unsigned delay;
static int send(void *p) {
    assert(p && !seen->unmaps && !seen->releases); ++seen->submits;
    if (!delay) marker = 101;
    return submit_error;
}
static int suspend_point(void) { ++seen->suspends; return suspend_error; }
static void flush_gpu_data(const void *p, size_t n) { assert(p == &marker && n == 4); }
static int sceKernelUsleep(uint32_t us) {
    assert(us == 1000 && !seen->unmaps && !seen->releases);
    if (++seen->waits >= delay) marker = 101;
    return 0;
}
static int unmap(void *p, size_t n) { assert(p && n == 64); ++seen->unmaps; return unmap_error; }
static int sceKernelReleaseDirectMemory(int64_t p, size_t n) {
    assert(p == 128 && n == 64 && !unmap_error); ++seen->releases; return release_error;
}
static void destructor(void) { ++seen->destructors; }
''' + guard + r'''
static int draw(void) {
    const struct { int (*submit)(void *); int (*suspend_point)(void); } agc = {send, suspend_point};
    int submit = 1, result = 0, work_unmap_rc = 0, work_release_rc = 0;
    void *memory = &submit;
    size_t work_bytes = 64;
    int64_t work_start = 128, render_marker = 101;
    volatile uint32_t *completion_marker = &marker;
    unsigned waits;
    uint64_t status[16] = {0};
''' + submit + r'''
#endif /* AGC_RUNTIME_PACKAGES: the non-runtime flip-probe branch is not used. */
    (void)status;
    result = submit_rc != 0 || suspend_rc != 0 || waits == 2000;
#define munmap unmap
''' + cleanup + r'''
#undef munmap
''' + return_code + r'''
}
int main(void) {
    seen = mmap(NULL, sizeof(*seen), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    assert(seen != MAP_FAILED);
    for (unsigned scenario = 0; scenario < 8; ++scenario) {
        *seen = (struct counters){0}; marker = 0;
        submit_error = scenario == 3 ? -1 : 0;
        suspend_error = scenario == 4 ? -1 : 0;
        unmap_error = scenario == 6 ? -1 : 0;
        release_error = scenario == 7 ? -1 : 0;
        delay = scenario == 1 ? 1 : scenario == 2 ? 1999 : scenario == 5 ? 2000 : 0;
        const int fatal = scenario >= 3 && scenario <= 5;
        pid_t child = fork(); assert(child >= 0);
        if (!child) {
            assert(atexit(destructor) == 0);
            int rc = draw();
            assert(!fatal); /* Unconfirmed GPU work must never return to callers. */
            assert((rc != 0) == (scenario >= 6));
            _Exit(0);
        }
        int status;
        assert(waitpid(child, &status, 0) == child && WIFEXITED(status));
        assert(WEXITSTATUS(status) == (fatal ? EXIT_FAILURE : 0));
        assert(seen->submits == 1 && seen->suspends == !submit_error);
        assert(seen->unmaps == !fatal && seen->releases == (!fatal && !unmap_error));
        assert(!seen->destructors && seen->waits <= 2000);
    }
    assert(munmap(seen, sizeof(*seen)) == 0);
    puts("submit-retirement: PASS success/boundary; submit/suspend/timeout stop before cleanup or atexit; unmap failure retains direct memory");
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "submit-retirement")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-x", "c",
                    "-o", executable, "-"], input=code, text=True, check=True)
    subprocess.run([executable], cwd=temporary, check=True)
