#include <_ctype.h>
#include <errno.h>
#include <execinfo.h>
#include <locale.h>
#include <nl_types.h>
#include <runetype.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <wchar.h>
#include <wctype.h>
#include <xlocale.h>

static _RuneLocale pss_cts_runes;
static int pss_cts_runes_ready;

static void pss_cts_init_runes(void) {
  if (pss_cts_runes_ready)
    return;

  memcpy(pss_cts_runes.__magic, _RUNE_MAGIC_1, 8);
  memcpy(pss_cts_runes.__encoding, "NONE", 5);
  for (int value = 0; value < _CACHED_RUNES; ++value) {
    pss_cts_runes.__maplower[value] = value;
    pss_cts_runes.__mapupper[value] = value;
    if (value < 32 || value == 127)
      pss_cts_runes.__runetype[value] = _CTYPE_C;
    else if (value >= 33 && value <= 126)
      pss_cts_runes.__runetype[value] =
          _CTYPE_G | _CTYPE_R | _CTYPE_P | _CTYPE_SW1;
  }

  pss_cts_runes.__runetype[' '] = _CTYPE_S | _CTYPE_B | _CTYPE_R | _CTYPE_SW1;
  pss_cts_runes.__runetype['\t'] |= _CTYPE_S | _CTYPE_B;
  for (int value = '\n'; value <= '\r'; ++value)
    pss_cts_runes.__runetype[value] |= _CTYPE_S;

  for (int value = '0'; value <= '9'; ++value)
    pss_cts_runes.__runetype[value] = _CTYPE_D | _CTYPE_N | _CTYPE_G |
                                      _CTYPE_R | _CTYPE_SW1 |
                                      (unsigned long)(value - '0');
  for (int value = 'A'; value <= 'Z'; ++value) {
    pss_cts_runes.__runetype[value] =
        _CTYPE_A | _CTYPE_U | _CTYPE_G | _CTYPE_R | _CTYPE_SW1;
    pss_cts_runes.__maplower[value] = value - 'A' + 'a';
  }
  for (int value = 'a'; value <= 'z'; ++value) {
    pss_cts_runes.__runetype[value] =
        _CTYPE_A | _CTYPE_L | _CTYPE_G | _CTYPE_R | _CTYPE_SW1;
    pss_cts_runes.__mapupper[value] = value - 'a' + 'A';
  }
  for (int value = 'A'; value <= 'F'; ++value)
    pss_cts_runes.__runetype[value] |= _CTYPE_X;
  for (int value = 'a'; value <= 'f'; ++value)
    pss_cts_runes.__runetype[value] |= _CTYPE_X;
  for (int value = '0'; value <= '9'; ++value)
    pss_cts_runes.__runetype[value] |= _CTYPE_X;

  pss_cts_runes_ready = 1;
}

_RuneLocale *__runes_for_locale(locale_t locale, int *limit) {
  (void)locale;
  pss_cts_init_runes();
  if (limit != NULL)
    *limit = _CACHED_RUNES;
  return &pss_cts_runes;
}

locale_t newlocale(int mask, const char *name, locale_t base) {
  static int c_locale;
  (void)mask;
  (void)name;
  return base != NULL && base != LC_GLOBAL_LOCALE ? base : (locale_t)&c_locale;
}

int freelocale(locale_t locale) {
  (void)locale;
  return 0;
}

int strcoll_l(const char *left, const char *right, locale_t locale) {
  (void)locale;
  return strcoll(left, right);
}

size_t strxfrm_l(char *output, const char *input, size_t size,
                 locale_t locale) {
  (void)locale;
  return strxfrm(output, input, size);
}

int wcscoll_l(const wchar_t *left, const wchar_t *right, locale_t locale) {
  (void)locale;
  return wcscoll(left, right);
}

size_t wcsxfrm_l(wchar_t *restrict output, const wchar_t *restrict input,
                 size_t size, locale_t locale) {
  (void)locale;
  return wcsxfrm(output, input, size);
}

int iswctype_l(wint_t value, wctype_t type, locale_t locale) {
  (void)locale;
  return (___runetype_l((__ct_rune_t)value, locale) & (unsigned long)type) != 0;
}

unsigned long ___runetype_l(__ct_rune_t value, locale_t locale) {
  (void)locale;
  pss_cts_init_runes();
  return value >= 0 && value < _CACHED_RUNES ? pss_cts_runes.__runetype[value]
                                             : 0;
}

__ct_rune_t ___toupper_l(__ct_rune_t value, locale_t locale) {
  (void)locale;
  pss_cts_init_runes();
  return value >= 0 && value < _CACHED_RUNES ? pss_cts_runes.__mapupper[value]
                                             : value;
}

__ct_rune_t ___tolower_l(__ct_rune_t value, locale_t locale) {
  (void)locale;
  pss_cts_init_runes();
  return value >= 0 && value < _CACHED_RUNES ? pss_cts_runes.__maplower[value]
                                             : value;
}

