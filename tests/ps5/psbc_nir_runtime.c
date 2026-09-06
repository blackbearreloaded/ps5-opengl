#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler/glsl_types.h"
#include "compiler/nir/nir_builder.h"
#include "psbc_compile.h"
#include "util/ralloc.h"

static uint32_t
fnv1a(const uint8_t *data, size_t size)
{
   uint32_t hash = UINT32_C(2166136261);

   for (size_t i = 0; i < size; ++i) {
      hash ^= data[i];
      hash *= UINT32_C(16777619);
   }
   return hash;
}

static nir_shader *
build_vertex_shader(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_VERTEX, psbc_get_nir_options(PSBC_STAGE_VERTEX),
      "ps5-gallium-vs");
   nir_variable *position = nir_variable_create(
      b.shader, nir_var_shader_out, glsl_vec4_type(), "gl_Position");
   nir_def *vertex_id = nir_load_vertex_id_zero_base(&b);
   nir_def *vertex_float = nir_i2f32(&b, vertex_id);
   nir_def *x = nir_fmul_imm(&b, nir_fcos(&b, vertex_float), 0.5f);
   nir_def *y = nir_fmul_imm(&b, nir_fsin(&b, vertex_float), 0.5f);

   position->data.location = VARYING_SLOT_POS;
   nir_store_var(&b, position,
                 nir_vec4(&b, x, y, nir_imm_float(&b, 0.0f),
                          nir_imm_float(&b, 1.0f)),
                 0xf);
   return b.shader;
}

static nir_shader *
build_fragment_shader(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, psbc_get_nir_options(PSBC_STAGE_FRAGMENT),
      "ps5-gallium-fs");
   nir_variable *color = nir_variable_create(
      b.shader, nir_var_shader_out, glsl_vec4_type(), "color");

   color->data.location = FRAG_RESULT_DATA0;
   nir_store_var(&b, color,
                 nir_imm_vec4(&b, 1.0f, 0.25f, 0.125f, 1.0f), 0xf);
   return b.shader;
}

static nir_shader *
build_legacy_rectangle_query_fragment_shader(void)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, psbc_get_nir_options(PSBC_STAGE_FRAGMENT),
      "ps5-gallium-legacy-rectangle-query-fs");
   nir_variable *color = nir_variable_create(
      b.shader, nir_var_shader_out, glsl_vec4_type(), "color");
   nir_tex_instr *query = nir_tex_instr_create(b.shader, 1);

   query->op = nir_texop_txs;
   query->sampler_dim = GLSL_SAMPLER_DIM_RECT;
   query->texture_index = 0;
   query->sampler_index = 0;
   query->dest_type = nir_type_int32;
   query->src[0].src_type = nir_tex_src_lod;
   query->src[0].src = nir_src_for_ssa(nir_imm_int(&b, 0));
   nir_def_init(&query->instr, &query->def,
                nir_tex_instr_dest_size(query), 32);
   nir_builder_instr_insert(&b, &query->instr);

   color->data.location = FRAG_RESULT_DATA0;
   nir_store_var(&b, color,
                 nir_vec4(&b,
                          nir_i2f32(&b, nir_channel(&b, &query->def, 0)),
                          nir_i2f32(&b, nir_channel(&b, &query->def, 1)),
                          nir_imm_float(&b, 0.0f), nir_imm_float(&b, 1.0f)),
                 0xf);
   return b.shader;
}

static int
compile_nir(const char *label, nir_shader *nir, PsbcStage stage, bool ngg,
            bool legacy_texture)
{
   PsbcCompileOptions options;
   PsbcShaderOutput output;
   PsbcResult result;
   uint32_t hash = 0;

   memset(&options, 0, sizeof(options));
   memset(&output, 0, sizeof(output));
   options.target = PSBC_TARGET_PS5;
   options.stage = stage;
   options.entrypoint = "main";
   options.optimise = true;
   options.ngg = ngg;
   if (legacy_texture) {
      options.descriptor_binding_count = 1;
      options.descriptor_bindings[0] = (PsbcDescriptorBinding){
         .set = 0,
         .binding = 0,
         .type = PSBC_DESCRIPTOR_COMBINED_IMAGE_SAMPLER,
         .array_size = 1,
         .offset = 0,
         .stride = 48,
      };
   }
   result = psbc_compile_nir(nir, &options, &output);
   if (output.machine_code)
      hash = fnv1a(output.machine_code, output.machine_code_size);
   printf("[ps5-psbc-nir] %s rc=%d(%s) code=%zu fnv=%08" PRIx32
          " source_stage=%u hw_stage=%u cx=%u sh=%u unresolved=%u\n",
          label, result, psbc_result_string(result), output.machine_code_size,
          hash, output.metadata.source_stage, output.metadata.hardware_stage,
          output.metadata.context_register_count,
          output.metadata.shader_register_count,
          output.metadata.unresolved_fields);
   psbc_free_output(&output);
   return result == PSBC_RESULT_OK && hash != 0 ? 0 : 1;
}

int
main(void)
{
   nir_shader *vs;
   nir_shader *fs;
   nir_shader *legacy_rectangle_fs;
   int result;

   printf("[ps5-psbc-nir] stage=start\n");
   setenv("PSBC_DEBUG_STAGES", "1", 1);
   psbc_init();
   printf("[ps5-psbc-nir] stage=initialized\n");
   vs = build_vertex_shader();
   printf("[ps5-psbc-nir] stage=vertex-built ptr=%p\n", (void *)vs);
   fs = build_fragment_shader();
   printf("[ps5-psbc-nir] stage=fragment-built ptr=%p\n", (void *)fs);
   legacy_rectangle_fs = build_legacy_rectangle_query_fragment_shader();
   printf("[ps5-psbc-nir] stage=legacy-rectangle-built ptr=%p\n",
          (void *)legacy_rectangle_fs);
   result = compile_nir("vertex-ngg", vs, PSBC_STAGE_VERTEX, true, false);
   printf("[ps5-psbc-nir] stage=vertex-compiled result=%d\n", result);
   result |= compile_nir("fragment", fs, PSBC_STAGE_FRAGMENT, false, false);
   printf("[ps5-psbc-nir] stage=fragment-compiled result=%d\n", result);
   result |= compile_nir("legacy-rectangle-query", legacy_rectangle_fs,
                         PSBC_STAGE_FRAGMENT, false, true);
   printf("[ps5-psbc-nir] stage=legacy-rectangle-compiled result=%d\n",
          result);
   ralloc_free(vs);
   ralloc_free(fs);
   ralloc_free(legacy_rectangle_fs);
   psbc_shutdown();
   printf("[ps5-psbc-nir] result=%d\n", result);
   return result;
}
