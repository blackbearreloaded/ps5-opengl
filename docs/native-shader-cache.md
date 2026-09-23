# Native shader cache

Set `PS5_SHADER_CACHE_DIR` before creating an OpenGL context to an existing,
application-owned writable directory. The native SDK caches compiled individual
NIR shader variants there. This supplements application shader/pipeline caches;
linked geometry and tessellation compilation retain their existing paths.

Keys cover serialized NIR, complete compiler options and entry-point contents,
plus a build fingerprint of the driver, configuration and compiler archives
(including thin archive object contents). GPU allocations and live pointers are
never persisted. Loaded code and metadata pass through the normal package builder.

There are 1024 directly mapped slots, each limited to 64 KiB. Hash collisions,
oversized shaders, stale builds, damaged records or unavailable storage fall back
to compilation. Atomic file replacement prevents readers seeing partial writes.
Normal records occupy at most 64 MiB; an interrupted write can leave a temporary
file. The application may delete its cache directory when no context is using it.

Run `python3 tests/ps5/test_shader_cache.py` for real compiler/restart checks,
input and build invalidation, corrupted-record rejection and storage fallback.
Host tests do not establish console startup or frame-time improvement.
