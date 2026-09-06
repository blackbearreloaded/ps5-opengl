# Installed-SDK Sokol renderer check

Uses unmodified [Sokol](https://github.com/floooh/sokol) `sokol_gfx.h` at
`48c85905aeaa1350feb17515961aecb6c75447d8` (zlib license), with its GL backend
and debug validation enabled. EGL entry point, scene and numeric oracle are
project-owned. The SDK contains modern Khronos headers; two 4.x header macros
are undefined before including Sokol to select its existing 3.3 fallback
paths, as with a 3.3-only external loader. No GL functions are stubbed and no
upstream algorithms are changed. The host script rejects imports outside the
344-command Core 3.3 list; it does not merely rely on a version string.

```sh
make source-fetch
make sdk
bash tools/test-sokol-host.sh
make sokol
```

Three frames at 320x240 / 640x480 / 320x240 recreate the renderer and resources,
upload an indexed quad, update an instance buffer, and draw two textured
instances with uniforms, nearest samplers, alpha blending and hardware scissor.
Every RGBA component is compared with an independent CPU oracle (1,843,200
components total, tolerance 2). All frames, resource states, GL/EGL cleanup,
and Sokol warnings/errors must pass. The same scene/oracle runs first on host
software Mesa. Blits present the checked images; screenshots are unnecessary.

Run the locked native gate wrapper with `egl_public_core33_sokol.o`, frozen
hashes, 60-second observation and stop text `[ps5-sokol] finished`.
This is renderer compatibility evidence, not official CTS coverage.
