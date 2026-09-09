#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Deterministic receipt checks; no GPU or native measurements are synthesized as evidence."""
from pathlib import Path
import runpy
import unittest

api = runpy.run_path(str(Path(__file__).with_name("summarize-cubes-profile.py")))
summarize, compare = api["summarize"], api["compare"]


def receipt(host=False, completion=1, gpu_clear=False, height=1080):
    lines = [f"[ps5-cubes-profile] config version=2 width={height * 16 // 9} height={height} objects=128 "
             f"modes=2 warmup=30 seconds=1 swap_completed={completion} host={int(host)} max_frames=16384"]
    for mode in (0, 1):
        public_draws = 1 if mode else 128
        draws_per_frame = 0 if host else public_draws + gpu_clear
        clear_path = "host" if host else ("gpu" if gpu_clear else "cpu")
        oracle = f"[ps5-cubes] oracle mode={mode} objects=128 probes=513 PASS"
        lines += [oracle,
                  f"[ps5-cubes-profile] calibration mode={mode} objects=128 public_draws={public_draws} "
                  f"native_draws_per_frame={draws_per_frame} clear_path={clear_path}", oracle]
        finish, swap = (0, 8_000_000) if completion else (3_000_000, 5_000_000)
        lines += [f"[ps5-cubes-profile] frame mode={mode} objects=128 frame={frame} "
                  f"clear_ns=1000000 submit_ns=1000000 finish_ns={finish} swap_ns={swap} "
                  f"total_ns=10000000 interval_ns=10000000 native_draws={draws_per_frame}" for frame in range(100)]
        draws = 132 * draws_per_frame
        lines += [f"[ps5-cubes-profile] mode mode={mode} objects=128 frames=100 measured_ns=1000000000 native_draws={draws}"]
    lines += ["[ps5-cubes-profile] completed=2 cleanup=1 result=0"]
    if not host:
        lines += ["[pss-opengl-native] gate completed status=0"]
    return "\n".join(lines) + "\n"


def rejected(text, **kwargs):
    try:
        summarize(text, seconds=1, **kwargs)
    except ValueError:
        return
    raise AssertionError("Malformed or incomplete measurement was accepted")


