# Textured 3D cubes / frame benchmark

Standard OpenGL 3.3 shaders, perspective, directional lighting, depth testing,
two procedural textures and rotating cubes. No assets or additional dependencies.
The native app presents on the TV; automated validation uses numerical probes,
not screenshots or Remote Play.

Run `make cubes` to build PPSA99005 using the current source runtime; this does
not modify the installed SDK. Follow the normal folder deployment and testing
protocol. Host reference: `make test-cubes` (requires the existing SDK headers
and host software Mesa).

Three 1080p workloads issue 1, 8 or 32 ordinary draws per frame (12 triangles
per cube), changing object uniforms and alternating textures. Each has two
warm-up frames and eight measured frames. The short low-poly scene measures
draw-call overhead, **not a full game's expected FPS or maximum GPU throughput**.
Different object counts also change coverage; it is not a fixed-fill-rate test.

Frame timings include color/depth clear, draws, one `glFinish`, and EGL swap.
Numerical depth/texture checks run before and after each workload, outside the
timed interval: 334 probes in total. Host tests deliberately disable depth or
bind the wrong texture and require these checks to fail. Do not use host timings
as PS5 results. CPU wall-time throughput is not a TV refresh-rate measurement.

Audit a saved native receipt:

```sh
python3 tools/summarize-cubes.py results/your-cycle-opengl.log
```

Require all probes, all 24 measured frames, cleanup, exact-title teardown and
healthy post-run services. Experimental runtime flags remain off by default.
