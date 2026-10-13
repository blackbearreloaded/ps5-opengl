#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Statistics printed while drawing are opt-in; the reports at teardown are not."""
import os
from pathlib import Path
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
screen = (root / "src/gallium/ps5/ps5_screen.c").read_text()
runtime = (root / "src/platform/ps5_agc_native_runtime.c").read_text()
cache = (root / "src/platform/ps5_cache_profile.h").read_text()
SWITCH = "PS5_DRAW_PROFILE_LOG"


def build(code, exe):
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(root),
                    "-x", "c", "-", "-o", str(exe)], input=code, text=True, check=True)


def run(exe, value):
    """Standard output and standard error with the switch unset (None) or set."""
    env = {name: text for name, text in os.environ.items() if name != SWITCH}
    if value is not None:
        env[SWITCH] = value
    done = subprocess.run([str(exe)], env=env, text=True, capture_output=True, check=True)
    return done.stdout, done.stderr


with tempfile.TemporaryDirectory() as tmp:
    exe = Path(tmp) / "switch"
    # Only "1" turns the reports on, and the environment is read once.
    build(r'''
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include "src/platform/ps5_profile_log.h"
int main(void) {
    int first = ps5_profile_log();
    setenv("PS5_DRAW_PROFILE_LOG", first ? "0" : "1", 1);
    printf("%d %d\n", first, (int)ps5_profile_log());
}
''', exe)
    for value, expected in ((None, "0 0\n"), ("1", "1 1\n"), ("0", "0 0\n"), ("", "0 0\n"),
                            ("11", "0 0\n"), ("true", "0 0\n")):
        assert run(exe, value)[0] == expected, (value, expected)
    print("PASS: the periodic reports are off unless PS5_DRAW_PROFILE_LOG=1, read once")

    # The driver's phase timer: 10,000 draws end a period. The consumer hook still runs;
    # the report is printed only when asked for, and the report at teardown always.
    start = screen.index("static uint64_t ps5_prepare_cycles[9]")
    phases = screen[start:screen.index("\n#endif\n", screen.index("static void ps5_prepare_scope_end", start))]
    build(r'''
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include "src/platform/ps5_profile_log.h"
#define PIPE_MAP_READ 1u
#define PIPE_BUFFER 0u
static unsigned registered;
static void ps5_opengl_register_gl_consumer(void) { ++registered; }
''' + phases + r'''
int main(void) {
    ps5_map_caller_record(0x1234, PIPE_MAP_READ, PIPE_BUFFER);
    for (unsigned i = 0; i < 20000; ++i) {
        struct ps5_prepare_scope scope = {0, ps5_prepare_clock()};
        ps5_prepare_scope_end(&scope);
    }
    fprintf(stderr, "registered=%u\n", registered);
    printf("teardown\n");
    ps5_prepare_report();
}
''', exe)
    report = 9 + 1 + 1  # nine phases, the anchor, one map caller
    for value, periodic in ((None, 0), ("0", 0), ("1", 2)):
        out, err = run(exe, value)
        during, final = out.split("teardown\n")
        assert err == "registered=2\n", err
        assert during.count("[ps5-driver-elapsed] calls=") == periodic
        assert len(during.splitlines()) == periodic * (1 + report), (value, during)
        assert len(final.splitlines()) == report and final.count("[ps5-driver-cycles] phase=0 calls=20000 ") == 1
    print("PASS: the draw phase report is printed every 10,000 draws only when asked for, and at teardown always")

    # The cache-flush attribution reports every 262,144 flushes.
    build(r'''
#include <assert.h>
#define PS5_DRAW_PROFILE 1
#include "src/platform/ps5_cache_profile.h"
int main(void) {
    for (unsigned i = 0; i < 0x40000; ++i)
        ps5_cache_record(7, 8, ps5_cache_clock());
    assert(ps5_cache_total == 0x40000 && ps5_cache_sites[7].calls == 0x40000);
    assert(ps5_cache_sites[7].bytes == 8 * UINT64_C(0x40000));
}
''', exe)
    assert run(exe, None)[0] == ""
    out = run(exe, "1")[0]
    assert out.count("\n") == 1 and " line=7 calls=262144 bytes=2097152 " in out, out
    print("PASS: cache-flush attribution counts always and prints only when asked for")

# Every count that ends a period asks the switch before it prints.
for name, source in (("ps5_screen.c", screen), ("ps5_agc_native_runtime.c", runtime), ("ps5_cache_profile.h", cache)):
    for match in re.finditer(r"%\s*10{3,}u?\b|&\s*0x3ffff\b|>=\s*10000u\b", source):
        line = source.count("\n", 0, match.start()) + 1
        following = "\n".join(source[match.start():].splitlines()[:4])
        assert "ps5_profile_log()" in following, f"{name}:{line} prints a periodic report without asking"
for needle in ("   if (context->draw_calls % 10000u == 0 && ps5_profile_log())\n      ps5_context_profile_report(context);\n",
               "   if (++context->batch_checks % 100000 == 0 && ps5_profile_log())\n      printf(\"[ps5-batch-checks]"):
    assert screen.count(needle) == 1, needle
for needle in ("    if (++runtime_pair_lookups % 100000 == 0 && ps5_profile_log())\n        printf(\"[ps5-pair-cache]",
               "        if ((consumed + 1) % 100000 == 0 && ps5_profile_log())\n            printf(\"[ps5-async-elapsed]",
               "    if (runtime_prepare_profile_calls + runtime_profile_calls >= 10000u)\n        runtime_profile_report(ps5_profile_log());\n"):
    assert runtime.count(needle) == 1, needle
# Timing lines of single clears are statistics too.
assert screen.count('printf("[ps5-clear-cpu] ') == 2
for match in re.finditer(r'printf\("\[ps5-clear-cpu\] ', screen):
    assert screen[:match.start()].rstrip().endswith("if (ps5_profile_log())"), "a clear timing line is printed without asking"

# Teardown keeps its reports: the context's summary, and the runtime's last period.
destroy = screen[screen.index("\nps5_context_destroy(struct pipe_context *base)"):]
destroy = destroy[:destroy.index("\n}\n")]
assert destroy.count("\n   ps5_context_profile_report(context);\n") == 1
assert runtime.count("#ifdef PS5_DRAW_PROFILE\n    runtime_profile_report(true);\n#endif\n") == 1
assert len(re.findall(r"\bruntime_profile_report\(", runtime)) == 3  # the definition and the two calls above
print("PASS: every periodic report asks first; teardown reports stay unconditional")
