#!/usr/bin/env python3
"""Audit one bounded demo profile; timings are CPU wall time, not GPU timers."""
import argparse
import json
import math
from pathlib import Path
import re


def summarize(text, host=False, submit_profile=False, deferred_batches=False):
    def require(ok, message):
        if not ok:
            raise ValueError(message)

    require(not re.search(r"^\[ps5-gallium\] (?:draw-rejected|reject-|clear-gpu-color status=(?!0\b))",
                          text, re.M), "driver reported a failed operation")
    lines = re.findall(r"^\[ps5-imgui-perf\] (.+)$", text, re.M)
    require(len(lines) == 1, "expected exactly one profile")
    pairs = [token.split("=", 1) for token in lines[0].split()]
    require(all(len(pair) == 2 for pair in pairs), "malformed profile field")
    fields = dict(pairs)
    stages = ["ui_ms", "clear_ms", "draw_ms", "readback_ms", "swap_ms"]
    require(len(fields) == len(pairs) and set(fields) ==
            set(stages + ["frames", "warmup", "cpu_wall_ms", "status"]), "unexpected fields")
    frames, warmup = int(fields["frames"]), int(fields["warmup"])
    require(warmup == (2 if host else 30) and frames >= 2 * warmup,
            "insufficient frames or wrong profile mode")
    values = {name: float(fields[name]) for name in stages + ["cpu_wall_ms"]}
    require(all(math.isfinite(v) and v >= 0 for v in values.values()), "invalid timing")
    require(values["cpu_wall_ms"] > 0 and
            abs(sum(values[s] for s in stages) - values["cpu_wall_ms"]) <= 0.000004,
            "phase accounting mismatch")
    require(fields["status"] == "0", "profile failed")
    probes = re.findall(r"\[ps5-imgui-tv\] readback frame=(\d+) rgba=[0-9,]+ (\w+)", text)
    require(probes == [("0", "PASS"), ("10", "PASS")], "pixel probes missing or failed")
    finished = re.findall(r"\[ps5-imgui-tv\] finished frames=(\d+) changes=\d+ status=(\d+)", text)
    require(finished == [(str(frames + warmup), "0")], "frame count or cleanup mismatch")
    require(re.findall(r"\[ps5-imgui\] finished status=(\d+)", text) == ["0"], "EGL cleanup failed")
    if not host:
        require(re.findall(r"\[pss-opengl-native\] gate completed status=(\d+)", text) == ["0"],
                "native gate incomplete")
    report = dict(mode="host-reference" if host else "PS5", frames=frames, warmup=warmup, **values)
    if deferred_batches:
        chunks = re.findall(r"\[ps5-deferred-batch\] draws=(\d+) result=0", text)
        native = re.findall(r"\[ps5-multidraw-batch\] draws=(\d+) attempted=(\d+) waits=(\d+) result=0", text)
        require(not host and len(chunks) == text.count("[ps5-deferred-batch]") ==
                len(native) == text.count("[ps5-multidraw-batch]"), "missing or failed batch receipt")
        require(all(count == draws == attempted and 0 < int(count) <= 8 and int(waits) < 2000
                    for count, (draws, attempted, waits) in zip(chunks, native)), "invalid batch retirement")
        grouped = sum(int(count) > 1 for count in chunks)
        # Aggregate coverage, not a claim that each frame had a particular group.
        require(grouped >= frames, "insufficient actual multi-draw groups")
        report["deferred_batches"] = dict(chunks=len(chunks), draws=sum(map(int, chunks)),
                                         multi_draw_chunks=grouped)
    submit_lines = re.findall(r"^\[ps5-submit-perf\] (.+)$", text, re.M)
    if submit_lines or submit_profile:
        require(not host and len(submit_lines) == 1, "expected one native submission profile")
        pairs = [token.split("=", 1) for token in submit_lines[0].split()]
        require(all(len(pair) == 2 for pair in pairs), "malformed submission field")
        fields = dict(pairs)
        phases = ["setup_ms", "scanout_flush_ms", "video_ms", "command_ms",
                  "command_flush_ms", "submit_wait_ms", "cleanup_ms"]
        metadata = ["calls", "failures", "warmup_frames", "total_ms"]
        if "sleeps" in fields:
            phases[5:6] = ["submit_ms", "suspend_ms", "poll_ms"]
            metadata.append("sleeps")
            require(0 <= int(fields["sleeps"]) <= int(fields["calls"]) * 2000,
                    "invalid completion sleep count")
        require(len(fields) == len(pairs) and set(fields) ==
                set(phases + metadata),
                "unexpected submission fields")
        calls = int(fields["calls"])
        require(calls >= frames and fields["failures"] == "0" and
                fields["warmup_frames"] == "30", "submission count/failure/warmup mismatch")
        timings = {name: float(fields[name]) for name in phases + ["total_ms"]}
        require(all(math.isfinite(v) and v >= 0 for v in timings.values()) and
                timings["total_ms"] > 0 and
                abs(sum(timings[p] for p in phases) - timings["total_ms"]) <= 0.000006,
                "invalid submission phase accounting")
        report["submission_per_call"] = dict(calls=calls, **timings)
        if "sleeps" in fields:
            report["submission_per_call"]["sleeps"] = int(fields["sleeps"])
    return report


