#include "ps5_agc_package.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#define AGC_MAGIC UINT32_C(0x34333231)
#define ELF_MACHINE_AMDGPU 224u
#define ELF_HEADER_SIZE 64u
#define ELF_SECTION_SIZE 64u
#define HEADER_PREFIX_SIZE 96u
#define RESOURCE_LAYOUT_SIZE 54u

static size_t
align_to(size_t value, size_t alignment)
{
   return (value + alignment - 1u) & ~(alignment - 1u);
}

static void
put16(uint8_t *data, size_t offset, uint16_t value)
{
   memcpy(data + offset, &value, sizeof(value));
}

static void
put32(uint8_t *data, size_t offset, uint32_t value)
{
   memcpy(data + offset, &value, sizeof(value));
}

static void
put64(uint8_t *data, size_t offset, uint64_t value)
{
   memcpy(data + offset, &value, sizeof(value));
}

static bool
register_pair_valid(const PsbcRegisterWrite *registers, uint32_t count,
                    uint16_t lo, uint16_t hi, bool require_zero)
{
   for (uint32_t i = 0; i + 1u < count; ++i) {
      if (registers[i].offset != lo)
         continue;
      return registers[i + 1u].offset == hi &&
             (!require_zero ||
              (!registers[i].value && !registers[i + 1u].value));
   }
   return false;
}

static const PsbcRegisterWrite *
find_register(const PsbcRegisterWrite *registers, uint32_t count,
              uint16_t offset)
{
   for (uint32_t i = 0; i < count; ++i)
      if (registers[i].offset == offset)
         return &registers[i];
   return NULL;
}

static bool
registers_valid(const PsbcRegisterWrite *registers, uint32_t count,
                uint32_t limit)
{
   if (count > limit || count > UINT8_MAX)
      return false;
   for (uint32_t i = 0; i < count; ++i) {
      for (uint32_t j = 0; j < i; ++j) {
         if (registers[i].offset == registers[j].offset)
            return false;
      }
   }
   return true;
}

static bool
semantics_valid(const uint32_t *semantics, uint32_t count, bool unique)
{
   if (count > PSBC_MAX_SEMANTICS)
      return false;
   for (uint32_t i = 0; i < count; ++i) {
      for (uint32_t j = 0; j < i; ++j) {
         if (unique &&
             (semantics[i] & 0xffu) == (semantics[j] & 0xffu))
            return false;
      }
   }
   return true;
}

static void
write_register(uint8_t *data, size_t offset,
               const PsbcRegisterWrite *record, bool patch_esgs,
               uint32_t esgs_ring_itemsize)
{
   put16(data, offset, record->offset);
   put16(data, offset + 2u, 0);
   put32(data, offset + 4u,
         patch_esgs && record->offset == 0x2abu
            ? esgs_ring_itemsize
            : record->value);
}

static void
write_section(uint8_t *data, size_t section_table, unsigned index,
              uint32_t name, uint32_t type, uint64_t flags,
              uint64_t offset, uint64_t size, uint64_t alignment)
{
   const size_t at = section_table + index * ELF_SECTION_SIZE;

   put32(data, at, name);
   put32(data, at + 4u, type);
   put64(data, at + 8u, flags);
   put64(data, at + 24u, offset);
   put64(data, at + 32u, size);
   put64(data, at + 48u, alignment);
}

