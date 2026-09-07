#ifndef PS5_SCREEN_H
#define PS5_SCREEN_H

struct pipe_screen;
struct pipe_context;
struct pipe_resource;

#include <stddef.h>

#define PS5_RENDER_WIDTH 1920u
#define PS5_RENDER_HEIGHT 1080u
#define PS5_MAX_RENDER_SIZE 8192u
#define PS5_MULTIDRAW_BATCH_CAPACITY 8u

struct pipe_screen *ps5_screen_create(void);
void ps5_screen_submit_lock(struct pipe_screen *screen);
void ps5_screen_submit_unlock(struct pipe_screen *screen);
void ps5_context_queue_present(struct pipe_context *context, unsigned buffer_index);
int ps5_context_last_draw_status(struct pipe_context *context,
                                 unsigned *draw_calls);
int ps5_shader_state_info(void *state, size_t *machine_code_size,
                          unsigned *hardware_stage,
                          unsigned *unresolved_fields);
int ps5_resource_info(struct pipe_resource *resource, void **address,
                      size_t *logical_size, size_t *allocation_size);
int ps5_resource_stencil_info(struct pipe_resource *resource, void **address,
                              size_t *allocation_size);
struct pipe_resource *ps5_display_target_alias(struct pipe_resource *owner,
                                               unsigned buffer_index);

#endif
