#!/usr/bin/env python3
"""Exercise actual Gallium release functions with pending borrowed packages."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / 'src/gallium/ps5/ps5_screen.c').read_text()
functions = []
for name, argument in [('ps5_release_geometry_pipeline', 'struct ps5_context *context'),
                       ('ps5_release_tessellation_pipeline', 'struct ps5_context *context'),
                       ('ps5_delete_shader_state', 'struct pipe_context *base, void *state')]:
    start = source.index('static void\n' + name + '(' + argument + ')\n{')
    functions.append(source[start:source.index('\n}', start) + 2])
fixture = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
struct pipe_context { int unused; };
struct output { void *data, *machine_code; };
struct ps5_shader_variant {
    struct ps5_shader_variant *next;
    void *package, *streamout_package;
    struct output output, streamout_output;
};
struct ps5_shader { unsigned stage; struct ps5_shader_variant *variants; void *nir; };
struct ps5_context {
    struct pipe_context base;
    struct ps5_shader *vs,*tcs,*default_tcs,*tes,*gs,*fs;
    struct ps5_shader *geometry_vs,*geometry_gs;
    struct ps5_shader *tessellation_vs,*tessellation_tcs,*tessellation_tes,*tessellation_gs;
    void *geometry_package,*geometry_streamout_package,*geometry_layout;
    unsigned geometry_package_size,geometry_streamout_package_size,geometry_primitive_type;
    bool geometry_provoking_vtx_last;
    struct output geometry_output,geometry_streamout_output;
    void *tessellation_hs_package,*tessellation_tes_package,*tessellation_layout;
    unsigned tessellation_hs_package_size,tessellation_tes_package_size;
    bool tessellation_streamout;
    struct output tessellation_output;
};
static unsigned char *borrowed;
static unsigned drains;
static void ps5_draw_batch_drain(void) {
    if (borrowed) { assert(*borrowed == 0x5a); borrowed=NULL; ++drains; }
}
static void checked_free(void *p) {
    assert(!p || p != borrowed); /* A worker still owns this input. */
    free(p);
}
#define free checked_free
#define ralloc_free checked_free
static void psbc_free_output(struct output *out) {
    free(out->data); free(out->machine_code); memset(out,0,sizeof(*out));
}
#define psbc_free_tessellation_output psbc_free_output
static void *package(void) { unsigned char *p=malloc(32); memset(p,0x5a,32); return p; }
'''
fixture += '\n'.join(functions)
fixture += r'''
int main(void) {
    struct ps5_context c={0};
    struct ps5_shader *shader=calloc(1,sizeof(*shader));
    shader->variants=calloc(1,sizeof(*shader->variants));
    shader->variants->package=package(); borrowed=shader->variants->package;
    shader->variants->output.data=package();
    shader->variants->output.machine_code=package();
    c.vs=shader;
    ps5_delete_shader_state(&c.base,shader);
    assert(!borrowed && drains==1 && !c.vs);
    ps5_delete_shader_state(&c.base,NULL);
    puts("PASS: shader deletion drains borrowed packages before free");
}
'''
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)
    (path/'test.c').write_text(fixture)
    subprocess.run(['clang-18','-std=c11','-O1','-g','-fsanitize=address,undefined',
                    '-Wall','-Wextra','-Werror',str(path/'test.c'),'-o',str(path/'test')],check=True)
    subprocess.run([str(path/'test')],check=True,timeout=10)