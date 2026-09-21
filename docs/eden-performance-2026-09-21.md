# Eden workload: submission and CPU preparation improvements

This branch removes repeated CPU waits and redundant preparation from the
OpenGL path exercised by Eden. It does not introduce a new API version or
replace the existing SDK release.

## Runtime changes

- Render supported packed-float and sRGB targets directly; preserve guarded
  fallbacks for unsupported layouts. Isolate clear draws from application queries.
- Group native command streams and retain up to eight in-flight batches.
  Fences, resources, descriptors and queries retain ownership until confirmed
  completion; CPU hazards wait for relevant work.
- Correct completion-marker visibility and CPU-upload cache acquisition.
  Reuse retired command/descriptor storage and unchanged texture publications.
  Explicit buffer writes publish their ranges; persistent mappings bypass cached
  draw publication.
- Reduce CPU fallback costs with bounded scratch reuse, compatible tiled-image
  copies, vectorized index bounds and aligned streaming clears.
- Overlap offscreen work with pending presentation while waiting before scanout
  reuse or shutdown. Build native hot paths with `-O3` without fast-math.

The compact native-work publication experiment was reverted. Full native-work
initialization/publication remains enabled. Optional per-draw profiling and GPU
timestamps were disabled in the measured application build.

## Recorded console result

Firmware 6.02, 1080p60 scanout, SUMMERHOUSE animated menu. The tested runtime
source was `a3372535eec15d58286c5a61730b9b32609c88fa`, built with
`PS5_DRAW_PROFILE=0` and `PS5_DRAW_GPU_TIMESTAMPS=0`.

| Measurement | Result |
| --- | ---: |
| Frames 40–1830, including capture pauses | 30.09679 FPS over 59.47478 s |
| Frames 310–580 | 30.23703 FPS |
| Frames 610–1180 | 30.47446 FPS |
| Frames 1210–1830 | 30.39882 FPS |
| Late ten-frame intervals, minimum / median / maximum | 28.79513 / 30.44330 / 30.91037 FPS |
| GPU submission thread CPU cost in steady segments | 27.02–27.14 ms/frame |

These are application presentation rates, not isolated GPU execution times.
Eden also received JIT memory-access improvements and physical-core worker
placement. **The combined 30 FPS result cannot be attributed to OpenGL alone.**
Gameplay, other titles and all-frame correctness are not qualified by this menu run.

Native startup draw/readback checks, orderly teardown and post-run console
service health passed. The final heap receipt reported zero allocation failures.
All seven menu glyph masks match exactly between frame 600 and frame 1200;
button interiors differ by at most one RGB channel level out of 255. Earlier
reports of missing text in these captures were mistaken visual assessments,
withdrawn after comparison of raw framebuffer pixels. No missing-text fix is claimed.

Local evidence: Eden candidate `c3cf7564abbdd9a88eb79649ca01785a93093a32`, case
`headless-fw602-20260921-095509`. Runtime archive SHA-256:
`1a513c43bdd6c0510964d0b6e29b5e4c61ec3f6b43118aabebfd300e6ac56bb6`.
Executable SHA-256:
`ed3665e6638159e73d947d7614f65f7ba9eca2d49d5781fccebb744b3b4b6922`.
Raw application captures/logs are retained locally; no game assets or keys are
included in this repository. Fresh builds do not inherit exact-binary qualification.

## Reproducible offline checks

`make test` covers extracted production queue, lifetime, fence, cache-publication,
transfer and presentation logic, including failure and boundary cases.
`make test-staging` covers CPU layouts and software-OpenGL reference behavior.
The native 1080p60 SDK was cross-built and integrated into the recorded console
candidate. Host checks are not hardware cache-coherence proof or certification.

The earlier dated Eden notes describe individual experiments and their historical
results; this note describes the final combined workload and its limits.