def main():
    text = receipt()
    report = summarize(text, seconds=1)
    cell = report["workloads"][0]
    assert cell["completed_fps"] == 100 and cell["native_draws"] == 16896
    assert report["version"] == 2 and cell["clear_path"] == "cpu"
    assert cell["public_draws"] == cell["native_draws_per_frame"] == 128
    for gpu_clear in (False, True):
        native_text = receipt(gpu_clear=gpu_clear)
        native_report = summarize(native_text, seconds=1)
        for mode, workload in enumerate(native_report["workloads"]):
            public = 1 if mode else 128
            assert workload["public_draws"] == public
            assert workload["native_draws_per_frame"] == public + gpu_clear
            assert workload["clear_path"] == ("gpu" if gpu_clear else "cpu")
            assert workload["native_draws"] == 132 * (public + gpu_clear)
        per_frame = 128 + gpu_clear
        for wrong in (per_frame - 1, per_frame + 1):
            rejected(native_text.replace(f" native_draws={per_frame}\n", f" native_draws={wrong}\n", 1))
        for wrong in (132 * per_frame - 1, 132 * per_frame + 1):
            rejected(native_text.replace(f"native_draws={132 * per_frame}\n", f"native_draws={wrong}\n", 1))
        rejected(native_text.replace("clear_path=gpu" if gpu_clear else "clear_path=cpu",
                                     "clear_path=cpu" if gpu_clear else "clear_path=gpu", 1))
        rejected("\n".join(line for line in native_text.splitlines() if " calibration " not in line))
        rejected(native_text.replace(f" native_draws={per_frame}\n", "\n", 1))
        rejected(native_text.replace(f" native_draws={132 * per_frame}\n", "\n", 1))
        rejected(native_text.replace("public_draws=128", "public_draws=127", 1))
        rejected(native_text.replace(f"native_draws_per_frame={per_frame}",
                                     f"native_draws_per_frame={per_frame + 1}", 1))
    assert cell["phases"]["interval"]["p99_ms"] == 10
    assert cell["phases"]["interval"]["budget_misses"] == 0
    assert summarize(text, seconds=1, budget_hz=120)["workloads"][0]["phases"]["interval"]["budget_misses"] == 100
    assert summarize(text.replace("\n", "\r\n"), seconds=1) == report
    for height in (1440, 2160):
        high_res = summarize(receipt(height=height), seconds=1, height=height)
        assert high_res["height"] == height and high_res["width"] == height * 16 // 9
        rejected(receipt(height=height))
        rejected(text, height=height)
    rejected(text, height=720)
    host_report = summarize(receipt(host=True), host=True, seconds=1)
    assert host_report["mode"] == "host-reference"
    assert all(row["clear_path"] == "host" and row["native_draws_per_frame"] == 0
               and row["native_draws"] == 0 for row in host_report["workloads"])
    rejected(receipt(host=True).replace("clear_path=host", "clear_path=cpu", 1), host=True)
    rejected(receipt(host=True).replace(" native_draws=0\n", " native_draws=1\n", 1), host=True)
    rejected(text.replace("clear_path=cpu", "clear_path=host", 1))
    finish_report = summarize(receipt(completion=0), seconds=1, swap_completed=0)
    assert compare(finish_report, report)["workloads"][0]["fps_ratio"] == 1
    try:
        compare(host_report, report)
    except ValueError:
        pass
    else:
        raise AssertionError("Host/native performance comparison accepted")
    for field, value in (("objects", 512), ("seconds", 30), ("budget_hz", 120),
                         ("height", 1440), ("width", 2560)):
        bad = dict(report, **{field: value})
        try:
            compare(bad, report)
        except ValueError:
            pass
        else:
            raise AssertionError(f"Unmatched {field} comparison accepted")
    # Nearest-rank statistics: max spikes must survive into p99 and budget misses.
    stats = api["timing"]([1_000_000] * 98 + [20_000_000, 40_000_000], 60)
    assert stats["p50_ms"] == stats["p95_ms"] == 1
    assert stats["p99_ms"] == 20 and stats["max_ms"] == 40 and stats["budget_misses"] == 2
    assert api["timing"]([16_680_000], 60)["budget_misses"] == 1
    assert api["timing"]([16_680_000], 59.94)["budget_misses"] == 0
    for budget in (0, -1, float("nan"), float("inf")):
        rejected(text, budget_hz=budget)
    for bad in ("", text + text, text.replace("PASS", "FAIL", 1),
                text.replace("frame=1 ", "frame=0 ", 1), text.replace("objects=128", "objects=32", 1),
                text.replace("seconds=1 ", "seconds=30 ", 1), text.replace("host=0", "host=1"),
                text.replace("width=1920", "width=3840"), text.replace("cleanup=1", "cleanup=0"),
                text.replace("clear_ns=1000000", "clear_ns=nan", 1),
                text.replace("submit_ns=1000000", "submit_ns=-1", 1),
                text.replace("finish_ns=0", "finish_ns=1", 1),
                text.replace("total_ns=10000000", "total_ns=9999999", 1),
                text.replace("interval_ns=10000000", "interval_ns=9999999", 1),
                text.replace("frames=100 ", "frames=99 ", 1),
                text.replace("measured_ns=1000000000", "measured_ns=999999999", 1),
                text.replace("native_draws=16896", "native_draws=16895"),
                text.replace("gate completed status=0", "gate completed status=-1"),
                text.replace("[pss-opengl-native] gate completed status=0", ""),
                text + "[ps5-gallium] draw-rejected error=1\n"):
        rejected(bad)
    rejected(receipt(host=True))
    rejected(receipt(completion=0))
    # Inter-frame overhead must affect completed FPS, not disappear into draw FPS.
    slower = text.replace("interval_ns=10000000", "interval_ns=10001000")
    slower = slower.replace("measured_ns=1000000000", "measured_ns=1000100000")
    assert summarize(slower, seconds=1)["workloads"][0]["completed_fps"] < 100
    print("cubes-profile parser: PASS (accounting, comparison, timing and negative receipts)")


class ProfileReceiptTests(unittest.TestCase):
    def test_receipts(self):
        main()


if __name__ == "__main__":
    unittest.main()
