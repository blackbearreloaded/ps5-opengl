// SPDX-License-Identifier: GPL-3.0-or-later
// Qualify bounded, address-only instruction sampling before using it in an app.
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ucontext.h>
#include <unistd.h>

static unsigned stopped, ready, samples;
static uintptr_t last_pc;
static volatile uint64_t work;

static void sample(int signal, siginfo_t *info, void *context)
{
   (void)signal;
   (void)info;
   // Native firmware 6.02 signal frame; qualified against this spin loop.
   // The SDK FreeBSD ucontext.mc_rip offset (176) does not describe this ABI.
   __atomic_store_n(&last_pc, ((const uintptr_t *)context)[224 / sizeof(uintptr_t)], __ATOMIC_RELAXED);
   __atomic_add_fetch(&samples, 1, __ATOMIC_RELEASE);
}

static void *spin(void *unused)
{
   (void)unused;
   sigset_t mask;
   sigemptyset(&mask);
   sigaddset(&mask, SIGUSR2);
   pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
   __atomic_store_n(&ready, 1, __ATOMIC_RELEASE);
   while (!__atomic_load_n(&stopped, __ATOMIC_ACQUIRE))
      ++work;
   return NULL;
}

int main(void)
{
   struct sigaction action = {0}, previous;
   action.sa_sigaction = sample;
   action.sa_flags = SA_SIGINFO | SA_RESTART;
   sigemptyset(&action.sa_mask);
   if (sigaction(SIGUSR2, &action, &previous)) return 1;
   pthread_t thread;
   if (pthread_create(&thread, NULL, spin, NULL)) {
      sigaction(SIGUSR2, &previous, NULL);
      return 1;
   }
   for (unsigned i = 0; i < 1000 && !__atomic_load_n(&ready, __ATOMIC_ACQUIRE); ++i)
      usleep(1000);
   unsigned passed = __atomic_load_n(&ready, __ATOMIC_ACQUIRE);
   for (unsigned i = 0; i < 32 && passed; ++i) {
      passed &= pthread_kill(thread, SIGUSR2) == 0;
      for (unsigned n = 0; n < 100 && __atomic_load_n(&samples, __ATOMIC_ACQUIRE) <= i; ++n)
         usleep(1000);
      passed &= __atomic_load_n(&samples, __ATOMIC_ACQUIRE) == i + 1;
      const uintptr_t pc = __atomic_load_n(&last_pc, __ATOMIC_RELAXED);
      passed &= pc >= (uintptr_t)spin && pc < (uintptr_t)spin + 256;
   }
   __atomic_store_n(&stopped, 1, __ATOMIC_RELEASE);
   passed &= pthread_join(thread, NULL) == 0;
   passed &= sigaction(SIGUSR2, &previous, NULL) == 0;
   printf("[ps5-pc-sample] count=%u pc=%lx work=%llu cleanup=1 result=%u\n",
          samples, (unsigned long)last_pc, (unsigned long long)work, passed ? 0 : 1);
   return passed ? 0 : 1;
}
