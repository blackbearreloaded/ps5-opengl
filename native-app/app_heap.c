/* Shared native-app allocator integration, originally used by the CTS runner. */
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>

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

/* ponytail: fixed 128 MiB, process-lifetime heap; revisit the budget only for
 * measured application needs. Do not unmap underneath late C++ destructors. */
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
  /* Acquire publication before reading non-atomic heap metadata. */
  if (atomic_load_explicit(&pss_heap_state, memory_order_acquire) != 2)
    return 0;
  uintptr_t value = (uintptr_t)address;
  uintptr_t base = (uintptr_t)pss_heap_base;
  return value >= base && value - base < PSS_OPENGL_HEAP_SIZE;
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
  if (iteration == 0) {
    int state = atomic_load_explicit(&pss_heap_state, memory_order_acquire);
    printf("[pss-opengl-cts] mspace state=%d base=%p size=%u\n",
           state, state == 2 ? pss_heap_base : NULL, PSS_OPENGL_HEAP_SIZE);
  }
}
