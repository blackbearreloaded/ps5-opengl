#ifndef PS5_SCANOUT_H
#define PS5_SCANOUT_H

/* One build-time native window mode, shared by EGL, Gallium and presentation.
 * No output-mode request is implied by choosing a render-buffer resolution. */
#ifndef PS5_SCANOUT_HEIGHT
#ifdef AGC_4K
#define PS5_SCANOUT_HEIGHT 2160
#else
#define PS5_SCANOUT_HEIGHT 1080
#endif
#endif

#if PS5_SCANOUT_HEIGHT == 1080
#define PS5_SCANOUT_WIDTH 1920u
#define PS5_SCANOUT_BYTES 0xa00000u
#elif PS5_SCANOUT_HEIGHT == 1440
#define PS5_SCANOUT_WIDTH 2560u
#define PS5_SCANOUT_BYTES 0x1000000u
#elif PS5_SCANOUT_HEIGHT == 2160
#define PS5_SCANOUT_WIDTH 3840u
#define PS5_SCANOUT_BYTES 0x2000000u
#else
#error Unsupported PS5 scanout resolution
#endif

#if defined(AGC_4K) && PS5_SCANOUT_HEIGHT != 2160
#error AGC_4K conflicts with the selected PS5 scanout resolution
#endif

#define PS5_SCANOUT_ALIGNMENT 0x200000u
#define PS5_SCANOUT_POOL_BYTES (2u * PS5_SCANOUT_BYTES)
#define PS5_SCANOUT_TILED_BYTES \
   (((PS5_SCANOUT_WIDTH + 127u) / 128u) * \
    ((PS5_SCANOUT_HEIGHT + 127u) / 128u) * 0x10000u)
#if PS5_SCANOUT_BYTES < PS5_SCANOUT_TILED_BYTES || \
    PS5_SCANOUT_BYTES % PS5_SCANOUT_ALIGNMENT != 0
#error Invalid tiled display buffer size or alignment
#endif

#endif
