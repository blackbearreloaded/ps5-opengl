#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "pipe/p_context.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "ps5_screen.h"

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

int
main(void)
{
   struct pipe_resource templ;
   struct pipe_resource *resource = NULL;
   struct pipe_transfer *transfer = NULL;
   struct pipe_fence_handle *fence = NULL;
   struct pipe_context *context = NULL;
   struct pipe_screen *screen = ps5_screen_create();
   struct pipe_box box = {
      .x = 128, .width = 256, .y = 0, .height = 1, .z = 0, .depth = 1,
   };
   struct pipe_box invalid = {
      .x = 4000, .width = 128, .y = 0, .height = 1, .z = 0, .depth = 1,
   };
   uint8_t expected[256];
   uint8_t *mapped;
   uint32_t expected_hash;
   uint32_t actual_hash;
   uintptr_t direct_base = 0;
   unsigned i;
   const char *stage = "screen_create";
   int result = 1;

   if (!screen)
      goto done;

   printf("[ps5-gallium] screen name=%s vendor=%s device=%s graphics=%u accelerated=%d\n",
          screen->get_name(screen), screen->get_vendor(screen),
          screen->get_device_vendor(screen), screen->caps.graphics,
          screen->caps.accelerated);

   stage = "capability_guard";
   if (screen->caps.graphics ||
       screen->is_format_supported(screen, PIPE_FORMAT_B8G8R8A8_UNORM,
                                   PIPE_TEXTURE_2D, 1, 1,
                                   PIPE_BIND_RENDER_TARGET))
      goto done;

   stage = "context_create";
   context = screen->context_create(screen, NULL, 0);
   if (!context)
      goto done;

   memset(&templ, 0, sizeof(templ));
   templ.width0 = 4096;
   templ.height0 = 1;
   templ.depth0 = 1;
   templ.array_size = 1;
   templ.format = PIPE_FORMAT_R8_UNORM;
   templ.target = PIPE_BUFFER;
   templ.usage = PIPE_USAGE_DEFAULT;
   templ.bind = PIPE_BIND_VERTEX_BUFFER;

   stage = "resource_validate";
   if (!screen->can_create_resource(screen, &templ))
      goto done;
   stage = "resource_create";
   resource = screen->resource_create(screen, &templ);
   if (!resource)
      goto done;

   for (i = 0; i < sizeof(expected); ++i)
      expected[i] = (uint8_t)(i ^ 0x5a);
   expected_hash = fnv1a(expected, sizeof(expected));

   stage = "write_map";
   mapped = context->buffer_map(context, resource, 0, PIPE_MAP_WRITE, &box,
                                &transfer);
   if (!mapped || !transfer)
      goto done;
   direct_base = (uintptr_t)mapped - (uintptr_t)box.x;
   if (direct_base & (uintptr_t)0x3fff)
      goto done;
   memcpy(mapped, expected, sizeof(expected));
   context->buffer_unmap(context, transfer);
   transfer = NULL;

   stage = "read_map";
   mapped = context->buffer_map(context, resource, 0, PIPE_MAP_READ, &box,
                                &transfer);
   if (!mapped || !transfer)
      goto done;
   actual_hash = fnv1a(mapped, sizeof(expected));
   context->buffer_unmap(context, transfer);
   transfer = NULL;
   stage = "hash_compare";
   if (actual_hash != expected_hash)
      goto done;

   stage = "invalid_map";
   mapped = context->buffer_map(context, resource, 0, PIPE_MAP_READ, &invalid,
                                &transfer);
   if (mapped || transfer)
      goto done;

   stage = "flush_fence";
   context->flush(context, &fence, 0);
   if (!fence || !screen->fence_finish(screen, context, fence, 0))
      goto done;

   printf("[ps5-gallium] resource bytes=%u direct_base=%016" PRIxPTR
          " alignment=0x4000 map_offset=%d map_bytes=%d hash=%08" PRIx32
          " invalid_map=rejected fence=signaled contexts=%u\n",
          templ.width0, direct_base, box.x, box.width, actual_hash,
          screen->num_contexts);
   result = 0;
   stage = "complete";

done:
   if (transfer && context)
      context->buffer_unmap(context, transfer);
   if (fence && screen)
      screen->fence_reference(screen, &fence, NULL);
   if (resource && screen)
      screen->resource_destroy(screen, resource);
   if (context)
      context->destroy(context);
   if (screen) {
      printf("[ps5-gallium] result=%d stage=%s contexts_after_destroy=%u\n",
             result, stage, screen->num_contexts);
      screen->destroy(screen);
   }
   return result;
}
