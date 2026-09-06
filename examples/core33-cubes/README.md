# Textured 3D cubes / frame benchmark

Standard OpenGL 3.3 shaders, perspective, directional lighting, depth testing,
two procedural textures and rotating cubes. No assets or additional dependencies.
The native app presents on the TV; automated validation uses numerical probes,
not screenshots or Remote Play.

Run `make cubes` to build PPSA99005 using the current source runtime; this does
not modify the installed SDK. Follow the normal folder deployment and testing
protocol. Host reference: `make test-cubes` (requires the existing SDK headers
and host software Mesa).

Three 1080p workloads render 1, 8 or 32 cubes (12 triangles per cube), first
using ordinary draws (`mode=0`), then one `glDrawArraysInstanced` (`mode=1`).
Both paths use the same shaders, lighting, geometry, alternating materials and
object positions. Instancing reads a per-instance placement attribute instead
of changing object uniforms. The two textures stay bound in both modes.
Each workload has two warm-up frames and eight measured frames. The short low-poly scene measures
draw-call overhead, **not a full game's expected FPS or maximum GPU throughput**.
Different object counts also change coverage; it is not a fixed-fill-rate test.

Frame timings include color/depth clear, draws, one `glFinish`, and EGL swap.
Numerical depth/texture checks run before and after each workload, outside the
timed interval: 668 probes in total. Host tests deliberately disable depth,
upload the wrong texture or omit instances and require these checks to fail. Do not use host timings
as PS5 results. CPU wall-time throughput is not a TV refresh-rate measurement.

Audit a saved native receipt:

```sh
python3 tools/summarize-cubes.py results/your-cycle-opengl.log
```

Require all probes, all 48 measured frames, cleanup, exact-title teardown and
healthy post-run services. Experimental runtime flags remain off by default.
The compiler fix makes this tested instanced path render correctly; it does not
automatically merge ordinary draws. Applications must group compatible objects
themselves to benefit from instancing.
The audit tool also accepts the original ordinary-only baseline receipts.
