# Upstream Sokol cube

Bounded native adaptation of [cube-glfw.c](https://github.com/floooh/sokol-samples/blob/8afa83928ce1870efeb0d513e7c4dce4f5db7b3e/glfw/cube-glfw.c):
180 rotating, depth-tested and back-face-culled frames at 1920x1080.
This is a small existing 3D sample, not a GLFW port or a game benchmark.

```sh
python3 tools/fetch-sources.py --sokol-samples
make test-sokol-cube
PS5_OPENGL_PREFIX=/absolute/path/to/candidate-sdk make sokol-cube
```

The standard native builder produces the PPSA99005 folder. Use the documented
bounded folder-upload/launch protocol; never send its graphics executable to a loader.

The generator verifies both pinned upstream checkouts and leaves them unchanged.
Its six exact adaptations are: native window glue include, vecmath include path,
wrapped entry point, GLSL 410 to 330 Core (no shader logic change), four samples
to one, and an error-recording logger. Geometry, transforms, vertex/index buffers,
uniforms, pipeline state and draw calls are retained. Platform glue replaces the
800x600 desktop window with the native fullscreen EGL surface and limits the loop;
there is no interactive input. The software-Mesa host uses a single-buffered
pbuffer with explicit front-buffer selection; the native window is unchanged.

At frames 0/44/89/134/179, 17 RGBA8 scanline readbacks are compared with independent CPU
ray/unit-box intersections using the intended transform. A 31x17 grid checks
foreground face colors and background, excluding ambiguous cube-edge probes;
color tolerance is two byte values. The host checks 2,596 pixels over five poses
and rejects an intentionally erased cube. Readbacks do not prove TV scanout or
input health. The readback buffer is 7,680 bytes, not a full 8,294,400-byte image.
The first native candidate aborted during GLSL built-in initialization after
allocation failure, before drawing. With the small buffer, native commit `a81c24d`
passes all 180 frames and 2,596 probes, EGL cleanup and title teardown. This supports
test-buffer memory pressure as the trigger; larger application-memory robustness
remains unvalidated. Busy VideoOut unregister followed by successful close remains.

The sample is MIT-licensed by Andre Weissflog; its bundled vecmath is used under
Mattias Gustavsson's MIT option. See [notices](../../THIRD_PARTY_NOTICES.md).
