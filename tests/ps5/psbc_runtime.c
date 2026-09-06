#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "psbc_compile.h"
#include "ps5_agc_package.h"

#include "gate1_vs_spv.inc"
#include "gate1_ps_spv.inc"
#include "gate1_vs_expected.inc"
#include "gate1_ps_expected.inc"
#include "gate1_vs_package_expected.inc"
#include "gate1_ps_package_expected.inc"

static uint32_t
fnv1a(const uint8_t *data, size_t size)
{
   uint32_t hash = UINT32_C(2166136261);
   size_t i;

   for (i = 0; i < size; ++i) {
      hash ^= data[i];
      hash *= UINT32_C(16777619);
   }
   return hash;
}

static int
compile_and_check(const char *label, const uint8_t *spirv, size_t spirv_size,
                  PsbcStage stage, int ngg, const uint8_t *expected,
                  size_t expected_size, const uint8_t *expected_package,
                  size_t expected_package_size)
{
   PsbcCompileOptions options;
   PsbcShaderOutput output;
   PsbcResult rc;
   uint32_t hash = 0;
   int match = 0;
   uint8_t *package = NULL;
   size_t package_size = 0;
   uint32_t package_hash = 0;
   int package_match = 0;

   memset(&options, 0, sizeof(options));
   memset(&output, 0, sizeof(output));
   options.target = PSBC_TARGET_PS5;
   options.stage = stage;
   options.entrypoint = "main";
   options.optimise = true;
   options.ngg = ngg != 0;

   rc = psbc_compile_shader((const uint32_t *)spirv, spirv_size, &options,
                            &output);
   if (rc == PSBC_RESULT_OK && output.machine_code) {
      hash = fnv1a(output.machine_code, output.machine_code_size);
      match = output.machine_code_size == expected_size &&
              memcmp(output.machine_code, expected, expected_size) == 0;
      if (ps5_agc_package_build(&output, 4, &package, &package_size) == 0) {
         package_hash = fnv1a(package, package_size);
         package_match = package_size == expected_package_size &&
                         memcmp(package, expected_package, package_size) == 0;
      }
   }

   printf("[ps5-psbc] %s rc=%d(%s) code=%zu fnv=%08" PRIx32
          " exact=%d package=%zu/%08" PRIx32 "/%d"
          " source_stage=%u hw_stage=%u cx=%u sh=%u unresolved=%u\n",
          label, rc, psbc_result_string(rc), output.machine_code_size, hash,
          match, package_size, package_hash, package_match,
          output.metadata.source_stage, output.metadata.hardware_stage,
          output.metadata.context_register_count,
          output.metadata.shader_register_count,
          output.metadata.unresolved_fields);
   psbc_free_output(&output);
   free(package);
   return rc == PSBC_RESULT_OK && match && package_match ? 0 : 1;
}

int
main(void)
{
   int result;

   psbc_init();
   result = compile_and_check("vertex-ngg", gate1_vs_spv,
                              gate1_vs_spv_len, PSBC_STAGE_VERTEX, 1,
                              gate1_vs_expected, gate1_vs_expected_len,
                              gate1_vs_package_expected,
                              gate1_vs_package_expected_len);
   result |= compile_and_check("fragment", gate1_ps_spv,
                               gate1_ps_spv_len, PSBC_STAGE_FRAGMENT, 0,
                               gate1_ps_expected, gate1_ps_expected_len,
                               gate1_ps_package_expected,
                               gate1_ps_package_expected_len);
   psbc_shutdown();
   printf("[ps5-psbc] result=%d\n", result);
   return result;
}
