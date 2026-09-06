#!/usr/bin/env python3
"""Run CTS's real output helpers with an opaque-FILE-safe stdio boundary."""
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
source = (ROOT / "third_party/VK-GL-CTS/framework/qphelper/qpDebugOut.c").read_text()
types = source[source.index("typedef enum MessageType_e"):source.index("static void printRaw")]
start = source.index("static FILE *getOutFile")
helpers = source[start:source.index("\n#endif\n\n/* exitProcess()", start)]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
typedef bool (*writePtr)(int, const char *);
typedef bool (*writeFtmPtr)(int, const char *, va_list);
static FILE *out_stream, *error_stream;
static unsigned inline_file_accesses;
#undef stdout
#undef stderr
#define stdout out_stream
#define stderr error_stream
/* Model the SDK's FILE-layout-dependent putc macro, without a real bad write. */
#undef putc
int layout_dependent_putc(int c, FILE *stream) {
    (void)c; (void)stream;
    ++inline_file_accesses;
    return EOF;
}
#define putc(c, stream) layout_dependent_putc(c, stream)
''' + types + helpers + r'''
static void formatted(MessageType type, const char *format, ...) {
    va_list args;
    va_start(args, format);
    printFmt(type, format, args);
    va_end(args);
}
static bool redirect_raw(int type, const char *message) {
    assert(type == MESSAGETYPE_ERROR && !strcmp(message, "redirected"));
    return false;
}
static bool redirect_fmt(int type, const char *format, va_list args) {
    assert(type == MESSAGETYPE_ERROR && !strcmp(format, "%d"));
    assert(va_arg(args, int) == 42);
    return false;
}
static void expect(FILE *stream, const char *expected) {
    char text[128] = {0};
    rewind(stream);
    assert(fread(text, 1, sizeof(text) - 1, stream) == strlen(expected));
    assert(!strcmp(text, expected));
}
int main(void) {
    out_stream = tmpfile();
    error_stream = tmpfile();
    assert(out_stream && error_stream);
    printRaw(MESSAGETYPE_INFO, "info ");
    formatted(MESSAGETYPE_INFO, "%d", 7);
    printRaw(MESSAGETYPE_NONFATAL_ERROR, "warning ");
    formatted(MESSAGETYPE_NONFATAL_ERROR, "%d ", 8);
    printRaw(MESSAGETYPE_ERROR, "raw failure");
    formatted(MESSAGETYPE_ERROR, "formatted failure %d", 9);
    writeRedirect = redirect_raw;
    writeFtmRedirect = redirect_fmt;
    printRaw(MESSAGETYPE_ERROR, "redirected");
    formatted(MESSAGETYPE_ERROR, "%d", 42);
    assert(inline_file_accesses == 0);
    expect(out_stream, "info 7");
    expect(error_stream, "warning 8 FATAL ERROR: raw failure\n"
                         "FATAL ERROR: formatted failure 9\n");
    fclose(out_stream);
    fclose(error_stream);
}
'''
with tempfile.TemporaryDirectory() as temporary:
    executable = str(Path(temporary) / "qp-debug-output")
    subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-x", "c", "-o", executable, "-"],
                   input=code, text=True, check=True)
    subprocess.run([executable], check=True)
print("PASS: CTS raw/formatted diagnostics, fatal newline, redirects; no FILE-layout access")
