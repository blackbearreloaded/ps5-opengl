#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>

#include "frontend/api.h"
#include "glapi/glapi/glapi.h"
#include "main/context.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"
#include "ps5_screen.h"
#include "state_tracker/st_context.h"
#include "util/u_atomic.h"
#include "util/u_inlines.h"
#include "util/simple_mtx.h"

#define PS5_EGL_CONTEXT_MAGIC UINT32_C(0x50454358)
#define PS5_EGL_SURFACE_MAGIC UINT32_C(0x50455346)
#define PS5_EGL_WIDTH 1920
#define PS5_EGL_HEIGHT 1080
#ifndef PS5_ENABLE_COMPRESSED_FALLBACK_CANDIDATE
#define PS5_ENABLE_COMPRESSED_FALLBACK_CANDIDATE 0
#endif
#ifndef PS5_ENABLE_CORE_CONTEXT_CANDIDATE
#define PS5_ENABLE_CORE_CONTEXT_CANDIDATE 0
#endif

struct ps5_egl_display {
   struct pipe_frontend_screen frontend;
   struct pipe_screen *screen;
   bool initialized;
   unsigned contexts;
   unsigned surfaces;
   unsigned next_drawable_id;
   struct pipe_resource *scanout[2];
};

struct ps5_egl_config {
   struct st_visual visual;
};

struct ps5_egl_surface {
   struct pipe_frontend_drawable drawable;
   uint32_t magic;
   struct st_visual visual;
   struct pipe_resource *targets[2];
   struct pipe_resource *depth_stencil;
   unsigned buffer_index;
   EGLint width;
   EGLint height;
   EGLint swap_interval;
   bool window;
   bool used;
   bool current;
   struct ps5_egl_surface *next;
};

_Static_assert(offsetof(struct ps5_egl_surface, drawable) == 0,
               "drawable callback casts require a leading base member");

struct ps5_egl_context {
   uint32_t magic;
   struct st_context *st;
   EGLint config_id;
   int major;
   int minor;
   EGLint profile_mask;
   bool current;
   struct ps5_egl_context *next;
};

static struct ps5_egl_display ps5_display;
static struct ps5_egl_config ps5_config;
static _Thread_local EGLint ps5_last_error = EGL_SUCCESS;
static _Thread_local EGLenum ps5_bound_api = EGL_OPENGL_ES_API;
static _Thread_local struct ps5_egl_context *ps5_current_context;
static _Thread_local struct ps5_egl_surface *ps5_current_draw;
static _Thread_local struct ps5_egl_surface *ps5_current_read;
static struct ps5_egl_context *ps5_contexts;
static struct ps5_egl_surface *ps5_surfaces;
static struct ps5_egl_surface *ps5_window_surface;
static simple_mtx_t ps5_egl_mutex = SIMPLE_MTX_INITIALIZER;

struct ps5_egl_lock_guard {
   simple_mtx_t *mutex;
};

static void
ps5_egl_unlock(struct ps5_egl_lock_guard *guard)
{
   simple_mtx_unlock(guard->mutex);
}

#define PS5_EGL_LOCK() \
   simple_mtx_lock(&ps5_egl_mutex); \
   struct ps5_egl_lock_guard ps5_egl_guard \
      __attribute__((cleanup(ps5_egl_unlock))) = { &ps5_egl_mutex }

int ps5_agc_gate2_present(unsigned buffer_index) __attribute__((weak));
int ps5_agc_gate2_shutdown_present(void) __attribute__((weak));
int sceSystemServiceHideSplashScreen(void);

int
ps5_egl_current_draw_status(unsigned *draw_calls)
{
   if (!ps5_current_context || !ps5_current_context->st ||
       !ps5_current_context->st->pipe)
      return -100;
   return ps5_context_last_draw_status(ps5_current_context->st->pipe,
                                       draw_calls);
}

static void
ps5_set_error(EGLint error)
{
   ps5_last_error = error;
}

static bool
ps5_valid_display(EGLDisplay display, bool initialized)
{
   return display == (EGLDisplay)&ps5_display &&
          (!initialized || ps5_display.initialized);
}

static bool
ps5_valid_config(EGLConfig config)
{
   return config == (EGLConfig)&ps5_config;
}

static struct ps5_egl_surface *
ps5_surface(EGLSurface surface)
{
   for (struct ps5_egl_surface *value = ps5_surfaces;
        value; value = value->next) {
      if ((EGLSurface)value == surface &&
          value->magic == PS5_EGL_SURFACE_MAGIC)
         return value;
   }
   return NULL;
}

static struct ps5_egl_context *
ps5_context(EGLContext context)
{
   for (struct ps5_egl_context *value = ps5_contexts;
        value; value = value->next) {
      if ((EGLContext)value == context &&
          value->magic == PS5_EGL_CONTEXT_MAGIC)
         return value;
   }
   return NULL;
}

static void
ps5_clear_current(void)
{
   if (ps5_current_context)
      ps5_current_context->current = false;
   if (ps5_current_draw)
      ps5_current_draw->current = false;
   if (ps5_current_read)
      ps5_current_read->current = false;
   ps5_current_context = NULL;
   ps5_current_draw = NULL;
   ps5_current_read = NULL;
}

static int
ps5_frontend_get_param(struct pipe_frontend_screen *screen,
                       enum st_manager_param param)
{
   (void)screen;
   (void)param;
   return 0;
}

static bool
ps5_flush_front(struct st_context *st,
                struct pipe_frontend_drawable *drawable,
                enum st_attachment_type attachment)
{
   (void)st;
   (void)drawable;
   return attachment == ST_ATTACHMENT_FRONT_LEFT;
}

