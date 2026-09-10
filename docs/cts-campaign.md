# CTS campaign

The objective is one fresh, reproducible GL33 baseline, followed by development
toward OpenGL 4.6 Core. Audit the 4.6 requirements offline in parallel; do not
wait for formal 3.3 certification before starting that work. Use targeted CTS
and 3.3 regressions during feature development, then a complete campaign on the
frozen 4.6 candidate. An engineering matrix and a Khronos-approved submission
are separate milestones.

## G1: establish the right target before a full run

Do not spend a full campaign validating the wrong test revision or configuration.

- Select an eligible OpenGL CTS release. The official release list currently
  identifies the 4.6.8 family as active; `opengl-cts-4.6.8.1` resolves to
  `067e8832315e79817ede1c4863804e440f5d1c80`. This CTS release can test GL33;
  it does not require implementing OpenGL 4.6. Recheck eligibility before submission.
  [Khronos release overview](https://github.com/KhronosGroup/VK-GL-CTS/wiki/Overview-of-releases).
- Our dependency pin is a different development commit. Prepare the release in
  a separate checkout; do not replace historical sources or receipts. Compare
  inventories, framework behavior, patches and dependencies before updating pins.
- Audit every overlay patch. In particular,
  [the compute-stage test correction](../conformance/vk-gl-cts/patches/0005-gl33-negative-compute-guard.patch)
  changes a test body. Establish an upstream-accepted fix or the required waiver;
  our local judgment alone is not submission approval.
  [Khronos porting rules](https://github.com/KhronosGroup/VK-GL-CTS/blob/main/external/openglcts/README.md#porting).
- Resolve the default-framebuffer/configuration gap. The current
  [EGL implementation](../src/egl/ps5_egl.c) exposes one RGBA8/D32/S8,
  non-multisampled config marked non-conformant. The
  [CTS adapter](../conformance/vk-gl-cts/framework/platform/ps5/tcuPS5Platform.cpp)
  is pbuffer-only. Its new release preparation path queries actual framebuffer
  properties and checks them against EGL instead of using a fixed descriptor.
  Supporting D24/S8
  and 4x MSAA in user FBOs does not establish equivalent default/window configs.
  Implement and test the required configurations, or obtain the applicable
  platform waiver. Never merely change the reported bits or conformance flags.
  [Khronos passing criteria](https://github.com/KhronosGroup/VK-GL-CTS/wiki/OpenGL-and-OpenGL-ES-Conformance-Submission-Passing-Criteria).
- Generate the official `cts-runner --type=gl33 --summary` configuration inventory
  on the actual binding/platform. Reconcile it with the PS5 runner before freezing
  the campaign. The existing four size/seed configurations are not proof of
  coverage of every required EGL/window configuration.

The older wiki's confidential-test wording predates the current README: the
README limits that OpenGL requirement to releases before 4.6.6. Use the selected
release's instructions, not a mixture of old requirements.

G1 passes only with a recorded release identity, patch disposition, truthful
config enumeration and an official-summary-equivalent execution inventory.
The current tooling below prepares engineering shards; it does not create
official summary files or establish that arbitrary sharding is submission-eligible.

Short diagnostic runs may establish these prerequisites before G1 passes:
start with `info`, then the bounded regression smoke and the unchanged
`KHR-GL33.shaders.negative.non_precision_qualifiers_in_struct_members` test.
Record them as release-port qualification, not submission acceptance. Stop on
the first functional failure and preserve it. Do not start the full matrix
while the required configuration/runner inventory remains unresolved.
The current adapter requires `--deqp-surface-type=pbuffer` for its native
offscreen profiles. Omitting it requests a window and fails before case
execution. The two FBO profiles remain explicitly `fbo`; they are not window
configuration coverage. Generate new hashed selections after changing this
contract; preserve older plans and receipts unchanged.

The adapter also registers the upstream `CTS-Configs` package and a native EGL
display backed by the runtime's static EGL 1.4 entry points. Run the single
`--deqp-case=CTS-Configs.gl33` diagnostic to inspect configuration selection,
then check its saved receipt:

```sh
python3 tools/summarize-cts-qpa.py PATH-TO-CONFIGS.qpa --require-egl-configs --json
```

This keeps the upstream result unchanged but rejects synthetic `default(0)`
coverage, missing/duplicate configurations and incomplete receipts. A passing
configuration case alone is not proof of an eligible real EGL config, required
framebuffer formats, working windows, or a complete official runner inventory.
Neither this adapter nor the checker changes runtime conformance flags.

## G2: freeze once, qualify cheaply

1. Finish affected host regressions and commit the candidate. Preserve the earlier
   branding changes; rebuild the native app so its logs match the current readers.
2. Build one production SDK and one native CTS folder app using that exact SDK.
   Freeze source/patch/toolchain identities, runtime archive, executable, modules,
   metadata and immutable data-file hashes. No performance experiments during
   acceptance collection.
3. Verify actual GL/EGL version, limits, color/depth/stencil/sample counts and
   the context selected for each required configuration. Do not infer these from
   a TV mode or a hardcoded descriptor.
4. Run short info/transfer/geometry regressions across the required configurations.
   These exercise recently changed code before long shader/texture workloads.
   A successfully audited shard on the unchanged candidate can count toward the
   engineering matrix; it need not be repeated just because it ran first.
5. Check measured startup and case times against the proposed budgets. Freeze
   revised selections before running them if the historical forecast is inaccurate.

No separate 1080p/1440p/2160p performance sweep is part of this CTS campaign.
Required CTS framebuffer dimensions/configurations must still be tested, even
when one TV output mode is sufficient.

## G3: prepare timed, resumable batches

Reuse [the inventory reader](../tools/summarize-cts-qpa.py),
[the argument generator](../tools/prepare-cts-shard.py) and
[the offline planner](../tools/plan-cts-campaign.py):

```sh
python3 tools/summarize-cts-qpa.py --inventory results/PRIOR-CAMPAIGN \
  --output .local/prior-cts-timings.json
python3 tools/plan-cts-campaign.py --timings .local/prior-cts-timings.json \
  --margin 1.2 --budget-seconds 120 --startup-seconds 15 \
  --output .local/NEW-CTS-PLAN
```

The planner uses per-configuration timings, preserves inventory order within
each shard, and asserts exact once-only coverage. More recent inventory files
can be appended with another `--timings`; they influence scheduling only.
It writes `plan.json`, its checksum, a summary, and hashed argument/case-list
files for each shard. It refuses to overwrite an existing plan.

The default margin is more conservative (1.5). The explicit 1.2 above reserves
20% timing headroom plus 15 seconds for startup. These are forecasts, not hard
execution guarantees; short-run calibration comes first. Unknown or failed
case timings receive a conservative estimate, never an assumed fast pass.

Queue order is short smoke, previous failures, isolated long cases, then bulk.
Early failures stop costly acceptance collection. The two-minute normal
observation limit remains in force. Cases whose estimated observation exceeds
it remain in the inventory with an explicit approval flag. No reduced iteration
counts, swizzle combinations, shader variants, dimensions or seeds.

The generated configuration set is explicitly the existing four-profile
engineering matrix. G1 may require additional profiles; regenerate the planner
configuration inputs and strict auditor together before a submission campaign.
Never silently claim those missing configurations passed.

## G4: execute and resume without redundant work

Use the existing [native CTS wrapper](../tools/Run-NativeOpenGLCTS.ps1), not a
new console controller. One coordinator owns deployment; offline review/build
work may run in parallel. Do not run simultaneous apps against one PS5.

- Follow the [testing protocol](testing.md#native-tests) for every bounded
  launch: lock, idle/health preflight, verify inputs, run, capture, close, verify
  health, release only the owned token. No lock during host analysis.
- Deploy the complete folder once. Subsequently use `-Incremental
  -ReuseInstalledBinaries` when valid: verify existing binary bytes and upload
  only the selected arguments/case list. Recheck after any intervening app update.
- Use `-Headless` and the completion marker; do not wait out an observation
  budget after completion. No Chiaki startup, routine screenshots or TV changes
  for numeric offscreen assertions. Display-specific assertions need separate evidence.
- Store each attempt in a new directory. Pass the shard's exact argument hash,
  case-list hash, count and observation limit. Review optional-extension
  `NotSupported` results by exact case/reason, not only an allowed total.
- Add a receipt to the selected candidate manifest only after strict input,
  QPA/status, binary, target, teardown and health verification. A file existing,
  a completion marker alone, or a timing ledger entry is not acceptance.
- On interruption, skip only already audited shards from the same frozen
  candidate. Preserve the failed attempt and rerun its exact selection after
  diagnosis; do not promote a truncated successful prefix.
- Driver/compiler/SDK/binary changes create a new candidate. Validate the affected
  cases first, then collect a complete final matrix for the new frozen identity.
  Do not mix successful shards from old and new binaries.

No automatic retry for a functional failure, device loss, uncertain completion
or suspected panic. A transport-only retry follows the protocol's bounded rule.

## G5: acceptance and submission

Use [the strict candidate auditor](../tools/verify-cts-candidate.py) during
collection with `--allow-incomplete`. Final engineering acceptance requires no
gaps, duplicate selected results, mixed identities or unreviewed exclusions.
Failed/incomplete/blocked tests remain visible. The published historical
39,544-result archives are controls, not current acceptance.

Keep original QPA files and the selected release's required summaries,
source/patch records, product statement and waiver records. Validate the
submission with the official tools after reconciling the native execution path
with the official runner's format; do not invent summary evidence or concatenate
logs to conceal separate attempts.
[Khronos submission instructions](https://github.com/KhronosGroup/VK-GL-CTS/wiki/Creating-a-OpenGL-and-OpenGL-ES-Submission-Package).

Report engineering completion separately from external Khronos review.
Certification/adopter actions require the project owner's involvement.
