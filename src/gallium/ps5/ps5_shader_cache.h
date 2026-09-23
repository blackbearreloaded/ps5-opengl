// SPDX-License-Identifier: GPL-3.0-or-later
/* Optional native compiler cache. The application supplies a private writable
 * directory through PS5_SHADER_CACHE_DIR. Missing/unwritable caches are misses. */
#include "compiler/nir/nir_serialize.h"
#include "util/mesa-blake3.h"
#include <unistd.h>

#ifdef PS5_SHADER_CACHE_BUILD_ID
/* ponytail: direct-mapped slots bound disk use to 64 MiB. Collisions recompile;
 * add associative eviction only if measured collision misses justify it. */
#define PS5_SHADER_CACHE_SLOTS 1024u
#define PS5_SHADER_CACHE_LIMIT (64u * 1024u)
struct ps5_shader_cache_record {
   uint8_t key[BLAKE3_KEY_LEN];
   uint8_t digest[BLAKE3_KEY_LEN];
   uint32_t wrapper_size, code_size;
   PsbcShaderMetadata metadata;
};

static void
ps5_shader_cache_digest(const struct ps5_shader_cache_record *record,
                        const PsbcShaderOutput *output, uint8_t *digest)
{
   blake3_hasher hash;
   _mesa_blake3_init(&hash);
   _mesa_blake3_update(&hash, &record->wrapper_size,
                       sizeof(*record) - offsetof(struct ps5_shader_cache_record, wrapper_size));
   _mesa_blake3_update(&hash, output->data, output->size);
   _mesa_blake3_update(&hash, output->machine_code, output->machine_code_size);
   _mesa_blake3_final(&hash, digest);
}

static bool
ps5_shader_cache_read(const char *path, const uint8_t *key, PsbcShaderOutput *out)
{
   struct ps5_shader_cache_record record;
   PsbcShaderOutput cached = {0};
   uint8_t digest[BLAKE3_KEY_LEN];
   FILE *file = fopen(path, "rb");
   if (!file)
      return false;
   bool valid = fread(&record, sizeof(record), 1, file) == 1 &&
      !memcmp(record.key, key, sizeof(record.key)) && record.code_size &&
      record.wrapper_size <= PS5_SHADER_CACHE_LIMIT - sizeof(record) &&
      record.code_size <= PS5_SHADER_CACHE_LIMIT - sizeof(record) - record.wrapper_size;
   if (valid) {
      cached.size = record.wrapper_size;
      cached.machine_code_size = record.code_size;
      cached.metadata = record.metadata;
      cached.data = malloc(cached.size ? cached.size : 1);
      cached.machine_code = malloc(cached.machine_code_size);
      valid = cached.data && cached.machine_code &&
         fread(cached.data, 1, cached.size, file) == cached.size &&
         fread(cached.machine_code, 1, cached.machine_code_size, file) == cached.machine_code_size &&
         fgetc(file) == EOF && !ferror(file);
      if (valid) {
         ps5_shader_cache_digest(&record, &cached, digest);
         valid = !memcmp(record.digest, digest, sizeof(digest));
      }
   }
   fclose(file);
   if (!valid) {
      psbc_free_output(&cached);
      return false;
   }
   *out = cached;
   return true;
}

static void
ps5_shader_cache_write(const char *path, const uint8_t *key, const PsbcShaderOutput *out)
{
   struct ps5_shader_cache_record record = {0};
   if (!out->machine_code_size || out->size > PS5_SHADER_CACHE_LIMIT - sizeof(record) ||
       out->machine_code_size > PS5_SHADER_CACHE_LIMIT - sizeof(record) - out->size)
      return;
   memcpy(record.key, key, sizeof(record.key));
   record.wrapper_size = out->size;
   record.code_size = out->machine_code_size;
   record.metadata = out->metadata;
   ps5_shader_cache_digest(&record, out, record.digest);
   char temporary[4096];
   if (snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path) >= (int)sizeof(temporary))
      return;
   int fd = mkstemp(temporary);
   if (fd < 0)
      return;
   FILE *file = fdopen(fd, "wb");
   if (!file) {
      close(fd);
      unlink(temporary);
      return;
   }
   bool written = fwrite(&record, sizeof(record), 1, file) == 1 &&
      fwrite(out->data, 1, out->size, file) == out->size &&
      fwrite(out->machine_code, 1, out->machine_code_size, file) == out->machine_code_size;
   if (fclose(file))
      written = false;
   if (written)
      rename(temporary, path); /* Atomic replacement; readers never see partial records. */
   unlink(temporary);
}
#endif

static PsbcResult
ps5_compile_cached_nir(const nir_shader *nir, const PsbcCompileOptions *options,
                       PsbcShaderOutput *out)
{
#ifdef PS5_SHADER_CACHE_BUILD_ID
   const char *directory = getenv("PS5_SHADER_CACHE_DIR");
   if (directory && directory[0]) {
      struct blob blob;
      uint8_t key[BLAKE3_KEY_LEN];
      char path[4096];
      /* Callers zero-initialize the complete options, including inactive slots.
       * Encode string contents instead of the process-specific pointer. */
      PsbcCompileOptions canonical;
      memcpy(&canonical, options, sizeof(canonical));
      canonical.entrypoint = NULL;
      blob_init(&blob);
      blob_write_bytes(&blob, PS5_SHADER_CACHE_BUILD_ID, sizeof(PS5_SHADER_CACHE_BUILD_ID));
      blob_write_bytes(&blob, &canonical, sizeof(canonical));
      blob_write_string(&blob, options->entrypoint ? options->entrypoint : "main");
      nir_serialize(&blob, nir, true);
      bool valid = !blob.out_of_memory;
      if (valid)
         _mesa_blake3_compute(blob.data, blob.size, key);
      blob_finish(&blob);
      if (valid && snprintf(path, sizeof(path), "%s/%03x.bin", directory,
                           (key[0] | (unsigned)key[1] << 8) % PS5_SHADER_CACHE_SLOTS) < (int)sizeof(path)) {
         if (ps5_shader_cache_read(path, key, out)) {
            printf("[ps5-shader-cache] hit stage=%u bytes=%zu\n", options->stage, out->machine_code_size);
            return PSBC_RESULT_OK;
         }
         PsbcResult result = psbc_compile_nir(nir, options, out);
         if (result == PSBC_RESULT_OK)
            ps5_shader_cache_write(path, key, out);
         printf("[ps5-shader-cache] miss stage=%u result=%d\n", options->stage, result);
         return result;
      }
   }
#endif
   return psbc_compile_nir(nir, options, out);
}