static bool
ps5_validate_drawable(struct st_context *st,
                      struct pipe_frontend_drawable *drawable,
                      const enum st_attachment_type *attachments,
                      unsigned count, struct pipe_resource **out,
                      struct pipe_resource **resolve)
{
   struct ps5_egl_surface *surface = (struct ps5_egl_surface *)drawable;

   (void)st;
   for (unsigned i = 0; i < count; ++i) {
      struct pipe_resource *resource;

      if (resolve)
         resolve[i] = NULL;
      if (attachments[i] == ST_ATTACHMENT_FRONT_LEFT)
         resource = surface->targets[surface->buffer_index];
      else if (attachments[i] == ST_ATTACHMENT_DEPTH_STENCIL)
         resource = surface->depth_stencil;
      else
         return false;
      pipe_resource_reference(&out[i], resource);
   }
   return true;
}

static bool
ps5_flush_swapbuffers(struct st_context *st,
                      struct pipe_frontend_drawable *drawable)
{
   (void)st;
   (void)drawable;
   return true;
}

static void
ps5_init_surface(struct ps5_egl_surface *surface, EGLint width,
                 EGLint height, bool window)
{
   surface->magic = PS5_EGL_SURFACE_MAGIC;
   surface->width = width;
   surface->height = height;
   surface->swap_interval = 1;
   surface->window = window;
   surface->visual = ps5_config.visual;
   surface->drawable.stamp = 1;
   surface->drawable.ID = ps5_display.next_drawable_id++;
   surface->drawable.fscreen = &ps5_display.frontend;
   surface->drawable.visual = &surface->visual;
   surface->drawable.flush_front = ps5_flush_front;
   surface->drawable.validate = ps5_validate_drawable;
   surface->drawable.flush_swapbuffers = ps5_flush_swapbuffers;
   surface->next = ps5_surfaces;
   ps5_surfaces = surface;
   ps5_display.surfaces++;
}

static struct pipe_resource *
ps5_create_depth_stencil(EGLint width, EGLint height)
{
   struct pipe_resource resource;

   memset(&resource, 0, sizeof(resource));
   resource.target = PIPE_TEXTURE_2D;
   resource.format = PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
   resource.width0 = width;
   resource.height0 = height;
   resource.depth0 = 1;
   resource.array_size = 1;
   resource.nr_samples = 1;
   resource.nr_storage_samples = 1;
   resource.bind = PIPE_BIND_DEPTH_STENCIL;
   return ps5_display.screen->resource_create(ps5_display.screen, &resource);
}

static bool
ps5_match_config(const EGLint *attributes, bool *bad_attribute)
{
   if (!attributes)
      return true;

   while (*attributes != EGL_NONE) {
      EGLint name = *attributes++;
      EGLint value = *attributes++;

      switch (name) {
      case EGL_RED_SIZE:
      case EGL_GREEN_SIZE:
      case EGL_BLUE_SIZE:
      case EGL_ALPHA_SIZE:
         if (value != EGL_DONT_CARE && value > 8)
            return false;
         break;
      case EGL_BUFFER_SIZE:
         if (value != EGL_DONT_CARE && value > 32)
            return false;
         break;
      case EGL_DEPTH_SIZE:
         if (value != EGL_DONT_CARE && value > 32)
            return false;
         break;
      case EGL_STENCIL_SIZE:
         if (value != EGL_DONT_CARE && value > 8)
            return false;
         break;
      case EGL_SAMPLE_BUFFERS:
      case EGL_SAMPLES:
      case EGL_LEVEL:
         if (value != EGL_DONT_CARE && value > 0)
            return false;
         break;
      case EGL_SURFACE_TYPE:
         if (value != EGL_DONT_CARE &&
             (value & ~(EGL_WINDOW_BIT | EGL_PBUFFER_BIT)))
            return false;
         break;
      case EGL_RENDERABLE_TYPE:
         if (value != EGL_DONT_CARE && (value & ~EGL_OPENGL_BIT))
            return false;
         break;
      case EGL_CONFORMANT:
         if (value != EGL_DONT_CARE && value != 0)
            return false;
         break;
      case EGL_CONFIG_ID:
         if (value != EGL_DONT_CARE && value != 1)
            return false;
         break;
      case EGL_CONFIG_CAVEAT:
         if (value != EGL_DONT_CARE && value != EGL_NON_CONFORMANT_CONFIG)
            return false;
         break;
      case EGL_NATIVE_RENDERABLE:
         if (value != EGL_DONT_CARE && value != EGL_FALSE)
            return false;
         break;
      case EGL_TRANSPARENT_TYPE:
         if (value != EGL_DONT_CARE && value != EGL_NONE)
            return false;
         break;
      default:
         *bad_attribute = true;
         return false;
      }
   }
   return true;
}

