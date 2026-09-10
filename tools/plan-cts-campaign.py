#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Prepare a complete timed engineering queue; never run or certify tests."""
import argparse
from collections import Counter
import hashlib
import importlib
import json
import math
from pathlib import Path

PREPARE = importlib.import_module("prepare-cts-shard")
ROOT = Path(__file__).resolve().parents[1]
MUSTPASS = ROOT / "third_party/VK-GL-CTS/external/openglcts/data/gl_cts/data/mustpass/gl/khronos_mustpass/main/gl33-main.txt"


def plan(cases, history, smoke, budget=120, startup=15, margin=1.5, unknown=30):
    if (not cases or len(cases) != len(set(cases)) or not set(smoke) <= set(cases) or
            any(not math.isfinite(v) for v in (budget, startup, margin, unknown)) or
            not (budget > startup >= 0 and margin >= 1 and unknown > 0)):
        raise ValueError("invalid inventory or time budget")
    capacity = (budget - startup) / margin
    shards = []
    for config in range(len(PREPARE.CONFIGURATIONS)):
        rows = history.get(str(config), {})
        timings, fallback, previous_failures = {}, set(), set()
        for name in cases:
            row = rows.get(name, {})
            value = row.get("seconds")
            if value is not None and (isinstance(value, bool) or not isinstance(value, (int, float)) or
                                      not math.isfinite(value) or value < 0):
                raise ValueError("invalid timing: " + name)
            if row.get("status") in {"Pass", "NotSupported"} and value is not None:
                timings[name] = value
            else:
                # No credit for history; failed/incomplete durations cannot estimate a passing run.
                fallback.add(name)
                if row and row.get("status") not in {"Pass", "NotSupported"}:
                    previous_failures.add(name)
        groups = {lane: [] for lane in ("smoke", "previous-failure", "long", "bulk")}
        for name in cases:
            seconds = timings.get(name, unknown)
            lane = ("long" if seconds > capacity else "previous-failure" if name in previous_failures else
                    "smoke" if name in smoke else "bulk")
            groups[lane].append(name)
        for lane, remaining in groups.items():
            while remaining:
                if lane in {"long", "previous-failure"}:
                    selected = remaining[:1]
                    seconds = timings.get(selected[0], unknown)
                else:
                    # Existing ordered splitter; no change to iterations, seeds or test bodies.
                    selected, seconds = PREPARE.timed_prefix(
                        remaining, {name: timings.get(name, unknown) for name in remaining}, capacity, unknown)
                observation = max(5, math.ceil(startup + margin * seconds))
                shards.append(dict(configuration=config, lane=lane, cases=selected,
                    estimated_test_seconds=round(seconds, 6), observation_seconds=observation,
                    needs_duration_approval=observation > budget,
                    exceeds_runner_limit=observation > 3600,
                    unknown_timings=sum(name in fallback for name in selected)))
                remaining = remaining[len(selected):]
    priorities = {"smoke": 0, "previous-failure": 1, "long": 2, "bulk": 3}
    shards.sort(key=lambda row: (priorities[row["lane"]], row["configuration"]))
    for config in range(len(PREPARE.CONFIGURATIONS)):
        covered = Counter(name for row in shards if row["configuration"] == config for name in row["cases"])
        if covered != Counter(cases):
            raise ValueError("lost or duplicated case in schedule")
    for index, row in enumerate(shards):
        row["id"] = f'{index:04d}-config{row["configuration"]}-{row["lane"]}'
    return shards


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timings", type=Path, action="append", required=True,
                        help="inventory JSONs, oldest first; scheduling hints, never acceptance")
    parser.add_argument("--output", type=Path, required=True, help="new directory; never overwritten")
    parser.add_argument("--budget-seconds", type=float, default=120)
    parser.add_argument("--startup-seconds", type=float, default=15)
    parser.add_argument("--margin", type=float, default=1.5)
    args = parser.parse_args()
    try:
        cases = MUSTPASS.read_text().splitlines()
        suites = json.loads((ROOT / "tests/ps5/cts-regressions.json").read_text())
        smoke = PREPARE.select_cases(cases, suites["smoke"])
        history, sources = {}, []
        for path in args.timings:
            raw = path.read_bytes()
            ledger = json.loads(raw)
            if ledger.get("scope") != "development-history-not-release-certification":
                raise ValueError("expected a CTS inventory ledger: " + str(path))
            for config, rows in ledger["cases"].items():
                history.setdefault(config, {}).update(rows)
            sources.append(dict(file=path.name, sha256=hashlib.sha256(raw).hexdigest()))
        shards = plan(cases, history, smoke, args.budget_seconds, args.startup_seconds, args.margin)
        revision = json.loads((ROOT / "dependencies.json").read_text())["repositories"]["VK-GL-CTS"]["revision"]
        output = args.output.resolve()
        output.mkdir(parents=True, exist_ok=False)
        for row in shards:
            folder = output / "shards" / row["id"]
            folder.mkdir(parents=True)
            data = {"cts-shard.txt": ("\n".join(row["cases"]) + "\n").encode(),
                    "cts-args.txt": PREPARE.encode_arguments(row["configuration"])}
            row["input_sha256"] = {}
            for name, content in data.items():
                (folder / name).write_bytes(content)
                row["input_sha256"][name] = hashlib.sha256(content).hexdigest()
        result = dict(format="ps5-opengl-cts-plan-v1",
            scope="pinned-inventory engineering rehearsal; NOT an official submission schedule",
            ready_for_console=False, candidate_frozen=False, completed_executions=0,
            blockers=["select approved CTS release and audit patches",
                      "enumerate required EGL/window configurations and official runner summary",
                      "freeze current SDK and native app with matching names",
                      "obtain explicit duration exceptions for long cases"],
            mustpass_sha256=hashlib.sha256(MUSTPASS.read_bytes()).hexdigest(),
            unique_cases=len(cases), planned_executions=sum(len(row["cases"]) for row in shards),
            cts_revision=revision,
            budget_seconds=args.budget_seconds, startup_seconds=args.startup_seconds, margin=args.margin,
            historical_estimated_test_hours=sum(row["estimated_test_seconds"] for row in shards)/3600,
            observation_budget_hours=sum(row["observation_seconds"] for row in shards)/3600,
            note="Excludes deploy/readback/teardown/lock waits. Old timings are not a current-runtime benchmark.",
            timing_sources=sources, configurations=PREPARE.CONFIGURATIONS, shards=shards)
        raw = (json.dumps(result, indent=2) + "\n").encode()
        (output / "plan.json").write_bytes(raw)
        (output / "plan.sha256").write_text(hashlib.sha256(raw).hexdigest() + "  plan.json\n")
        lines = ["# CTS engineering queue", "", result["scope"], "",
                 "**Not ready for console execution:** resolve the preflight in the local PLAN.md first.",
                 "", f'- Inventory: {len(cases):,} cases; {result["planned_executions"]:,} configuration/case pairs.',
                 f'- Frozen selections: {len(shards)} shards; zero cases omitted or duplicated.',
                 f'- Historical test-time estimate: {result["historical_estimated_test_hours"]:.2f} hours.',
                 f'- Observation budgets including margin: {result["observation_budget_hours"]:.2f} hours, plus external overhead.',
                 "- Completed on this candidate: **0**. Historical results supply timings only.", "",
                 "| Lane | Shards | Executions | Historical seconds |", "|---|---:|---:|---:|"]
        for lane in ("smoke", "previous-failure", "long", "bulk"):
            rows = [row for row in shards if row["lane"] == lane]
            lines.append(f'| {lane} | {len(rows)} | {sum(len(row["cases"]) for row in rows)} | '
                         f'{sum(row["estimated_test_seconds"] for row in rows):.1f} |')
        lines += ["", "## Duration exceptions", "",
                  "These cases remain mandatory. No automatic timeout extension or exclusion.", "",
                  "| Config | Case | Historical seconds | Proposed observation seconds | Runner change needed |",
                  "|---:|---|---:|---:|---|"]
        for row in shards:
            if row["needs_duration_approval"]:
                lines.append(f'| {row["configuration"]} | {row["cases"][0]} | '
                             f'{row["estimated_test_seconds"]:.1f} | {row["observation_seconds"]} | '
                             f'{row["exceeds_runner_limit"]} |')
        (output / "SUMMARY.md").write_text("\n".join(lines) + "\n")
        print("\n".join(lines[:21]))
        return 0
    except (OSError, ValueError, KeyError, TypeError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    raise SystemExit(main())
