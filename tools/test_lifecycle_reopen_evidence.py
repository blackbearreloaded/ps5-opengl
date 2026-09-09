# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later
import unittest
from lifecycle_reopen_evidence import summarize


class LifecycleReopenEvidenceTests(unittest.TestCase):
    def test_ordered_runtime_waits_and_hdmi(self):
        app = ""
        for n in range(3):
            app += f"[ps5-imgui-lifecycle] session={n} begin\n"
            if n:
                app += "[ps5-output-reopen] settle_ms=5000 result=00000000\n"
            app += f"[ps5-agc] present-open handle=7\n[ps5-imgui-lifecycle] session={n} PASS\n"
            if n < 2:
                app += f"[ps5-imgui-lifecycle] settle_after={n} seconds=0\n"
        klog = "launchApp(PPSA99005)\nEXEC /app0/eboot.bin\n" + (
            "[AvControl] video: port:HDMI 2160P_11988\n"
            "[AvControl] video: port:HDMI 3840_2160P_5994\n") * 3
        self.assertEqual(summarize(app, klog, 2160)["runtime_reopens"], 2)
        for bad in (app.replace("seconds=0", "seconds=5"), app.replace("settle_ms=5000", "settle_ms=500"),
                    app.replace("result=00000000", "result=ffffffff"),
                    app.replace("[ps5-output-reopen] settle_ms=5000 result=00000000\n", "", 1),
                    "[ps5-output-reopen] settle_ms=5000 result=00000000\n" + app,
                    app.replace("session=1 PASS", "session=2 PASS"),
                    app.replace("[ps5-output-reopen] settle_ms=5000 result=00000000\n[ps5-agc] present-open handle=7",
                                "[ps5-agc] present-open handle=7\n[ps5-output-reopen] settle_ms=5000 result=00000000")):
            with self.assertRaises(ValueError):
                summarize(bad, klog, 2160)
        for bad in (klog.replace("2160P_11988", "1080P_11988"), klog + "[AvControl] ** set_hdmi_disconnect **\n",
                    klog + "[AvControl] Hdmi Event:connect (prev:discon dur:126)\n",
                    klog + "[AvControl] video: port:HDMI 2160P_11988\n"):
            with self.assertRaises(ValueError):
                summarize(app, bad, 2160)


if __name__ == "__main__":
    unittest.main()