EGLAPI EGLDisplay EGLAPIENTRY
eglGetDisplay(EGLNativeDisplayType display_id)
{
   if ((uintptr_t)display_id != 0) {
      ps5_set_error(EGL_BAD_PARAMETER);
      return EGL_NO_DISPLAY;
   }
   return (EGLDisplay)&ps5_display;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglInitialize(EGLDisplay display, EGLint *major, EGLint *minor)
{
   PS5_EGL_LOCK();

   if (!ps5_valid_display(display, false)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_FALSE;
   }
   if (!ps5_display.initialized) {
      ps5_display.screen = ps5_screen_create();
      if (!ps5_display.screen) {
         ps5_set_error(EGL_BAD_ALLOC);
         return EGL_FALSE;
      }
      memset(&ps5_display.frontend, 0, sizeof(ps5_display.frontend));
      ps5_display.frontend.screen = ps5_display.screen;
      ps5_display.frontend.get_param = ps5_frontend_get_param;
      ps5_display.next_drawable_id = 1;

      struct pipe_resource resource;

      memset(&resource, 0, sizeof(resource));
      resource.target = PIPE_TEXTURE_2D;
      resource.format = PIPE_FORMAT_R8G8B8A8_UNORM;
      resource.width0 = PS5_EGL_WIDTH;
      resource.height0 = PS5_EGL_HEIGHT;
      resource.depth0 = 1;
      resource.array_size = 1;
      resource.nr_samples = 1;
      resource.nr_storage_samples = 1;
      resource.bind = PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET;
      ps5_display.scanout[0] = ps5_display.screen->resource_create(
         ps5_display.screen, &resource);
      if (ps5_display.scanout[0])
         ps5_display.scanout[1] = ps5_display_target_alias(
            ps5_display.scanout[0], 1);
      if (!ps5_display.scanout[0] || !ps5_display.scanout[1]) {
         pipe_resource_reference(&ps5_display.scanout[1], NULL);
         pipe_resource_reference(&ps5_display.scanout[0], NULL);
         ps5_display.screen->destroy(ps5_display.screen);
         memset(&ps5_display, 0, sizeof(ps5_display));
         ps5_set_error(EGL_BAD_ALLOC);
         return EGL_FALSE;
      }

      memset(&ps5_config, 0, sizeof(ps5_config));
      ps5_config.visual.buffer_mask = ST_ATTACHMENT_FRONT_LEFT_MASK |
                                      ST_ATTACHMENT_DEPTH_STENCIL_MASK;
      ps5_config.visual.color_format = PIPE_FORMAT_R8G8B8A8_UNORM;
      ps5_config.visual.depth_stencil_format =
         PIPE_FORMAT_Z32_FLOAT_S8X24_UINT;
      ps5_config.visual.accum_format = PIPE_FORMAT_NONE;
      ps5_config.visual.samples = 1;
      ps5_display.initialized = true;
   }
   if (major)
      *major = 1;
   if (minor)
      *minor = 4;
   return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglTerminate(EGLDisplay display)
{
   PS5_EGL_LOCK();

   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_FALSE;
   }
   if (ps5_display.contexts || ps5_display.surfaces) {
      ps5_set_error(EGL_BAD_ACCESS);
      return EGL_FALSE;
   }
   if (ps5_agc_gate2_shutdown_present) {
      ps5_screen_submit_lock(ps5_display.screen);
      ps5_agc_gate2_shutdown_present();
      ps5_screen_submit_unlock(ps5_display.screen);
   }
   pipe_resource_reference(&ps5_display.scanout[1], NULL);
   pipe_resource_reference(&ps5_display.scanout[0], NULL);
   st_screen_destroy(&ps5_display.frontend);
   ps5_display.screen->destroy(ps5_display.screen);
   memset(&ps5_display, 0, sizeof(ps5_display));
   return EGL_TRUE;
}

EGLAPI EGLint EGLAPIENTRY
eglGetError(void)
{
   EGLint error = ps5_last_error;
   ps5_last_error = EGL_SUCCESS;
   return error;
}

EGLAPI const char *EGLAPIENTRY
eglQueryString(EGLDisplay display, EGLint name)
{
   PS5_EGL_LOCK();

   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return NULL;
   }
   switch (name) {
   case EGL_VENDOR:
      return "PS5 Mesa experimental";
   case EGL_VERSION:
      return "1.4 PS5 experimental";
   case EGL_EXTENSIONS:
      return PS5_ENABLE_CORE_CONTEXT_CANDIDATE ?
         "EGL_KHR_create_context EGL_KHR_no_config_context "
         "EGL_KHR_surfaceless_context" : "";
   case EGL_CLIENT_APIS:
      return "OpenGL";
   default:
      ps5_set_error(EGL_BAD_PARAMETER);
      return NULL;
   }
}

