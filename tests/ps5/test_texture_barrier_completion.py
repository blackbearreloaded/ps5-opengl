#!/usr/bin/env python3
"""Check actual GFX10 release/wait encoding and command-local scratch bounds."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/platform/ps5_agc_runtime_backend.c').read_text()
start = source.index('static bool\nps5_agc_emit_texture_barrier(')
body = source[start:source.index('\nstatic uint32_t *', start)]
code = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#define PS5_AGC_PKT3(op,n) (0xc0000000u | ((n)<<16) | ((op)<<8))
struct ps5_agc_command_buffer { uint32_t *bottom,*top,*up,*down; };
static bool ps5_agc_color_to_texture_barrier, ps5_agc_depth_to_texture_barrier;
''' + body + r'''
int main(void) {
   _Alignas(64) uint32_t words[256];
   assert(ps5_agc_emit_texture_barrier(NULL));
   for (unsigned flags=1; flags<=3; ++flags) {
      ps5_agc_color_to_texture_barrier=flags&1;
      ps5_agc_depth_to_texture_barrier=flags&2;
      assert(!ps5_agc_emit_texture_barrier(NULL));
      for (unsigned alignment=0; alignment<16; ++alignment) {
         memset(words,0xa5,sizeof(words));
         struct ps5_agc_command_buffer c={words,words+256,words,words+256-alignment};
         assert(ps5_agc_emit_texture_barrier(&c));
         assert(c.up==words+15 && c.up<c.down && !((uintptr_t)c.down&63));
         assert(c.down[0]==0 && words[0]==0xc0064900u);
         assert(words[1]==(flags==3 ? 0x70f514u : flags==2 ? 0x70f52bu : 0x70f52du));
         assert(words[2]==0x23000000u && words[5]==1);
         uintptr_t address=(uintptr_t)words[3] | ((uintptr_t)words[4]<<32);
         assert(address==(uintptr_t)c.down);
         assert(words[8]==0xc0053c00u && words[9]==0x13);
         assert(words[10]==words[3] && words[11]==words[4]);
         assert(words[12]==1 && words[13]==UINT32_MAX && words[14]==4);
         uint32_t *first=c.down;
         assert(ps5_agc_emit_texture_barrier(&c) && c.down<first);
      }
      for (unsigned space=0;space<47;++space) {
         memset(words,0xa5,sizeof(words));
         struct ps5_agc_command_buffer c={words,words+256,words,words+space};
         assert(!ps5_agc_emit_texture_barrier(&c));
         assert(c.up==words && c.down==words+space && words[0]==0xa5a5a5a5);
      }
   }
}
'''
with tempfile.TemporaryDirectory() as d:
    exe = str(Path(d) / 'barrier')
    subprocess.run(['cc', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-x', 'c', '-', '-o', exe],
                   input=code, text=True, check=True)
    subprocess.run([exe], check=True)
print('PASS: actual release/wait packets, distinct retained scratch, bounds and no-op')
