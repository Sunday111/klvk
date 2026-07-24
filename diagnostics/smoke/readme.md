# Diagnostic smoke tests

This suite renders deterministic, offscreen snapshots from a selection of klvk examples. It is intended for comparing
a development branch against a freshly captured `main` baseline on the same machine and driver. Baseline images are
generated locally rather than committed because Vulkan output can legitimately vary across GPUs, drivers, and shader
compilers.

The suite currently excludes `curve_fractal`: repeated fixed-clock captures from the same build differ because that
example uses wall-clock work scheduling internally. Add other examples only after confirming that two consecutive runs
produce exact images.

## Capture and compare a branch

Run the first capture while klvk is checked out on an up-to-date `main`:

```sh
python3 diagnostics/smoke/smoke_test.py capture --output /tmp/klvk-smoke/main
```

Switch klvk to the development branch and capture it:

```sh
python3 diagnostics/smoke/smoke_test.py capture --output /tmp/klvk-smoke/branch
```

Compare the two runs:

```sh
python3 diagnostics/smoke/smoke_test.py compare \
  --baseline /tmp/klvk-smoke/main \
  --candidate /tmp/klvk-smoke/branch \
  --diff-output /tmp/klvk-smoke/diff
```

The default consumer is `$YAE_CLONED_REPOSITORIES_DIR/Sunday111/verlet/main`; pass `--project-dir` to use another yae
project. Capture uses `yae run`, so it builds each target, stages its content, and uses the configured GPU launcher.

Comparison is deliberately strict: any changed RGB pixel returns a failure. The console and `diff/report.json` record
changed-pixel counts, maximum channel delta, mean absolute channel delta, and image hashes. Each file under `diff/` is
an absolute per-channel RGB difference image for visual inspection. Capture manifests include a fingerprint of the
suite and diagnostic configs, so comparison refuses stale, partial, or mismatched runs.

If a branch changes `suite.json` or one of its configs, snapshot the branch's entire suite definition before switching
revisions. Use that same snapshot for both captures and the comparison; `--suite` is a global option and must precede
`capture` or `compare`:

```sh
# On the development branch:
rm -rf /tmp/klvk-smoke/suite
cp -a diagnostics/smoke /tmp/klvk-smoke/suite

# After switching klvk to an up-to-date main:
python3 diagnostics/smoke/smoke_test.py \
  --suite /tmp/klvk-smoke/suite/suite.json \
  capture --output /tmp/klvk-smoke/main

# After switching klvk back to the development branch:
python3 diagnostics/smoke/smoke_test.py \
  --suite /tmp/klvk-smoke/suite/suite.json \
  capture --output /tmp/klvk-smoke/branch
python3 diagnostics/smoke/smoke_test.py \
  --suite /tmp/klvk-smoke/suite/suite.json \
  compare \
  --baseline /tmp/klvk-smoke/main \
  --candidate /tmp/klvk-smoke/branch \
  --diff-output /tmp/klvk-smoke/diff
```

Copying the complete directory is important because config paths are relative to `suite.json`. A shared snapshot keeps
the manifest fingerprints identical without weakening stale- or mismatched-suite detection.

The committed diagnostic configs use a fixed clock, a 320×240 framebuffer, offscreen presentation, and captures without
ImGui. `falling_sand_input.json` additionally exercises scheduled mouse movement, clicking, scrolling, and keyboard
press/release events.