EGLAPI EGLBoolean EGLAPIENTRY
eglGetConfigs(EGLDisplay display, EGLConfig *configs, EGLint config_size,
              EGLint *num_config)
{
   PS5_EGL_LOCK();

   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_FALSE;
   }
   if (!num_config || config_size < 0) {
      ps5_set_error(EGL_BAD_PARAMETER);
      return EGL_FALSE;
   }
   *num_config = 1;
   if (configs && config_size)
      configs[0] = (EGLConfig)&ps5_config;
   return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglChooseConfig(EGLDisplay display, const EGLint *attributes,
                EGLConfig *configs, EGLint config_size, EGLint *num_config)
{
   PS5_EGL_LOCK();

   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_FALSE;
   }
   if (!num_config || config_size < 0) {
      ps5_set_error(EGL_BAD_PARAMETER);
      return EGL_FALSE;
   }
   bool bad_attribute = false;
   bool match = ps5_match_config(attributes, &bad_attribute);
   if (bad_attribute) {
      ps5_set_error(EGL_BAD_ATTRIBUTE);
      return EGL_FALSE;
   }
   *num_config = match ? 1 : 0;
   if (match && configs && config_size)
      configs[0] = (EGLConfig)&ps5_config;
   return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglGetConfigAttrib(EGLDisplay display, EGLConfig config, EGLint attribute,
                   EGLint *value)
{
   PS5_EGL_LOCK();

   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_FALSE;
   }
   if (!ps5_valid_config(config)) {
      ps5_set_error(EGL_BAD_CONFIG);
      return EGL_FALSE;
   }
   if (!value) {
      ps5_set_error(EGL_BAD_PARAMETER);
      return EGL_FALSE;
   }
   switch (attribute) {
   case EGL_BUFFER_SIZE: *value = 32; break;
   case EGL_RED_SIZE:
   case EGL_GREEN_SIZE:
   case EGL_BLUE_SIZE:
   case EGL_ALPHA_SIZE: *value = 8; break;
   case EGL_DEPTH_SIZE: *value = 32; break;
   case EGL_STENCIL_SIZE: *value = 8; break;
   case EGL_SAMPLE_BUFFERS:
   case EGL_SAMPLES:
   case EGL_LEVEL:
   case EGL_NATIVE_VISUAL_ID:
   case EGL_NATIVE_VISUAL_TYPE:
   case EGL_TRANSPARENT_RED_VALUE:
   case EGL_TRANSPARENT_GREEN_VALUE:
   case EGL_TRANSPARENT_BLUE_VALUE:
   case EGL_BIND_TO_TEXTURE_RGB:
   case EGL_BIND_TO_TEXTURE_RGBA:
   case EGL_LUMINANCE_SIZE:
   case EGL_ALPHA_MASK_SIZE:
   case EGL_CONFORMANT: *value = 0; break;
   case EGL_MAX_PBUFFER_WIDTH: *value = PS5_EGL_WIDTH; break;
   case EGL_MAX_PBUFFER_HEIGHT: *value = PS5_EGL_HEIGHT; break;
   case EGL_MAX_PBUFFER_PIXELS: *value = PS5_EGL_WIDTH * PS5_EGL_HEIGHT; break;
   case EGL_CONFIG_ID: *value = 1; break;
   case EGL_CONFIG_CAVEAT: *value = EGL_NON_CONFORMANT_CONFIG; break;
   case EGL_NATIVE_RENDERABLE: *value = EGL_FALSE; break;
   case EGL_RENDERABLE_TYPE: *value = EGL_OPENGL_BIT; break;
   case EGL_SURFACE_TYPE: *value = EGL_WINDOW_BIT | EGL_PBUFFER_BIT; break;
   case EGL_TRANSPARENT_TYPE: *value = EGL_NONE; break;
   case EGL_COLOR_BUFFER_TYPE: *value = EGL_RGB_BUFFER; break;
   case EGL_MIN_SWAP_INTERVAL: *value = 0; break;
   case EGL_MAX_SWAP_INTERVAL: *value = 1; break;
   default:
      ps5_set_error(EGL_BAD_ATTRIBUTE);
      return EGL_FALSE;
   }
   return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglBindAPI(EGLenum api)
{
   if (api != EGL_OPENGL_API) {
      ps5_set_error(EGL_BAD_PARAMETER);
      return EGL_FALSE;
   }
   ps5_bound_api = api;
   return EGL_TRUE;
}

EGLAPI EGLenum EGLAPIENTRY
eglQueryAPI(void)
{
   return ps5_bound_api;
}

EGLAPI EGLSurface EGLAPIENTRY
eglCreateWindowSurface(EGLDisplay display, EGLConfig config,
                       EGLNativeWindowType window, const EGLint *attributes)
{
   struct ps5_egl_surface *surface;

   PS5_EGL_LOCK();

   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_NO_SURFACE;
   }
   if (!ps5_valid_config(config)) {
      ps5_set_error(EGL_BAD_CONFIG);
      return EGL_NO_SURFACE;
   }
   if ((uintptr_t)window != 0) {
      ps5_set_error(EGL_BAD_NATIVE_WINDOW);
      return EGL_NO_SURFACE;
   }
   if (attributes && attributes[0] != EGL_NONE) {
      ps5_set_error(EGL_BAD_ATTRIBUTE);
      return EGL_NO_SURFACE;
   }
   if (ps5_window_surface) {
      ps5_set_error(EGL_BAD_ALLOC);
      return EGL_NO_SURFACE;
   }
   printf("[ps5-egl] hide-splash=%08x\n",
          (unsigned)sceSystemServiceHideSplashScreen());
   surface = calloc(1, sizeof(*surface));
   if (!surface) {
      ps5_set_error(EGL_BAD_ALLOC);
      return EGL_NO_SURFACE;
   }
   pipe_resource_reference(&surface->targets[0], ps5_display.scanout[0]);
   pipe_resource_reference(&surface->targets[1], ps5_display.scanout[1]);
   surface->depth_stencil =
      ps5_create_depth_stencil(PS5_EGL_WIDTH, PS5_EGL_HEIGHT);
   if (!surface->depth_stencil) {
      pipe_resource_reference(&surface->targets[1], NULL);
      pipe_resource_reference(&surface->targets[0], NULL);
      free(surface);
      ps5_set_error(EGL_BAD_ALLOC);
      return EGL_NO_SURFACE;
   }
   ps5_init_surface(surface, PS5_EGL_WIDTH, PS5_EGL_HEIGHT, true);
   ps5_window_surface = surface;
   return (EGLSurface)surface;
}

