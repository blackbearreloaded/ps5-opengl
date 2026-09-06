#include "glcPS5GL33PackageEntry.hpp"

#include "deUniquePtr.hpp"
#include "tcuApp.hpp"
#include "tcuCommandLine.hpp"
#include "tcuPlatform.hpp"
#include "tcuResource.hpp"
#include "tcuTestLog.hpp"
#include "tcuTestPackage.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <exception>

extern "C" int sceKernelDebugOutText(int channel, const char *text);
struct LibcMallocManagedSize {
  uint16_t size;
  uint16_t version;
  uint32_t reserved;
  size_t maxSystemSize;
  size_t currentSystemSize;
  size_t maxInuseSize;
  size_t currentInuseSize;
};
extern "C" void malloc_stats_fast(LibcMallocManagedSize *stats);
extern "C" void psbc_glsl_type_cache_print_stats(unsigned iteration);
extern "C" void pss_opengl_heap_stats_print(unsigned iteration);

tcu::Platform *createPlatform(void);

namespace {

constexpr const char *kArgumentsPath = "/app0/cts-args.txt";
constexpr const char *kStatusPath = "/download0/pss-opengl-cts.status";
constexpr int kMaxArguments = 64;
constexpr int kMaxArgumentLength = 512;

void printHeapStats(unsigned iteration) {
  pss_opengl_heap_stats_print(iteration);
  if (iteration == 14) {
    LibcMallocManagedSize stats = {sizeof(stats), 1, 0, 0, 0, 0, 0};
    malloc_stats_fast(&stats);
    std::printf("[pss-opengl-cts] libc-heap max-system=%zu current-system=%zu "
                "max-inuse=%zu current-inuse=%zu\n",
                stats.maxSystemSize, stats.currentSystemSize,
                stats.maxInuseSize, stats.currentInuseSize);
  }
  psbc_glsl_type_cache_print_stats(iteration);
  using Mallctl = int (*)(const char *, void *, size_t *, void *, size_t);
  static Mallctl mallctl =
      reinterpret_cast<Mallctl>(dlsym(RTLD_DEFAULT, "mallctl"));
  if (mallctl == nullptr)
    return;

  uint64_t epoch = 1;
  size_t epochSize = sizeof(epoch);
  size_t allocated = 0;
  size_t allocatedSize = sizeof(allocated);
  if (mallctl("epoch", &epoch, &epochSize, &epoch, sizeof(epoch)) == 0 &&
      mallctl("stats.allocated", &allocated, &allocatedSize, nullptr, 0) == 0)
    std::printf("[pss-opengl-cts] heap iteration=%u allocated=%zu\n",
                iteration, allocated);
}

struct Arguments {
  int count = 1;
  char storage[kMaxArguments - 1][kMaxArgumentLength] = {};
  const char *values[kMaxArguments + 1] = {"ps5-gl33-cts"};
};

void writeStatus(const char *state, const tcu::TestRunStatus *result) {
  FILE *file = std::fopen(kStatusPath, "w");
  if (file == nullptr)
    return;

  if (result == nullptr)
    std::fprintf(file, "state=%s\n", state);
  else
    std::fprintf(file,
                 "state=%s complete=%d executed=%d passed=%d failed=%d "
                 "not_supported=%d warnings=%d waived=%d device_lost=%d\n",
                 state, result->isComplete ? 1 : 0, result->numExecuted,
                 result->numPassed, result->numFailed,
                 result->numNotSupported, result->numWarnings,
                 result->numWaived, result->numDeviceLost);
  std::fclose(file);
}

int finish(const char *state, int exitCode,
           const tcu::TestRunStatus *result = nullptr) {
  writeStatus(state, result);
  std::printf("[pss-opengl-cts] finished state=%s", state);
  if (result != nullptr)
    std::printf(" executed=%d failed=%d device_lost=%d", result->numExecuted,
                result->numFailed, result->numDeviceLost);
  std::printf("\n");
  std::fflush(stdout);
  sceKernelDebugOutText(0, "[pss-opengl-cts] finished\n");
  return exitCode;
}

bool startsWith(const char *value, const char *prefix) {
  return std::strncmp(value, prefix, std::strlen(prefix)) == 0;
}

bool loadArguments(Arguments &arguments) {
  FILE *file = std::fopen(kArgumentsPath, "r");
  if (file == nullptr) {
    std::fprintf(stderr, "missing CTS argument file: %s\n", kArgumentsPath);
    return false;
  }

  bool hasCaseSelection = false;
  char line[kMaxArgumentLength];
  while (std::fgets(line, sizeof(line), file) != nullptr) {
    if (std::strchr(line, '\n') == nullptr && !std::feof(file)) {
      std::fprintf(stderr, "overlong CTS argument\n");
      std::fclose(file);
      return false;
    }

    char *argument = line;
    while (*argument == ' ' || *argument == '\t')
      ++argument;

    char *end = argument + std::strlen(argument);
    while (end != argument && (end[-1] == '\r' || end[-1] == '\n' ||
                               end[-1] == ' ' || end[-1] == '\t'))
      *--end = '\0';

    if (*argument == '\0' || *argument == '#')
      continue;
    if (!startsWith(argument, "--deqp-")) {
      std::fprintf(stderr, "invalid CTS argument: %s\n", argument);
      std::fclose(file);
      return false;
    }
    if (std::strcmp(argument, "--deqp-watchdog=enable") == 0 ||
        std::strcmp(argument, "--deqp-crashhandler=enable") == 0) {
      std::fprintf(stderr, "unsafe CTS runtime option: %s\n", argument);
      std::fclose(file);
      return false;
    }
    if (arguments.count >= kMaxArguments) {
      std::fprintf(stderr, "too many CTS arguments\n");
      std::fclose(file);
      return false;
    }

    hasCaseSelection |= startsWith(argument, "--deqp-case=") ||
                        startsWith(argument, "--deqp-caselist=") ||
                        startsWith(argument, "--deqp-caselist-file=");
    char *stored = arguments.storage[arguments.count - 1];
    std::strcpy(stored, argument);
    arguments.values[arguments.count++] = stored;
  }

  std::fclose(file);
  arguments.values[arguments.count] = nullptr;

  if (!hasCaseSelection)
    std::fprintf(stderr,
                 "CTS run requires an explicit bounded case selection\n");
  return hasCaseSelection;
}

} // anonymous namespace

