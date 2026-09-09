# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Extra G63 checks layered on the existing pixel/memory/native-cycle audit."""
import importlib
import re

DISPLAY = importlib.import_module("summarize-display")


def summarize(app, klog, height):
    expected = [f"session={n} {phase}" for n in range(3) for phase in ("begin", "PASS")]
    if re.findall(r"^\[ps5-imgui-lifecycle\] (session=\d+ (?:begin|PASS))$", app, re.M) != expected:
        raise ValueError("G63 lifecycle sessions incomplete")
    for n in range(3):
        section = app.split(f"[ps5-imgui-lifecycle] session={n} begin\n", 1)[1].split(
            f"[ps5-imgui-lifecycle] session={n} PASS", 1)[0]
        waits = re.findall(r"^\[ps5-output-reopen\] (.*)$", section, re.M)
        if waits != ([] if n == 0 else ["settle_ms=5000 result=00000000"]):
            raise ValueError("G63 missing, failed or unnecessary runtime settling")
        if n and section.index("[ps5-output-reopen]") > section.index("[ps5-agc] present-open"):
            raise ValueError("G63 settling happened after reopen")
    if re.findall(r"^\[ps5-imgui-lifecycle\] settle_after=(\d+) seconds=(\d+)$", app, re.M) != [("0", "0"), ("1", "0")]:
        raise ValueError("G63 application supplied its own delay")
    if app.count("[ps5-output-reopen]") != 2:
        raise ValueError("G63 extra settling outside session boundaries")
    hdmi = DISPLAY.hdmi_report(klog, "PPSA99005", height * 16 // 9, height, 119.88)
    if [(r["width"], r["height"], r["refresh_hz"]) for r in hdmi["captured_hdmi_sequence"]] != [
            (height * 16 // 9, height, hz) for _ in range(3) for hz in (119.88, 59.94)]:
        raise ValueError("G63 unexpected HDMI mode sequence")
    if re.search(r"\[AvControl\].*(?:set_hdmi_(?:dis)?connect|Hdmi Event:(?:discon|connect))", klog, re.I):
        raise ValueError("G63 HDMI connection event")
    return dict(application_settle_seconds=0, runtime_reopens=2, settle_ms_per_reopen=5000,
                hdmi_mode_pairs=3, logged_hdmi_reconnects=0)