EGLAPI EGLContext EGLAPIENTRY
eglCreateContext(EGLDisplay display, EGLConfig config, EGLContext share,
                 const EGLint *attributes)
{
   struct st_context_attribs attribs;
   struct st_config_options options;
   enum st_context_error context_error = ST_CONTEXT_SUCCESS;
   struct ps5_egl_context *context;
   struct ps5_egl_context *shared = NULL;
   int core = 0, compat = 0, es1 = 0, es2 = 0;
   int major = 0, minor = 0;
   int requested_major = 0, requested_minor = 0;
   EGLint profile_mask = 0;
   unsigned context_flags = 0;

   PS5_EGL_LOCK();

   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_NO_CONTEXT;
   }
   if (!ps5_valid_config(config) &&
       !(PS5_ENABLE_CORE_CONTEXT_CANDIDATE &&
         config == EGL_NO_CONFIG_KHR)) {
      ps5_set_error(EGL_BAD_CONFIG);
      return EGL_NO_CONTEXT;
   }
   if (ps5_bound_api != EGL_OPENGL_API) {
      ps5_set_error(EGL_BAD_MATCH);
      return EGL_NO_CONTEXT;
   }
   if (share != EGL_NO_CONTEXT) {
      shared = ps5_context(share);
      if (!shared) {
         ps5_set_error(EGL_BAD_CONTEXT);
         return EGL_NO_CONTEXT;
      }
   }
   if (attributes && attributes[0] != EGL_NONE) {
      if (!PS5_ENABLE_CORE_CONTEXT_CANDIDATE) {
         ps5_set_error(EGL_BAD_ATTRIBUTE);
         return EGL_NO_CONTEXT;
      }
      while (*attributes != EGL_NONE) {
         EGLint name = *attributes++;
         EGLint value = *attributes++;

         switch (name) {
         case EGL_CONTEXT_MAJOR_VERSION_KHR:
            requested_major = value;
            break;
         case EGL_CONTEXT_MINOR_VERSION_KHR:
            requested_minor = value;
            break;
         case EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR:
            profile_mask = value;
            break;
         case EGL_CONTEXT_FLAGS_KHR:
            if (value & ~(EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR |
                          EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR)) {
               ps5_set_error(EGL_BAD_ATTRIBUTE);
               return EGL_NO_CONTEXT;
            }
            if (value & EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR)
               context_flags |= ST_CONTEXT_FLAG_DEBUG;
            if (value & EGL_CONTEXT_OPENGL_FORWARD_COMPATIBLE_BIT_KHR)
               context_flags |= ST_CONTEXT_FLAG_FORWARD_COMPATIBLE;
            break;
         default:
            ps5_set_error(EGL_BAD_ATTRIBUTE);
            return EGL_NO_CONTEXT;
         }
      }
   }
   memset(&options, 0, sizeof(options));
   options.allow_compressed_fallback =
      PS5_ENABLE_COMPRESSED_FALLBACK_CANDIDATE;
   st_api_query_versions(&ps5_display.frontend, &options, &core, &compat,
                         &es1, &es2);
#if PS5_ENABLE_CORE_CONTEXT_CANDIDATE
   printf("[ps5-egl] versions core=%d compat=%d requested=%d.%d profile=%04x\n",
          core, compat, requested_major, requested_minor, profile_mask);
