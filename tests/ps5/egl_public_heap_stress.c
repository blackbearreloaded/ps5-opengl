// SPDX-License-Identifier: GPL-3.0-or-later
/* Exercise the native app's real wrapped allocator, without emulator/GPU work. */
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { THREADS=8, SLOTS=32, ROUNDS=20000 };
static atomic_int start, errors;
struct slot { unsigned char *p; size_t size; unsigned char value; };
static int valid(struct slot *s) {
    for (size_t j=0; j<s->size; ++j)
        if (s->p[j]!=s->value) return 0;
    return 1;
}
static void *worker(void *argument) {
    uintptr_t id=(uintptr_t)argument;
    struct slot slots[SLOTS]={0};
    while (!atomic_load_explicit(&start,memory_order_acquire)) __builtin_ia32_pause();
    for (unsigned i=0; i<ROUNDS; ++i) {
        struct slot *s=&slots[i%SLOTS];
        if (!valid(s)) { atomic_fetch_add(&errors,1); break; }
        size_t size=1+(i*1543u+id*29u)%8192;
        unsigned char *p=NULL;
        if (i%3==0) {
            p=realloc(s->p,size);
            if (p) {
                struct slot prefix={p,s->size<size?s->size:size,s->value};
                if (!valid(&prefix)) atomic_fetch_add(&errors,1);
            }
        } else {
            free(s->p); s->p=NULL; s->size=0;
            if (i%3==1) {
                p=calloc(size,1);
                if (p) { struct slot zero={p,size,0}; if (!valid(&zero)) atomic_fetch_add(&errors,1); }
            } else if (posix_memalign((void **)&p,256,size)!=0) p=NULL;
        }
        if (!p) { atomic_fetch_add(&errors,1); break; }
        s->p=p; s->size=size; s->value=(unsigned char)(i+id*17);
        memset(p,s->value,size);
    }
    for (unsigned i=0;i<SLOTS;++i) {
        if (!valid(&slots[i])) atomic_fetch_add(&errors,1);
        free(slots[i].p);
    }
    return NULL;
}
#ifdef PS5_HEAP_LARGE_TEST
#include <sys/mman.h>
extern void *sceLibcMspaceCreate(const char *,void *,size_t,unsigned);
extern void *sceLibcMspaceMalloc(void *,size_t);
extern void sceLibcMspaceFree(void *,void *);
extern int sceLibcMspaceDestroy(void *);
extern int64_t sceKernelGetDirectMemorySize(void);
extern int32_t sceKernelAllocateDirectMemory(int64_t,int64_t,size_t,size_t,int,int64_t *);
extern int32_t sceKernelMapDirectMemory(void **,size_t,int,int,int64_t,size_t);
extern int32_t sceKernelReleaseDirectMemory(int64_t,size_t);
static int large_heap(void) {
    const size_t mib=1024u*1024u, capacity=2048u*mib;
    void *base=NULL; int64_t physical=-1;
    int32_t rc=sceKernelAllocateDirectMemory(0,sceKernelGetDirectMemorySize(),capacity,16384,12,&physical);
    if(rc) { printf("[heap-stress] direct allocation=%08x\n",(unsigned)rc); return 1; }
    rc=sceKernelMapDirectMemory(&base,capacity,PROT_READ|PROT_WRITE,0,physical,16384);
    if(rc || !base) {
        sceKernelReleaseDirectMemory(physical,capacity);
        printf("[heap-stress] direct mapping=%08x\n",(unsigned)rc); return 1;
    }
    memset(base,0,capacity);
    void *space=sceLibcMspaceCreate("heap-2GiB-check",base,capacity,0);
    if(!space) { munmap(base,capacity); sceKernelReleaseDirectMemory(physical,capacity); return 1; }
    struct slot blocks[6]={0};
    const unsigned sizes[]={17,33,65,129,257,601}, order[]={2,5,0,4,1,3};
    int result=1;
    for(unsigned i=0;i<6;++i) {
        blocks[i]=(struct slot){sceLibcMspaceMalloc(space,sizes[i]*mib),sizes[i]*mib,(unsigned char)(0x31+i)};
        if(!blocks[i].p) { printf("[heap-stress] large allocation failed index=%u\n",i); goto done; }
        memset(blocks[i].p,blocks[i].value,blocks[i].size);
    }
    printf("[heap-stress] capacity=2048MiB live=1102MiB allocated=6\n");
    for(unsigned j=0;j<6;++j) {
        unsigned i=order[j];
        if(!valid(&blocks[i])) goto done;
        sceLibcMspaceFree(space,blocks[i].p); blocks[i].p=NULL; blocks[i].size=0;
        printf("[heap-stress] large free index=%u passed\n",i);
    }
    blocks[0]=(struct slot){sceLibcMspaceMalloc(space,1536u*mib),1536u*mib,0xa6};
    if(!blocks[0].p) goto done;
    memset(blocks[0].p,blocks[0].value,blocks[0].size);
    if(!valid(&blocks[0])) goto done;
    result=0;
done:
    for(unsigned i=0;i<6;++i) if(blocks[i].p) sceLibcMspaceFree(space,blocks[i].p);
    if(sceLibcMspaceDestroy(space)) result=1;
    if(munmap(base,capacity)) result=1;
    if(sceKernelReleaseDirectMemory(physical,capacity)) result=1;
    printf("[heap-stress] capacity=2048MiB coalesced=1536MiB result=%d\n",result);
    return result;
}
#endif
int main(void) {
#ifdef PS5_HEAP_LARGE_TEST
    if(large_heap()) return 1;
#endif
    atomic_store(&start,1);
    worker((void *)0);
    printf("[heap-stress] serial rounds=%u errors=%d\n",ROUNDS,atomic_load(&errors));
    if (atomic_load(&errors)) return 1;
    atomic_store(&start,0);
    pthread_t threads[THREADS]; unsigned count=0;
    for (;count<THREADS;++count)
        if (pthread_create(&threads[count],NULL,worker,(void *)(uintptr_t)count)) break;
    atomic_store_explicit(&start,1,memory_order_release);
    for (unsigned i=0;i<count;++i)
        if (pthread_join(threads[i],NULL)) abort();
    int result=count!=THREADS || atomic_load(&errors)!=0;
    printf("[heap-stress] threads=%u rounds=%u errors=%d result=%d\n",
           count,ROUNDS,atomic_load(&errors),result);
    return result;
}