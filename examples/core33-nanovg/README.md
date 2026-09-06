# Installed-SDK NanoVG GL3 renderer check

Uses unmodified [NanoVG](https://github.com/memononen/nanovg) at
`ce3bf745eb2d2dbc14a50bf2446783f691ac4353` (zlib license). Only EGL/native
entry-point, build glue, and deterministic test content are project-owned.
The GL3 backend keeps antialiasing, stencil strokes, UBOs, and debug checks.

```sh
make source-fetch
make sdk
bash tools/test-nanovg-host.sh
make nanovg
```

Three frames exercise 320x240 / 640x480 targets and renderer recreation:
premultiplied blending, a stencil-cut hole, stencil strokes with a
self-intersection, shader clipping, a nearest-filtered image, and a gradient.
Acceptance requires 45 toleranced pixel probes, an entirely cleared stencil
buffer after every frame, no logged GL/backend errors, status 0, and clean
resource/EGL teardown. The same oracle runs first on host software Mesa.
The image is presented, but screenshots are not required.

Use the locked native gate wrapper with `egl_public_core33_nanovg.o`, frozen
hashes, 60-second observation, and stop text `[ps5-nanovg] finished`.
This is supplemental renderer compatibility evidence, not official CTS coverage.