#endif
   (void)es1;
   (void)es2;
   if (profile_mask == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR) {
      if (core <= 0 || requested_major < 3 ||
          (requested_major == 3 && requested_minor < 2)) {
         ps5_set_error(EGL_BAD_MATCH);
         return EGL_NO_CONTEXT;
      }
      major = core / 10;
      minor = core % 10;
   } else if (!profile_mask ||
              profile_mask == EGL_CONTEXT_OPENGL_COMPATIBILITY_PROFILE_BIT_KHR) {
      if (compat <= 0) {
         ps5_set_error(EGL_BAD_MATCH);
         return EGL_NO_CONTEXT;
      }
      major = compat / 10;
      minor = compat % 10;
   } else {
      ps5_set_error(EGL_BAD_MATCH);
      return EGL_NO_CONTEXT;
   }
   if (requested_major < 0 || requested_minor < 0 ||
       (requested_major && requested_major * 10 + requested_minor >
                              major * 10 + minor)) {
      ps5_set_error(EGL_BAD_MATCH);
      return EGL_NO_CONTEXT;
   }
   context = calloc(1, sizeof(*context));
   if (!context) {
      ps5_set_error(EGL_BAD_ALLOC);
      return EGL_NO_CONTEXT;
   }
   memset(&attribs, 0, sizeof(attribs));
   attribs.profile = profile_mask == EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT_KHR ?
                     API_OPENGL_CORE : API_OPENGL_COMPAT;
   attribs.major = requested_major ? requested_major : major;
   attribs.minor = requested_major ? requested_minor : minor;
   attribs.flags = context_flags;
   attribs.visual = ps5_config.visual;
   attribs.options = options;
   context->st = st_api_create_context(&ps5_display.frontend, &attribs,
                                       &context_error,
                                       shared ? shared->st : NULL);
   if (!context->st) {
      free(context);
      ps5_set_error(context_error == ST_CONTEXT_ERROR_NO_MEMORY ?
                    EGL_BAD_ALLOC : EGL_BAD_MATCH);
      return EGL_NO_CONTEXT;
   }
   context->magic = PS5_EGL_CONTEXT_MAGIC;
   context->config_id = config == EGL_NO_CONFIG_KHR ? 0 : 1;
   context->major = major;
   context->minor = minor;
   context->profile_mask = profile_mask;
   context->next = ps5_contexts;
   ps5_contexts = context;
   ps5_display.contexts++;
   return (EGLContext)context;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglMakeCurrent(EGLDisplay display, EGLSurface draw_handle,
               EGLSurface read_handle, EGLContext context_handle)
{
   const bool no_draw = draw_handle == EGL_NO_SURFACE;
   const bool no_read = read_handle == EGL_NO_SURFACE;
   struct ps5_egl_surface *draw;
   struct ps5_egl_surface *read;
   struct ps5_egl_context *context;

   PS5_EGL_LOCK();
   draw = ps5_surface(draw_handle);
   read = ps5_surface(read_handle);
   context = ps5_context(context_handle);

   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_FALSE;
   }
   if (context_handle == EGL_NO_CONTEXT && draw_handle == EGL_NO_SURFACE &&
       read_handle == EGL_NO_SURFACE) {
      if (!st_api_make_current(NULL, NULL, NULL)) {
         ps5_set_error(EGL_BAD_ACCESS);
         return EGL_FALSE;
      }
      ps5_clear_current();
      return EGL_TRUE;
   }
   if (!context) {
      ps5_set_error(EGL_BAD_CONTEXT);
      return EGL_FALSE;
   }
   if (no_draw != no_read) {
      ps5_set_error(EGL_BAD_MATCH);
      return EGL_FALSE;
   }
   if (no_draw) {
      if (!PS5_ENABLE_CORE_CONTEXT_CANDIDATE) {
         ps5_set_error(EGL_BAD_MATCH);
         return EGL_FALSE;
      }
      if (context->current && context != ps5_current_context) {
         ps5_set_error(EGL_BAD_ACCESS);
         return EGL_FALSE;
      }
      if (!st_api_make_current(context->st, NULL, NULL)) {
         ps5_set_error(EGL_BAD_MATCH);
         return EGL_FALSE;
      }
      ps5_clear_current();
      ps5_current_context = context;
      context->current = true;
      return EGL_TRUE;
   }
   if (!draw || !read) {
      ps5_set_error(EGL_BAD_SURFACE);
      return EGL_FALSE;
   }
   if ((context->current && context != ps5_current_context) ||
       (draw->current && draw != ps5_current_draw &&
        draw != ps5_current_read) ||
       (read->current && read != ps5_current_draw &&
        read != ps5_current_read)) {
      ps5_set_error(EGL_BAD_ACCESS);
      return EGL_FALSE;
   }
   if (!st_api_make_current(context->st, &draw->drawable, &read->drawable)) {
      ps5_set_error(EGL_BAD_MATCH);
      return EGL_FALSE;
   }
   draw->used = true;
   read->used = true;
   ps5_clear_current();
   ps5_current_context = context;
   ps5_current_draw = draw;
   ps5_current_read = read;
   context->current = true;
   draw->current = true;
   read->current = true;
   return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglSwapBuffers(EGLDisplay display, EGLSurface surface_handle)
{
   struct ps5_egl_surface *surface;
   struct pipe_fence_handle *fence = NULL;

   PS5_EGL_LOCK();
   surface = ps5_surface(surface_handle);

   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_FALSE;
   }
   if (!surface) {
      ps5_set_error(EGL_BAD_SURFACE);
      return EGL_FALSE;
   }
   if (!ps5_current_context || ps5_current_draw != surface) {
      ps5_set_error(EGL_BAD_CURRENT_SURFACE);
      return EGL_FALSE;
   }
   st_context_flush(ps5_current_context->st,
                    ST_FLUSH_FRONT | ST_FLUSH_END_OF_FRAME | ST_FLUSH_WAIT,
                    &fence, NULL, NULL);
   if (!surface->window)
      return EGL_TRUE;
   if (ps5_agc_gate2_present) {
      int present_status;

      ps5_screen_submit_lock(ps5_display.screen);
      present_status = ps5_agc_gate2_present(surface->buffer_index);
      ps5_screen_submit_unlock(ps5_display.screen);
      if (present_status != 0) {
         ps5_set_error(EGL_BAD_SURFACE);
         return EGL_FALSE;
      }
   }
   surface->buffer_index ^= 1u;
   p_atomic_inc(&surface->drawable.stamp);
   return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglSwapInterval(EGLDisplay display, EGLint interval)
{
   PS5_EGL_LOCK();

   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_FALSE;
   }
   if (!ps5_current_context || !ps5_current_draw) {
      ps5_set_error(ps5_current_context ? EGL_BAD_SURFACE : EGL_BAD_CONTEXT);
      return EGL_FALSE;
   }
   if (interval < 0 || interval > 1) {
      ps5_set_error(EGL_BAD_PARAMETER);
      return EGL_FALSE;
   }
   ps5_current_draw->swap_interval = interval;
   return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglDestroyContext(EGLDisplay display, EGLContext context_handle)
{
   struct ps5_egl_context *context;

   PS5_EGL_LOCK();
   context = ps5_context(context_handle);

   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_FALSE;
   }
   if (!context) {
      ps5_set_error(EGL_BAD_CONTEXT);
      return EGL_FALSE;
   }
   if (context->current) {
      ps5_set_error(EGL_BAD_ACCESS);
      return EGL_FALSE;
   }
   context->magic = 0;
   st_destroy_context(context->st);
   struct ps5_egl_context **link = &ps5_contexts;

   while (*link != context)
      link = &(*link)->next;
   *link = context->next;
   free(context);
   ps5_display.contexts--;
   return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglDestroySurface(EGLDisplay display, EGLSurface surface_handle)
{
   struct ps5_egl_surface *surface;

   PS5_EGL_LOCK();
   surface = ps5_surface(surface_handle);

   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_FALSE;
   }
   if (!surface) {
      ps5_set_error(EGL_BAD_SURFACE);
      return EGL_FALSE;
   }
   if (surface->current) {
      ps5_set_error(EGL_BAD_ACCESS);
      return EGL_FALSE;
   }
   surface->magic = 0;
   if (surface->window && ps5_agc_gate2_shutdown_present) {
      ps5_screen_submit_lock(ps5_display.screen);
      ps5_agc_gate2_shutdown_present();
      ps5_screen_submit_unlock(ps5_display.screen);
   }
   if (surface->used)
      st_api_destroy_drawable(&surface->drawable);
   pipe_resource_reference(&surface->depth_stencil, NULL);
   pipe_resource_reference(&surface->targets[1], NULL);
   pipe_resource_reference(&surface->targets[0], NULL);
   struct ps5_egl_surface **link = &ps5_surfaces;

   while (*link != surface)
      link = &(*link)->next;
   *link = surface->next;
   if (ps5_window_surface == surface)
      ps5_window_surface = NULL;
   free(surface);
   ps5_display.surfaces--;
   return EGL_TRUE;
}