int main(void) {
  writeStatus("starting", nullptr);
  std::printf("[pss-opengl-cts] starting GL33 CTS runner\n");
  // Negative tests intentionally raise millions of GL errors. Suppress only
  // Mesa's stderr duplicates; glGetError, debug callbacks and QPA stay enabled.
  if (setenv("MESA_DEBUG", "silent", 1) != 0)
    return finish("logging_setup_error", 2);

  try {
    Arguments arguments;
    if (!loadArguments(arguments)) {
      return finish("argument_error", 2);
    }

    for (int index = 1; index < arguments.count; ++index)
      std::printf("[pss-opengl-cts] arg %s\n", arguments.values[index]);

    glctsRegisterPS5GL33Package();
    tcu::CommandLine commandLine(arguments.count, arguments.values);
    tcu::DirArchive archive(commandLine.getArchiveDir());
    tcu::TestLog log(commandLine.getLogFileName(), commandLine.getLogFlags());
    de::UniquePtr<tcu::Platform> platform(createPlatform());
    tcu::App app(*platform, archive, log, commandLine);

    unsigned iteration = 0;
    while (app.iterate())
      printHeapStats(++iteration);

    const tcu::TestRunStatus result = app.getResult();
    // Match the upstream tcuMain acceptance rule. The GL must-pass list also
    // contains optional-extension cases, for which NotSupported is valid.
    const bool passed = result.isComplete && result.numExecuted > 0 &&
                        result.numFailed == 0 && result.numDeviceLost == 0;
    return finish(passed ? "passed" : "failed", passed ? 0 : 1, &result);
  } catch (const std::exception &error) {
    std::fprintf(stderr, "[pss-opengl-cts] fatal: %s\n", error.what());
    return finish("fatal", 3);
  }
}
