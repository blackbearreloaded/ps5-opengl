# EGL lifecycle and high-refresh presentation

Current source enforces a conservative five-second interval before reopening
a successfully closed high-refresh presenter. Applications may recreate their
EGL window/display without implementing their own settling sleep.

**Availability:** the guard is included from source commit
`a62bd48d3b7fefe6f4028973a491d1e60a5ddb56`. Existing SDK 0.2.0 downloads do not
contain it and retain their application-owned five-second lifecycle requirement.

## Supported contract

- Successful HFR VideoOut close arms a process-local guard that survives EGL
  teardown. The next acquisition waits before opening/registering a presenter.
  Window-surface destruction, EGL termination and replacement share this path.
- Acquisition is lazy. The delay may occur at the first rendering operation,
  not necessarily during `eglInitialize` or surface creation.
- The runtime waits the full five seconds even if the application already
  waited. Keep an existing presenter for normal menu/level changes when practical.
- First acquisition, live reuse, steady rendering, final close and 60-Hz builds
  do not incur this additional wait. Flip draining and output restoration remain.
- Failed close retains ownership. Interrupted settling returns an error without
  opening/registering another port and leaves the guard armed. Honor errors;
  do not blindly retry or bypass the guard.

This is paced reopening, not unrestricted HDMI mode churn. It is not a
sink-readiness detector, a minimum-delay proof, cross-process pacing or
hotplug/suspend/device-loss recovery.

## Focused qualification

Two native lifecycle runs used three EGL sessions each, with **zero
application delay**. All cycles closed cleanly with healthy services and
exact-token lock release.

| Check | Result |
| --- | --- |
| EGL sessions / render-oracle frames | **6 / 36 passed** |
| Guarded reopen events | **4**, five seconds each |
| Initial session in each fresh process | No reopen wait |
| Logged HDMI disconnect/reconnect | **0** |
| HDMI mode sequence per session | 2160p119.88, then 2160p59.94 restoration |
| Tracked GPU direct allocations/mappings after each session | Zero |
| Tracked owned heap after first session | 9,455 bytes; zero subsequent growth |
| Separate 30-second 4K window benchmark | **119.882665 FPS**, no reopen-wait event |

The window completed 3,597 measured frames in 30.004338 seconds. This preserves
the tested average throughput, not perfect pacing or game FPS.
Memory counters exclude foreign heaps, module-internal GPU allocations and RSS.
HDMI evidence is console negotiation, not a new independent TV measurement.

Host checks cover actual acquisition/shutdown code, first/live open, failed
close, idempotent cleanup, interrupted settling and guarded retry. The software
ImGui oracle and malformed-evidence rejection tests also pass. Both profile SDKs
pass 344 export checks and five consumer links: three GL and two relocated SDL.

## Tested identities

The focused 4K results above belong to these successor artifacts, not the
published 0.2.0 runtime or a subsequent rebuild. The 1440p pair is host-only.

| SHA-256 | 4K120 | 1440p120, host-only |
| --- | --- | --- |
| GL manifest | `e94082986af6b83ffcd3352ea5eda247276d246afcca3f7e6a055019afd13d2d` | `fb1ab56f8e9da18b54889d38660826c13bca5dd11884818845bb62ac2aff9d6a` |
| Runtime archive | `44d471b305b62838f2922549e9cd80542a56c9ec0a3fa89cda9eae32e447adfe` | `dcdb1318ad04c30806556841bc766d2d490a83c4f06d8f1577a13b89cc23128a` |
| SDL build receipt | `450e5c42e412d59b75272f7c961cb3704ed59562dbaec33faf2b481c40cbec42` | `c94044dae2f3ec0b3c8734dcba7bc4b35805e176a89251edfe37d7dd0996c833` |

Only `ps5_agc_runtime_backend.o` changed in each GL archive relative to its
0.2.0 counterpart. Other runtime members and dependency bytes are unchanged.

| Audited artifact | SHA-256 |
| --- | --- |
| Lifecycle executable | `ba7b1ed4f7a7e7b4ef63b6d491d2286a84b85506c60371df224720c7c946d0ce` |
| Window executable | `49c06256861782acf0a7336f2e335a7acf353e779b5a6424611b4d47c8d3cd27` |
| Lifecycle audit 1 | `948e379f1795d8cd6c74cb35b1b97fee79e566e6c9dc0cbb37cce82732efef87` |
| Lifecycle audit 2 | `3f50d66061d8dd7f23c98ac3aca83082d1d3297bcb0bdc166fa5e21d45e8b7ff` |
| Window audit | `312321280fe829bdd7a5e33a9032a8ebcf6c9fa7a933eb85d1286f6142cd9df8` |

Raw receipts and detailed development notes remain local. The
`tools/lifecycle_reopen_evidence.py` verifier checks the ordered sessions,
runtime waits and all HDMI modes without filtering reconnect events.

Use the [SDK integration guide](consumer-build.md) when adopting this change.
Keep a matched GL/SDL pair, preserve error/ownership handling and validate the
application's own lifecycle. Prior game or CTS results do not qualify new bytes.