EGLAPI EGLDisplay EGLAPIENTRY
eglGetCurrentDisplay(void)
{
   return ps5_current_context ? (EGLDisplay)&ps5_display : EGL_NO_DISPLAY;
}

EGLAPI EGLContext EGLAPIENTRY
eglGetCurrentContext(void)
{
   return (EGLContext)ps5_current_context;
}

EGLAPI EGLSurface EGLAPIENTRY
eglGetCurrentSurface(EGLint readdraw)
{
   if (readdraw == EGL_DRAW)
      return (EGLSurface)ps5_current_draw;
   if (readdraw == EGL_READ)
      return (EGLSurface)ps5_current_read;
   ps5_set_error(EGL_BAD_PARAMETER);
   return EGL_NO_SURFACE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglQueryContext(EGLDisplay display, EGLContext context_handle,
                EGLint attribute, EGLint *value)
{
   struct ps5_egl_context *context;

   PS5_EGL_LOCK();
   context = ps5_context(context_handle);
   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_FALSE;
   }
   if (!context) {
      ps5_set_error(EGL_BAD_CONTEXT);
      return EGL_FALSE;
   }
   if (!value) {
      ps5_set_error(EGL_BAD_PARAMETER);
      return EGL_FALSE;
   }
   switch (attribute) {
   case EGL_CONFIG_ID: *value = context->config_id; break;
   case EGL_CONTEXT_CLIENT_TYPE: *value = EGL_OPENGL_API; break;
   case EGL_CONTEXT_CLIENT_VERSION: *value = context->major; break;
#if PS5_ENABLE_CORE_CONTEXT_CANDIDATE
   case EGL_CONTEXT_OPENGL_PROFILE_MASK_KHR:
      *value = context->profile_mask;
      break;
#endif
   case EGL_RENDER_BUFFER: *value = EGL_BACK_BUFFER; break;
   default:
      ps5_set_error(EGL_BAD_ATTRIBUTE);
      return EGL_FALSE;
   }
   return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglQuerySurface(EGLDisplay display, EGLSurface surface_handle,
                EGLint attribute, EGLint *value)
{
   struct ps5_egl_surface *surface;

   PS5_EGL_LOCK();
   surface = ps5_surface(surface_handle);
   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_FALSE;
   }
   if (!surface) {
      ps5_set_error(EGL_BAD_SURFACE);
      return EGL_FALSE;
   }
   if (!value) {
      ps5_set_error(EGL_BAD_PARAMETER);
      return EGL_FALSE;
   }
   switch (attribute) {
   case EGL_WIDTH: *value = surface->width; break;
   case EGL_HEIGHT: *value = surface->height; break;
   case EGL_CONFIG_ID: *value = 1; break;
   case EGL_RENDER_BUFFER: *value = EGL_BACK_BUFFER; break;
   case EGL_SWAP_BEHAVIOR: *value = EGL_BUFFER_DESTROYED; break;
   case EGL_LARGEST_PBUFFER: *value = EGL_FALSE; break;
   case EGL_TEXTURE_FORMAT:
   case EGL_TEXTURE_TARGET: *value = EGL_NO_TEXTURE; break;
   case EGL_MIPMAP_TEXTURE: *value = EGL_FALSE; break;
   default:
      ps5_set_error(EGL_BAD_ATTRIBUTE);
      return EGL_FALSE;
   }
   return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglReleaseThread(void)
{
   PS5_EGL_LOCK();

   if (ps5_current_context)
      st_api_make_current(NULL, NULL, NULL);
   ps5_clear_current();
   ps5_bound_api = EGL_OPENGL_ES_API;
   ps5_last_error = EGL_SUCCESS;
   return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglWaitClient(void)
{
   struct pipe_fence_handle *fence = NULL;

   PS5_EGL_LOCK();

   if (ps5_current_context)
      st_context_flush(ps5_current_context->st, ST_FLUSH_WAIT, &fence,
                       NULL, NULL);
   return EGL_TRUE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglWaitGL(void)
{
   return eglWaitClient();
}

EGLAPI EGLBoolean EGLAPIENTRY
eglWaitNative(EGLint engine)
{
   if (engine != EGL_CORE_NATIVE_ENGINE) {
      ps5_set_error(EGL_BAD_PARAMETER);
      return EGL_FALSE;
   }
   return EGL_TRUE;
}

EGLAPI __eglMustCastToProperFunctionPointerType EGLAPIENTRY
eglGetProcAddress(const char *name)
{
   if (!name)
      return NULL;
   return (__eglMustCastToProperFunctionPointerType)
      _mesa_glapi_get_proc_address(name);
}

EGLAPI EGLSurface EGLAPIENTRY
eglCreatePbufferSurface(EGLDisplay display, EGLConfig config,
                        const EGLint *attributes)
{
   struct pipe_resource resource;
   struct ps5_egl_surface *surface;
   EGLint width = 1;
   EGLint height = 1;

   PS5_EGL_LOCK();

   if (!ps5_valid_display(display, true)) {
      ps5_set_error(EGL_BAD_DISPLAY);
      return EGL_NO_SURFACE;
   }
   if (!ps5_valid_config(config)) {
      ps5_set_error(EGL_BAD_CONFIG);
      return EGL_NO_SURFACE;
   }
   while (attributes && *attributes != EGL_NONE) {
      EGLint name = *attributes++;
      EGLint value = *attributes++;

      switch (name) {
      case EGL_WIDTH: width = value; break;
      case EGL_HEIGHT: height = value; break;
      case EGL_LARGEST_PBUFFER:
         if (value != EGL_FALSE && value != EGL_TRUE) {
            ps5_set_error(EGL_BAD_ATTRIBUTE);
            return EGL_NO_SURFACE;
         }
         break;
      case EGL_TEXTURE_FORMAT:
      case EGL_TEXTURE_TARGET:
         if (value != EGL_NO_TEXTURE) {
            ps5_set_error(EGL_BAD_MATCH);
            return EGL_NO_SURFACE;
         }
         break;
      case EGL_MIPMAP_TEXTURE:
         if (value != EGL_FALSE) {
            ps5_set_error(EGL_BAD_MATCH);
            return EGL_NO_SURFACE;
         }
         break;
      default:
         ps5_set_error(EGL_BAD_ATTRIBUTE);
         return EGL_NO_SURFACE;
      }
   }
   if (width <= 0 || height <= 0) {
      ps5_set_error(EGL_BAD_PARAMETER);
      return EGL_NO_SURFACE;
   }
   if (width > PS5_EGL_WIDTH || height > PS5_EGL_HEIGHT ||
       (uint64_t)width * height >
          (uint64_t)PS5_EGL_WIDTH * PS5_EGL_HEIGHT) {
      ps5_set_error(EGL_BAD_ALLOC);
      return EGL_NO_SURFACE;
   }
   surface = calloc(1, sizeof(*surface));
   if (!surface) {
      ps5_set_error(EGL_BAD_ALLOC);
      return EGL_NO_SURFACE;
   }
   memset(&resource, 0, sizeof(resource));
   resource.target = PIPE_TEXTURE_2D;
   resource.format = PIPE_FORMAT_R8G8B8A8_UNORM;
   resource.width0 = width;
   resource.height0 = height;
   resource.depth0 = 1;
   resource.array_size = 1;
   resource.nr_samples = 1;
   resource.nr_storage_samples = 1;
   resource.bind = PIPE_BIND_RENDER_TARGET;
   surface->targets[0] = ps5_display.screen->resource_create(
      ps5_display.screen, &resource);
   if (!surface->targets[0]) {
      free(surface);
      ps5_set_error(EGL_BAD_ALLOC);
      return EGL_NO_SURFACE;
   }
   surface->depth_stencil = ps5_create_depth_stencil(width, height);
   if (!surface->depth_stencil) {
      pipe_resource_reference(&surface->targets[0], NULL);
      free(surface);
      ps5_set_error(EGL_BAD_ALLOC);
      return EGL_NO_SURFACE;
   }
   ps5_init_surface(surface, width, height, false);
   return (EGLSurface)surface;
}

EGLAPI EGLSurface EGLAPIENTRY
eglCreatePixmapSurface(EGLDisplay display, EGLConfig config,
                       EGLNativePixmapType pixmap, const EGLint *attributes)
{
   PS5_EGL_LOCK();

   (void)config;
   (void)pixmap;
   (void)attributes;
   ps5_set_error(ps5_valid_display(display, true) ? EGL_BAD_NATIVE_PIXMAP :
                 EGL_BAD_DISPLAY);
   return EGL_NO_SURFACE;
}

EGLAPI EGLSurface EGLAPIENTRY
eglCreatePbufferFromClientBuffer(EGLDisplay display, EGLenum buftype,
                                 EGLClientBuffer buffer, EGLConfig config,
                                 const EGLint *attributes)
{
   PS5_EGL_LOCK();

   (void)buftype;
   (void)buffer;
   (void)config;
   (void)attributes;
   ps5_set_error(ps5_valid_display(display, true) ? EGL_BAD_MATCH :
                 EGL_BAD_DISPLAY);
   return EGL_NO_SURFACE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglCopyBuffers(EGLDisplay display, EGLSurface surface,
               EGLNativePixmapType target)
{
   PS5_EGL_LOCK();

   (void)surface;
   (void)target;
   ps5_set_error(ps5_valid_display(display, true) ? EGL_BAD_NATIVE_PIXMAP :
                 EGL_BAD_DISPLAY);
   return EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglBindTexImage(EGLDisplay display, EGLSurface surface, EGLint buffer)
{
   PS5_EGL_LOCK();

   (void)surface;
   (void)buffer;
   ps5_set_error(ps5_valid_display(display, true) ? EGL_BAD_MATCH :
                 EGL_BAD_DISPLAY);
   return EGL_FALSE;
}

EGLAPI EGLBoolean EGLAPIENTRY
eglReleaseTexImage(EGLDisplay display, EGLSurface surface, EGLint buffer)
{
   return eglBindTexImage(display, surface, buffer);
}

EGLAPI EGLBoolean EGLAPIENTRY
eglSurfaceAttrib(EGLDisplay display, EGLSurface surface, EGLint attribute,
                 EGLint value)
{
   PS5_EGL_LOCK();

   (void)surface;
   (void)attribute;
   (void)value;
   ps5_set_error(ps5_valid_display(display, true) ? EGL_BAD_ATTRIBUTE :
                 EGL_BAD_DISPLAY);
   return EGL_FALSE;
}
