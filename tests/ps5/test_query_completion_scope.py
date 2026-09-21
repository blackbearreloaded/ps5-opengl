#!/usr/bin/env python3
"""Actual query completion helper: target-only waits and nonblocking polling."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2];s=(root/'src/gallium/ps5/ps5_screen.c').read_text()
a=s.index('static bool\nps5_draw_batch_query_ready(');b=s.index('static bool\nps5_buffer_overlaps_resource(',a)
for name in ['ps5_get_query_result','ps5_get_query_result_resource']:
 i=s.index('\n'+name+'(');f=s[i:s.index('\n}\n',i)]
 assert 'ps5_draw_batch_drain();' not in f
 assert 'ps5_draw_batch_query_ready(query,' in f
code=r"""
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#define PS5_INFLIGHT_BATCH_CAPACITY 3
struct ps5_query { unsigned value; };
struct ps5_deferred_batch { uint64_t sequence; unsigned count; struct { struct ps5_query *occlusion_query; } slots[4]; };
static struct ps5_deferred_batch ps5_deferred,ps5_inflight[3];
static unsigned ps5_inflight_head,ps5_inflight_count,waits,polls,submits,locked;
static uint64_t ps5_completed_sequence,next_sequence;
static bool available;
static int ps5_deferred_mutex;
static void simple_mtx_lock(int *m){(void)m;assert(!locked);locked=1;}
static void simple_mtx_unlock(int *m){(void)m;assert(locked);locked=0;}
static bool ps5_draw_batch_retire_one_locked(bool wait) {
 assert(locked);if(wait)waits++;else polls++;
 if(!ps5_inflight_count||(!wait&&!available))return false;
 struct ps5_deferred_batch *b=&ps5_inflight[ps5_inflight_head];
 for(unsigned i=0;i<b->count;i++)if(b->slots[i].occlusion_query)b->slots[i].occlusion_query->value++;
 ps5_completed_sequence=b->sequence;memset(b,0,sizeof(*b));ps5_inflight_head=(ps5_inflight_head+1)%3;ps5_inflight_count--;return true;
}
static void ps5_draw_batch_retire_locked(bool wait){while(ps5_inflight_count&&ps5_draw_batch_retire_one_locked(wait)){} }
static void ps5_draw_batch_flush_locked(void){
 assert(locked);if(ps5_inflight_count==3)assert(ps5_draw_batch_retire_one_locked(true));
 ps5_deferred.sequence=++next_sequence;ps5_inflight[(ps5_inflight_head+ps5_inflight_count++)%3]=ps5_deferred;
 memset(&ps5_deferred,0,sizeof(ps5_deferred));submits++;
}
static void enqueue(struct ps5_query *q){ps5_deferred.count=1;ps5_deferred.slots[0].occlusion_query=q;simple_mtx_lock(&ps5_deferred_mutex);ps5_draw_batch_flush_locked();simple_mtx_unlock(&ps5_deferred_mutex);}
"""+s[a:b]+r"""
int main(void){
 struct ps5_query q={0},other={0},empty={0};
 enqueue(&q);enqueue(&other);
 ps5_deferred.count=1;ps5_deferred.slots[0].occlusion_query=&other;
 assert(!ps5_draw_batch_query_ready(&q,false)&&waits==0&&q.value==0);
 assert(ps5_draw_batch_query_ready(&q,true)&&waits==1&&q.value==1&&other.value==0&&ps5_inflight_count==1&&ps5_deferred.count==1);
 unsigned n=submits;assert(ps5_draw_batch_query_ready(&q,true)&&waits==1&&submits==n);
 ps5_deferred.count=1;ps5_deferred.slots[0].occlusion_query=&other;
 assert(ps5_draw_batch_query_ready(&empty,false)&&ps5_deferred.count==1&&submits==n);
 enqueue(&other);enqueue(&other);assert(ps5_inflight_count==3);
 ps5_deferred.count=1;ps5_deferred.slots[0].occlusion_query=&q;n=submits;
 assert(!ps5_draw_batch_query_ready(&q,false)&&submits==n&&ps5_deferred.count==1&&waits==1);
 available=true;assert(ps5_draw_batch_query_ready(&q,false)&&waits==1&&q.value==2&&ps5_inflight_count==0);
 available=false;enqueue(&q);enqueue(&other);
 ps5_deferred.count=1;ps5_deferred.slots[0].occlusion_query=&other;enqueue(&q);
 assert(ps5_draw_batch_query_ready(&q,true)&&q.value==4&&ps5_inflight_count==0);
 assert(!locked);return 0;
}
"""
with tempfile.TemporaryDirectory() as d:
 p=Path(d)/'test.c';p.write_text(code)
 subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p),'-o',str(p.with_suffix(''))],check=True)
 subprocess.run([str(p.with_suffix(''))],check=True)
print('Query-only completion, no-wait availability, full FIFO, wraparound and result collection PASS')
