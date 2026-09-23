// SPDX-License-Identifier: GPL-3.0-or-later
// Distinct-address wakes must not strand a different waiter.
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>

int futex_wait(uint32_t *, int32_t, const struct timespec *);
int futex_wake(uint32_t *, int32_t);
int64_t os_time_get_nano(void);

static uint32_t words[8], ready, done[8];
static void *waiter(void *arg)
{
   const unsigned i = (uintptr_t)arg;
   __atomic_add_fetch(&ready, 1, __ATOMIC_RELEASE);
   while (!__atomic_load_n(&words[i], __ATOMIC_ACQUIRE))
      futex_wait(&words[i], 0, NULL);
   __atomic_store_n(&done[i], 1, __ATOMIC_RELEASE);
   return NULL;
}

int main(void)
{
   pthread_t threads[8];
   unsigned count = 0, passed = 1;
   for (; count < 8; ++count)
      if (pthread_create(&threads[count], NULL, waiter, (void *)(uintptr_t)count))
         break;
   for (unsigned n = 0; n < 1000 && __atomic_load_n(&ready, __ATOMIC_ACQUIRE) != count; ++n)
      usleep(1000);
   passed &= count == 8 && __atomic_load_n(&ready, __ATOMIC_ACQUIRE) == count;
   usleep(20000);
   for (unsigned n = count; n; --n) {
      const unsigned i = n - 1;
      __atomic_store_n(&words[i], 1, __ATOMIC_RELEASE);
      futex_wake(&words[i], 1);
      for (unsigned wait = 0; wait < 100 && !__atomic_load_n(&done[i], __ATOMIC_ACQUIRE); ++wait)
         usleep(1000);
      passed &= __atomic_load_n(&done[i], __ATOMIC_ACQUIRE);
   }
   for (unsigned i = 0; i < count; ++i) {
      __atomic_store_n(&words[i], 1, __ATOMIC_RELEASE);
      futex_wake(&words[i], INT_MAX);
   }
   for (unsigned i = 0; i < count; ++i)
      passed &= pthread_join(threads[i], NULL) == 0;
   uint32_t timeout_word = 0;
   struct timespec deadline;
   clock_gettime(CLOCK_MONOTONIC, &deadline);
   deadline.tv_nsec += 30000000;
   if (deadline.tv_nsec >= 1000000000) {
      ++deadline.tv_sec;
      deadline.tv_nsec -= 1000000000;
   }
   const int result = futex_wait(&timeout_word, 0, &deadline);
   const int timed_out = result == ETIMEDOUT || (result == -1 && errno == ETIMEDOUT);
   passed &= timed_out;
   printf("[ps5-futex] waiters=%u address-wakes=%u timeout=%d result=%d\n",
          count, passed, timed_out, passed ? 0 : 1);
#ifdef PS5_CLOCK_COST_TEST
   /* The draw profiler calls this clock at every preparation phase. */
   const int64_t start = os_time_get_nano();
   int64_t previous = start;
   for (unsigned i = 0; i < 10000; ++i) {
      const int64_t current = os_time_get_nano();
      passed &= current >= previous;
      previous = current;
   }
   printf("[ps5-clock-cost] calls=10000 mean_ns=%.1f monotonic=%u\n",
          (previous - start) / 10000.0, passed);
#endif
   return passed ? 0 : 1;
}