wint_t btowc_l(int value, locale_t locale) {
  (void)locale;
  return btowc(value);
}

int wctob_l(wint_t value, locale_t locale) {
  (void)locale;
  return wctob(value);
}

size_t wcsnrtombs_l(char *restrict output, const wchar_t **restrict source,
                    size_t source_size, size_t output_size,
                    mbstate_t *restrict state, locale_t locale) {
  const wchar_t *input = *source;
  size_t written = 0;
  size_t consumed = 0;
  (void)state;
  (void)locale;
  while (consumed < source_size && input[consumed] != L'\0') {
    if ((unsigned long)input[consumed] > 0x7f) {
      errno = EILSEQ;
      return (size_t)-1;
    }
    if (output != NULL) {
      if (written == output_size)
        break;
      output[written] = (char)input[consumed];
    }
    ++written;
    ++consumed;
  }
  if (output != NULL)
    *source = consumed < source_size && input[consumed] == L'\0'
                  ? NULL
                  : input + consumed;
  return written;
}

size_t wcrtomb_l(char *restrict output, wchar_t value,
                 mbstate_t *restrict state, locale_t locale) {
  (void)locale;
  return wcrtomb(output, value, state);
}

size_t mbsnrtowcs_l(wchar_t *restrict output, const char **restrict source,
                    size_t source_size, size_t output_size,
                    mbstate_t *restrict state, locale_t locale) {
  const unsigned char *input = (const unsigned char *)*source;
  size_t written = 0;
  size_t consumed = 0;
  (void)state;
  (void)locale;
  while (consumed < source_size && input[consumed] != '\0') {
    if (input[consumed] > 0x7f) {
      errno = EILSEQ;
      return (size_t)-1;
    }
    if (output != NULL) {
      if (written == output_size)
        break;
      output[written] = input[consumed];
    }
    ++written;
    ++consumed;
  }
  if (output != NULL)
    *source = consumed < source_size && input[consumed] == '\0'
                  ? NULL
                  : (const char *)input + consumed;
  return written;
}

size_t mbrtowc_l(wchar_t *restrict output, const char *restrict input,
                 size_t size, mbstate_t *restrict state, locale_t locale) {
  (void)locale;
  return mbrtowc(output, input, size, state);
}

int mbtowc_l(wchar_t *restrict output, const char *restrict input, size_t size,
             locale_t locale) {
  (void)locale;
  return mbtowc(output, input, size);
}

int ___mb_cur_max_l(locale_t locale) {
  (void)locale;
  return 1;
}

size_t mbrlen_l(const char *restrict input, size_t size,
                mbstate_t *restrict state, locale_t locale) {
  (void)locale;
  return mbrlen(input, size, state);
}

struct lconv *localeconv_l(locale_t locale) {
  (void)locale;
  return localeconv();
}

long long strtoll_l(const char *text, char **end, int base, locale_t locale) {
  (void)locale;
  return strtoll(text, end, base);
}

unsigned long long strtoull_l(const char *text, char **end, int base,
                              locale_t locale) {
  (void)locale;
  return strtoull(text, end, base);
}

float strtof_l(const char *text, char **end, locale_t locale) {
  (void)locale;
  return strtof(text, end);
}

double strtod_l(const char *text, char **end, locale_t locale) {
  (void)locale;
  return strtod(text, end);
}

long double strtold_l(const char *text, char **end, locale_t locale) {
  (void)locale;
  return strtold(text, end);
}

int sscanf_l(const char *text, locale_t locale, const char *format, ...) {
  va_list arguments;
  (void)locale;
  va_start(arguments, format);
  int result = vsscanf(text, format, arguments);
  va_end(arguments);
  return result;
}

int snprintf_l(char *output, size_t size, locale_t locale, const char *format,
               ...) {
  va_list arguments;
  (void)locale;
  va_start(arguments, format);
  int result = vsnprintf(output, size, format, arguments);
  va_end(arguments);
  return result;
}

int asprintf_l(char **output, locale_t locale, const char *format, ...) {
  va_list arguments;
  va_list copy;
  (void)locale;
  va_start(arguments, format);
  va_copy(copy, arguments);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  if (length < 0) {
    va_end(arguments);
    return -1;
  }
  *output = malloc((size_t)length + 1);
  if (*output == NULL) {
    va_end(arguments);
    return -1;
  }
  int result = vsnprintf(*output, (size_t)length + 1, format, arguments);
  va_end(arguments);
  return result;
}

size_t strftime_l(char *output, size_t size, const char *format,
                  const struct tm *time_value, locale_t locale) {
  (void)locale;
  return strftime(output, size, format, time_value);
}

size_t mbsrtowcs_l(wchar_t *restrict output, const char **restrict source,
                   size_t size, mbstate_t *restrict state, locale_t locale) {
  (void)locale;
  return mbsrtowcs(output, source, size, state);
}