def self_test():
    text = """[ps5-imgui-tv] readback frame=0 rgba=45,215,245,255 PASS
[ps5-imgui-tv] readback frame=10 rgba=45,215,245,255 PASS
[ps5-imgui-perf] frames=100 warmup=30 ui_ms=1 clear_ms=2 draw_ms=3 readback_ms=0 swap_ms=4 cpu_wall_ms=10 status=0
[ps5-imgui-tv] finished frames=130 changes=0 status=0
[ps5-imgui] finished status=0
[pss-opengl-native] gate completed status=0
"""
    assert summarize(text)["cpu_wall_ms"] == 10
    assert summarize(text.replace("\n", "\r\n"))["frames"] == 100
    batch = "[ps5-multidraw-batch] draws=2 attempted=2 waits=1 result=0\n[ps5-deferred-batch] draws=2 result=0\n"
    assert summarize(text + batch * 130, deferred_batches=True)["deferred_batches"]["draws"] == 260
    for bad in (text, text + batch * 99, text + batch.replace("=2", "=1") * 130,
                text + batch.replace("attempted=2", "attempted=1") * 130,
                text + batch.replace("waits=1", "waits=2000") * 130,
                text + batch.replace("result=0", "result=-1") * 130,
                text + batch * 130 + "[ps5-deferred-batch] malformed"):
        try:
            summarize(bad, deferred_batches=True)
        except ValueError:
            pass
        else:
            raise AssertionError("missing, ungrouped or failed batches accepted")
    submit = ("[ps5-submit-perf] calls=300 failures=0 warmup_frames=30 "
              "setup_ms=1 scanout_flush_ms=2 video_ms=0 command_ms=1 "
              "command_flush_ms=0 submit_wait_ms=1 cleanup_ms=1 total_ms=6\n")
    assert summarize(text + submit, submit_profile=True)["submission_per_call"]["calls"] == 300
    split_submit = submit.replace("warmup_frames=30", "warmup_frames=30 sleeps=300").replace(
        "submit_wait_ms=1", "submit_ms=0 suspend_ms=0 poll_ms=1")
    assert summarize(text + split_submit)["submission_per_call"]["sleeps"] == 300
    for bad in (text + text, text + "[ps5-gallium] clear-gpu-color status=-3 draws=1\n",
                text.replace("PASS", "FAIL", 1),
                text.replace("frames=130", "frames=129"), text.replace("warmup=30", "warmup=2"),
                text.replace("clear_ms=2", "clear_ms=nan"), text.replace("clear_ms=2", "clear_ms=-1"),
                text.replace("cpu_wall_ms=10", "cpu_wall_ms=11"),
                text.replace("status=0", "status=1", 1), text.replace("gate completed", "incomplete"),
                text.replace("clear_ms=2", "clear_ms=2 clear_ms=2"),
                text + submit + submit, text + submit.replace("calls=300", "calls=99"),
                text + submit.replace("failures=0", "failures=1"),
                text + submit.replace("warmup_frames=30", "warmup_frames=0"),
                text + submit.replace("total_ms=6", "total_ms=5"),
                text + split_submit.replace("sleeps=300", "sleeps=-1"),
                text + split_submit.replace("sleeps=300", "sleeps=600001"),
                text + submit.replace("setup_ms=1", "setup_ms=nan")):
        try:
            summarize(bad)
        except ValueError:
            pass
        else:
            raise AssertionError("invalid profile accepted")
    try:
        summarize(text, submit_profile=True)
    except ValueError:
        pass
    else:
        raise AssertionError("missing required submission profile accepted")
    print("imgui-profile: self-test PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path, nargs="?")
    parser.add_argument("--host", action="store_true")
    parser.add_argument("--submit-profile", action="store_true")
    parser.add_argument("--deferred-batches", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    else:
        if not args.receipt:
            parser.error("receipt required")
        print(json.dumps(summarize(args.receipt.read_text(), args.host, args.submit_profile,
                                   args.deferred_batches), indent=2))
