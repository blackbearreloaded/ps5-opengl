#!/usr/bin/env python3
"""Compare actual cached/uncached register transformations and measure local cost."""
from pathlib import Path
import runpy
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
fixture = runpy.run_path(str(Path(__file__).with_name('test_linear_color_targets.py')))
code = fixture['code'].replace('int main(void)', 'int existing_fixture(void)')
code = '#define _POSIX_C_SOURCE 200809L\n#include <time.h>\n' + code + r'''
static bool callback_fail;
static uint32_t *checked_capture(void *command, const void *table, uint32_t count) {
   capture(command, table, count);
   return callback_fail ? NULL : &marker;
}
static uint32_t random_value(uint32_t *state) {
   *state = *state * 1664525u + 1013904223u;
   return *state;
}
static uint64_t nanoseconds(void) {
   struct timespec t; assert(!clock_gettime(CLOCK_MONOTONIC, &t));
   return (uint64_t)t.tv_sec * 1000000000u + t.tv_nsec;
}
int main(void) {
   const uint16_t offsets[] = {0x318,0x31b,0x31c,0x31d,0x31e,0x31f,0x321,0x323,
      0x324,0x325,0x390,0x398,0x3a0,0x3a8,0x3b0,0x3b8,0x2d5,0x1e0,0x08e,0x205,0x207,0x7};
   struct ps5_agc_register input[PS5_AGC_CX_RECORD_CAPACITY], expected[PS5_AGC_CX_RECORD_CAPACITY];
   struct ps5_agc_register actual[PS5_AGC_CX_RECORD_CAPACITY];
   uint32_t rng=123;
   ps5_agc_set_cx=checked_capture;
   for (unsigned trial=0; trial<1000; ++trial) {
      memset(input,0xa5,sizeof(input));
      unsigned count=80 + trial%8;
      for (unsigned i=0; i<count; ++i)
         input[i]=(struct ps5_agc_register){i<ARRAY_SIZE(offsets) ? offsets[i] : 0x700+i,0,random_value(&rng)};
      if (trial%7==0) input[25]=input[2]; /* Duplicate preserves first-match semantics. */
      ps5_agc_mrt_count=1+trial%8;
      ps5_agc_mrt_samples=trial%3==0 ? 4 : 1;
      for (unsigned i=0; i<8; ++i) {
         ps5_agc_mrt_targets[i]=(void *)(uintptr_t)(0x1000000u+i*0x10000u+trial*256u);
         ps5_agc_mrt_color_info[i]=0x8028;
         ps5_agc_mrt_attrib2[i]=random_value(&rng);
         ps5_agc_mrt_views[i]=0;
         ps5_agc_mrt_pitches[i]=trial%5==0 ? 256 : 0;
         ps5_agc_mrt_blend[i]=random_value(&rng);
      }
      ps5_agc_mrt_mask=random_value(&rng);
      ps5_agc_depth_width=1+trial; ps5_agc_depth_height=1+trial*2;
      ps5_agc_sample_mask=random_value(&rng);
      ps5_agc_multisample_enable=trial&1; ps5_agc_sample_shading=trial&2;
      ps5_agc_dual_source_blend=trial&4; ps5_agc_alpha_to_coverage=trial&8;
      ps5_agc_poly_line_smooth=trial&16;
      ps5_agc_clip_control_valid=trial&32; ps5_agc_clip_control=random_value(&rng);
      ps5_agc_vs_out_control_valid=trial&64; ps5_agc_vs_out_control=random_value(&rng);
      ps5_agc_occlusion_query=trial&128 ? (void *)(uintptr_t)0x400000 : NULL;
      ps5_agc_occlusion_precise=trial&256;
      ps5_agc_border_color_table=trial&512 ? (void *)(uintptr_t)0x800000 : NULL;
      callback_fail=trial%11==0;
      uint32_t expected_count=0;
      memcpy(expected,input,sizeof(input));
      uint32_t *result=ps5_agc_set_cx_mrt_uncached(&marker,expected,count,&expected_count);
      for (unsigned repeat=0; repeat<3; ++repeat) {
         memcpy(actual,input,sizeof(input));
         assert(ps5_agc_set_cx_mrt(&marker,actual,count)==result);
         assert(!memcmp(actual,expected,sizeof(actual)));
         if (result) assert(captured_count==expected_count);
      }
   }
   callback_fail=false;
   ps5_agc_mrt_samples=1; ps5_agc_mrt_count=1;
   const unsigned count=80, iterations=20000;
   for (unsigned i=0;i<count;++i)
      input[i]=(struct ps5_agc_register){i<ARRAY_SIZE(offsets) ? offsets[i] : 0x700+i,0,i};
   for (unsigned mode=0;mode<2;++mode) {
      uint64_t start=nanoseconds();
      for (unsigned i=0;i<iterations;++i) {
         memcpy(actual,input,sizeof(input));
         uint32_t output_count=0;
         assert((mode ? ps5_agc_set_cx_mrt(&marker,actual,count) :
            ps5_agc_set_cx_mrt_uncached(&marker,actual,count,&output_count))==&marker);
      }
      printf("state-transform %s ns/call=%.1f\n",mode ? "cached" : "uncached",
             (double)(nanoseconds()-start)/iterations);
   }
   puts("PASS: 1000 state/table mutations, repeated hits, duplicate registers, failures and exact output bytes");
}
'''
# ARRAY_SIZE is not exposed by this standalone backend fixture.
code = code.replace('#include <time.h>', '#include <time.h>\n#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))')
with tempfile.TemporaryDirectory() as tmp:
    binary = str(Path(tmp) / 'state-cache')
    for sanitize in (True, False):
        flags = ['-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-no-pie'] if sanitize else []
        subprocess.run(['cc', '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                        '-Wno-unused-function', '-Wno-unused-variable', *flags,
                        '-I', str(root / 'src/gallium/ps5'), '-x', 'c', '-', '-o', binary],
                       input=code, text=True, check=True, timeout=60)
        print('sanitized' if sanitize else 'optimized host timing (not PS5)', flush=True)
        subprocess.run([binary], check=True, timeout=30)