nl_catd catopen(const char *name, int type) {
  (void)name;
  (void)type;
  return (nl_catd)-1;
}

char *catgets(nl_catd catalog, int set, int message, const char *fallback) {
  (void)catalog;
  (void)set;
  (void)message;
  return (char *)fallback;
}

int catclose(nl_catd catalog) {
  (void)catalog;
  return 0;
}

int utimensat(int directory, const char *path, const struct timespec times[2],
              int flags) {
  (void)directory;
  (void)path;
  (void)times;
  (void)flags;
  errno = ENOSYS;
  return -1;
}

size_t backtrace(void **addresses, size_t size) {
  (void)addresses;
  (void)size;
  return 0;
}

int backtrace_symbols_fd(void *const *addresses, size_t count, int descriptor) {
  (void)addresses;
  (void)count;
  (void)descriptor;
  return 0;
}

void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *address, size_t size);
void __real_free(void *address);
int __real_posix_memalign(void **address, size_t alignment, size_t size);
size_t __real_malloc_usable_size(const void *address);

void *sceLibcMspaceCreate(const char *name, void *base, size_t size,
                          unsigned flags);
void *sceLibcMspaceMalloc(void *mspace, size_t size);
void *sceLibcMspaceCalloc(void *mspace, size_t count, size_t size);
void *sceLibcMspaceRealloc(void *mspace, void *address, size_t size);
void sceLibcMspaceFree(void *mspace, void *address);
int sceLibcMspacePosixMemalign(void *mspace, void **address, size_t alignment,
                              size_t size);
size_t sceLibcMspaceMallocUsableSize(const void *address);

#define PSS_OPENGL_HEAP_SIZE (128u * 1024u * 1024u)

static atomic_int pss_heap_state;
static void *pss_heap_base;
static void *pss_heap_mspace;

static int pss_heap_ready(void) {
  int state = atomic_load_explicit(&pss_heap_state, memory_order_acquire);
  if (state == 2)
    return 1;
  if (state != 0)
    return 0;

  int expected = 0;
  if (!atomic_compare_exchange_strong_explicit(
          &pss_heap_state, &expected, 1, memory_order_acq_rel,
          memory_order_acquire))
    return expected == 2;

  void *base = mmap(NULL, PSS_OPENGL_HEAP_SIZE, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANON, -1, 0);
  if (base == MAP_FAILED) {
    atomic_store_explicit(&pss_heap_state, -1, memory_order_release);
    return 0;
  }

  pss_heap_base = base;
  pss_heap_mspace =
      sceLibcMspaceCreate("PSS-OpenGL", base, PSS_OPENGL_HEAP_SIZE, 0);
  if (pss_heap_mspace == NULL) {
    pss_heap_base = NULL;
    munmap(base, PSS_OPENGL_HEAP_SIZE);
    atomic_store_explicit(&pss_heap_state, -1, memory_order_release);
    return 0;
  }

  atomic_store_explicit(&pss_heap_state, 2, memory_order_release);
  return 1;
}

static int pss_heap_owns(const void *address) {
  uintptr_t value = (uintptr_t)address;
  uintptr_t base = (uintptr_t)pss_heap_base;
  return atomic_load_explicit(&pss_heap_state, memory_order_acquire) == 2 &&
         value >= base && value - base < PSS_OPENGL_HEAP_SIZE;
}

void *__wrap_malloc(size_t size) {
  return pss_heap_ready() ? sceLibcMspaceMalloc(pss_heap_mspace, size)
                          : __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size) {
  return pss_heap_ready() ? sceLibcMspaceCalloc(pss_heap_mspace, count, size)
                          : __real_calloc(count, size);
}

void *__wrap_realloc(void *address, size_t size) {
  if (address == NULL)
    return __wrap_malloc(size);
  return pss_heap_owns(address)
             ? sceLibcMspaceRealloc(pss_heap_mspace, address, size)
             : __real_realloc(address, size);
}

void __wrap_free(void *address) {
  if (pss_heap_owns(address))
    sceLibcMspaceFree(pss_heap_mspace, address);
  else
    __real_free(address);
}

int __wrap_posix_memalign(void **address, size_t alignment, size_t size) {
  return pss_heap_ready()
             ? sceLibcMspacePosixMemalign(pss_heap_mspace, address, alignment,
                                          size)
             : __real_posix_memalign(address, alignment, size);
}

size_t __wrap_malloc_usable_size(const void *address) {
  return pss_heap_owns(address) ? sceLibcMspaceMallocUsableSize(address)
                                : __real_malloc_usable_size(address);
}

void pss_opengl_heap_stats_print(unsigned iteration) {
  if (iteration == 0)
    printf("[pss-opengl-cts] mspace state=%d base=%p size=%u\n",
           atomic_load_explicit(&pss_heap_state, memory_order_relaxed),
           pss_heap_base, PSS_OPENGL_HEAP_SIZE);
}