int
ps5_agc_package_build(const PsbcShaderOutput *shader,
                      uint32_t esgs_ring_itemsize,
                      uint8_t **package, size_t *package_size)
{
   static const uint8_t names[] =
      "\0.shader_text\0.shader_header\0.shstrtab";
   const PsbcShaderMetadata *metadata;
   const PsbcRegisterWrite *context;
   const PsbcRegisterWrite *shader_registers;
   uint8_t agc_stage;
   uint16_t pgm_lo, pgm_hi, rsrc1, rsrc2;
   size_t shader_at = HEADER_PREFIX_SIZE;
   size_t context_at;
   size_t linkage_at;
   size_t cursor;
   size_t input_at;
   size_t output_at;
   size_t resource_at;
   size_t header_size;
   size_t text_at = 0x100u;
   size_t header_at;
   size_t names_at;
   size_t sections_at;
   size_t total_size;
   uint8_t *data;
   uint8_t *header;
   bool has_linkage;
   bool patch_esgs;
   const PsbcRegisterWrite *linkage_primitive_override = NULL;

   if (!shader || !package || !package_size || !shader->machine_code ||
       !shader->machine_code_size || (shader->machine_code_size & 3u))
      return -1;
   *package = NULL;
   *package_size = 0;
   metadata = &shader->metadata;
   context = metadata->context_registers;
   shader_registers = metadata->shader_registers;
   if (metadata->version != PSBC_SHADER_METADATA_VERSION ||
       metadata->target != PSBC_TARGET_PS5 ||
       (metadata->unresolved_fields &
        ~(PSBC_UNRESOLVED_PROGRAM_CHECKSUM |
          PSBC_UNRESOLVED_NGG_ESGS_RING_ITEMSIZE |
          PSBC_UNRESOLVED_AGC_LINKAGE)) ||
       !registers_valid(context, metadata->context_register_count,
                        PSBC_MAX_CONTEXT_REGISTERS) ||
       !registers_valid(shader_registers, metadata->shader_register_count,
                        PSBC_MAX_SHADER_REGISTERS) ||
       !semantics_valid(metadata->input_semantics,
                        metadata->input_semantic_count, false) ||
       !semantics_valid(metadata->output_semantics,
                        metadata->output_semantic_count, true))
      return -2;

   has_linkage = metadata->linkage_valid;
   if (metadata->hardware_stage == PSBC_HW_STAGE_PIXEL &&
       metadata->source_stage == PSBC_STAGE_FRAGMENT &&
       !metadata->output_semantic_count) {
      agc_stage = 1;
      pgm_lo = 0x008;
      pgm_hi = 0x009;
      rsrc1 = 0x00a;
      rsrc2 = 0x00b;
      patch_esgs = false;
   } else if (metadata->hardware_stage == PSBC_HW_STAGE_VERTEX &&
              metadata->source_stage == PSBC_STAGE_VERTEX &&
              !metadata->input_semantic_count && has_linkage) {
      agc_stage = 0;
      pgm_lo = 0x048;
      pgm_hi = 0x049;
      rsrc1 = 0x04a;
      rsrc2 = 0x04b;
      patch_esgs = false;
   } else if (metadata->hardware_stage == PSBC_HW_STAGE_NGG &&
              (metadata->source_stage == PSBC_STAGE_VERTEX ||
               metadata->source_stage == PSBC_STAGE_GEOMETRY) &&
              !metadata->input_semantic_count && metadata->linkage_valid) {
      agc_stage = 2;
      pgm_lo = 0x0c8;
      pgm_hi = 0x0c9;
      rsrc1 = 0x08a;
      rsrc2 = 0x08b;
      patch_esgs = true;
      /* NGG without an API GS consumes unscaled vertex indices. Passthrough
       * ignores this register, but non-passthrough multiplies each index by
       * it before NIR uses it for primitive exports and LDS addressing. */
      if (metadata->source_stage == PSBC_STAGE_VERTEX)
         esgs_ring_itemsize = 1;
      if (metadata->source_stage == PSBC_STAGE_GEOMETRY) {
         linkage_primitive_override = find_register(
            context, metadata->context_register_count, UINT16_C(0x29b));
         if (!linkage_primitive_override ||
             linkage_primitive_override->value > 2u)
            return -3;
      }
   } else {
      return -3;
   }
   if (!register_pair_valid(shader_registers,
                            metadata->shader_register_count,
                            pgm_lo, pgm_hi, true) ||
       !register_pair_valid(shader_registers,
                            metadata->shader_register_count,
                            rsrc1, rsrc2, false))
      return -4;
   if (has_linkage &&
       (metadata->linkage_ge_cntl.offset != 0x25b ||
        metadata->linkage_stages_en.offset != 0x2d5 ||
        metadata->linkage_user_vgpr_en.offset != 0x262))
      return -5;

   context_at = shader_at + metadata->shader_register_count * 8u;
   linkage_at = align_to(context_at + metadata->context_register_count * 8u,
                         8u);
   cursor = linkage_at + (has_linkage ? 48u : 0u);
   input_at = metadata->input_semantic_count ? cursor : 0u;
   cursor += metadata->input_semantic_count * 4u;
   output_at = metadata->output_semantic_count ? cursor : 0u;
   cursor += metadata->output_semantic_count * 4u;
   resource_at = align_to(cursor, 8u);
   header_size = resource_at + RESOURCE_LAYOUT_SIZE;
   header_at = align_to(text_at + shader->machine_code_size, 8u);
   names_at = header_at + header_size;
   sections_at = align_to(names_at + sizeof(names), 8u);
   total_size = sections_at + 4u * ELF_SECTION_SIZE;
   data = calloc(1, total_size);
   if (!data)
      return -6;
   header = data + header_at;

   memcpy(data, "\177ELF", 4);
   data[4] = 2;
   data[5] = 1;
   data[6] = 1;
   put16(data, 16, 2);
   put16(data, 18, ELF_MACHINE_AMDGPU);
   put32(data, 20, 1);
   put64(data, 40, sections_at);
   put16(data, 52, ELF_HEADER_SIZE);
   put16(data, 54, 56);
   put16(data, 58, ELF_SECTION_SIZE);
   put16(data, 60, 4);
   put16(data, 62, 3);
   memcpy(data + text_at, shader->machine_code, shader->machine_code_size);
   memcpy(data + names_at, names, sizeof(names));

   put32(header, 0, AGC_MAGIC);
   put32(header, 4, 24);
   put64(header, 8, resource_at - 8u);
   put64(header, 24,
         metadata->context_register_count ? context_at - 24u : 0u);
   put64(header, 32, shader_at - 32u);
   put64(header, 40, has_linkage ? linkage_at - 40u : 0u);
   put64(header, 48, input_at ? input_at - 48u : 0u);
   put64(header, 56, output_at ? output_at - 56u : 0u);
   put32(header, 64, (uint32_t)header_size);
   put32(header, 68, (uint32_t)shader->machine_code_size);
   put32(header, 80, metadata->input_semantic_count);
   put16(header, 86, (uint16_t)metadata->output_semantic_count);
   header[90] = agc_stage;
   header[91] = (uint8_t)metadata->context_register_count;
   header[92] = (uint8_t)metadata->shader_register_count;

   for (uint32_t i = 0; i < metadata->shader_register_count; ++i)
      write_register(header, shader_at + i * 8u, &shader_registers[i],
                     false, esgs_ring_itemsize);
   for (uint32_t i = 0; i < metadata->context_register_count; ++i)
      write_register(header, context_at + i * 8u, &context[i], patch_esgs,
                     esgs_ring_itemsize);
   if (has_linkage) {
      write_register(header, linkage_at, &metadata->linkage_ge_cntl,
                     false, 0);
      write_register(header, linkage_at + 8u,
                     &metadata->linkage_stages_en, false, 0);
      /* libSceAgc selects this slot when GS_EN is set instead of deriving the
       * output primitive from the input draw topology. */
      if (linkage_primitive_override)
         write_register(header, linkage_at + 32u,
                        linkage_primitive_override, false, 0);
      write_register(header, linkage_at + 40u,
                     &metadata->linkage_user_vgpr_en, false, 0);
   }
   for (uint32_t i = 0; i < metadata->input_semantic_count; ++i)
      put32(header, input_at + i * 4u, metadata->input_semantics[i]);
   for (uint32_t i = 0; i < metadata->output_semantic_count; ++i)
      put32(header, output_at + i * 4u, metadata->output_semantics[i]);

   write_section(data, sections_at, 0, 0, 0, 0, 0, 0, 0);
   write_section(data, sections_at, 1, 1, 1, 0x6, text_at,
                 shader->machine_code_size, 256);
   write_section(data, sections_at, 2, 14, 1, 0x3, header_at,
                 header_size, 8);
   write_section(data, sections_at, 3, 29, 3, 0, names_at,
                 sizeof(names), 1);

   *package = data;
   *package_size = total_size;
   return 0;
}
