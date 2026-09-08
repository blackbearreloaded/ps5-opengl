#!/usr/bin/env python3
"""Audit owned-heap observations; does not measure GPU memory or process RSS."""
import argparse
import json
from pathlib import Path
import re


def summarize(text, sessions=1, steady_samples=0):
    pattern = (r"^\[pss-opengl-heap\] phase=(begin|steady|end) sample=(\d+) state=(-?\d+) "
               r"live_bytes=(\d+) peak_bytes=(\d+) blocks=(\d+) failures=(\d+) "
               r"ambiguous_zero_reallocs=(\d+)$")
    rows = [(phase, *map(int, values)) for phase, *values in re.findall(pattern, text, re.M)]
    if len(rows) != text.count("[pss-opengl-heap]") or not rows:
        raise ValueError("missing or malformed heap samples")
    groups, current = [], None
    peak = 0
    for row in rows:
        phase, sample, state, live, high, blocks, failures, ambiguous = row
        if failures or ambiguous or high < max(peak, live) or (state != 2 and (phase != "begin" or state != 0)):
            raise ValueError("invalid/inconclusive heap counters or allocation failure")
        peak = high
        if phase == "begin":
            if current is not None or sample != 0:
                raise ValueError("overlapping heap sessions")
            current = []
        elif current is None:
            raise ValueError("heap sample outside a session")
        current.append(row)
        if phase == "end":
            if sample != 0:
                raise ValueError("invalid end sample")
            groups.append(current)
            current = None
    if current is not None or len(groups) != sessions:
        raise ValueError("incomplete or unexpected heap sessions")
    reports = []
    for group in groups:
        steady = [r for r in group if r[0] == "steady"]
        if len(steady) != steady_samples or any(a[1] >= b[1] for a, b in zip(steady, steady[1:])):
            raise ValueError("missing/duplicate steady heap samples")
        if steady and steady[0][1] != 0:
            raise ValueError("missing initial steady sample")
        # Exclude the initial frame; subsequent samples follow 30-second warm-up.
        live = [r[3] for r in steady[1:]]
        reports.append(dict(begin_bytes=group[0][3], end_bytes=group[-1][3],
                            end_blocks=group[-1][5], peak_bytes=group[-1][4],
                            steady_samples=len(steady),
                            steady_growth_bytes=live[-1] - live[0] if len(live) >= 2 else None,
                            steady_range_bytes=max(live) - min(live) if live else None))
    return dict(scope="owned heap usable bytes; excludes GPU mappings, foreign heaps and RSS",
                sessions=reports, post_session_growth_bytes=groups[-1][-1][3] - groups[0][-1][3])


def self_test():
    def row(phase, sample=0, live=16, blocks=1):
        return (f"[pss-opengl-heap] phase={phase} sample={sample} state=2 live_bytes={live} "
                f"peak_bytes=64 blocks={blocks} failures=0 ambiguous_zero_reallocs=0\n")
    text = row("begin") + row("steady") + row("steady", 1800) + row("steady", 3600) + row("end")
    assert summarize(text, steady_samples=3)["sessions"][0]["steady_growth_bytes"] == 0
    assert summarize(row("begin") + row("end") + row("begin") + row("end", live=32),
                     sessions=2)["post_session_growth_bytes"] == 16
    for bad in ("", text + row("end"), text.replace("state=2", "state=-1"),
                text.replace("failures=0", "failures=1"), text.replace("phase=end", "phase=steady"),
                text.replace("ambiguous_zero_reallocs=0", "ambiguous_zero_reallocs=1"),
                text.replace("sample=3600", "sample=1800"), text.replace("peak_bytes=64", "peak_bytes=1")):
        try:
            summarize(bad, steady_samples=3)
        except ValueError:
            continue
        raise AssertionError("invalid heap receipt accepted")
    print("app-heap audit: self-test PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path, nargs="?")
    parser.add_argument("--sessions", type=int, default=1)
    parser.add_argument("--steady-samples", type=int, default=0)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    elif not args.receipt:
        parser.error("receipt required")
    else:
        print(json.dumps(summarize(args.receipt.read_text(), args.sessions, args.steady_samples), indent=2))
