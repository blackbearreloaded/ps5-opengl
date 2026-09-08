# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

import importlib
import unittest

DISPLAY = importlib.import_module('summarize-display')


class DisplayTests(unittest.TestCase):
    def test_modes_are_not_inferred_from_render_size_or_another_launch(self):
        begin = 'launchApp(PPSA99005)\nEXEC /app0/eboot.bin\n'
        def line(mode):
            return '<118>[AvControl]  video: port:HDMI ' + mode + ' YUV422 limited\n'
        def report(modes, prefix=begin):
            return DISPLAY.hdmi_report(prefix + ''.join(map(line, modes)), 'PPSA99005', 3840, 2160, 119.88)
        restore = '3840_2160P_5994'
        for mode in ('2160P_11988', '3840_2160P_11988'):
            good = report([mode, mode, restore])
            self.assertEqual(good['classification'], 'verified-match')
            self.assertEqual(good['negotiated_active']['refresh_hz'], 119.88)
            self.assertFalse(good['sink_independently_verified'])
        mismatch = report(['1080P_11988', restore])
        self.assertEqual(mismatch['classification'], 'verified-mismatch')
        self.assertEqual(mismatch['negotiated_active']['height'], 1080)
        self.assertFalse(mismatch['render_size_matches_hdmi'])
        for modes in ([], [restore], ['2160P_11988'], ['unknown', '2160P_11988', restore],
                      ['', '2160P_11988', restore],
                      ['1080P_11988', '2160P_11988', restore],
                      ['2160P_11988', restore, '2160P_11988', restore],
                      ['1920_2160P_11988', restore]):
            self.assertEqual(report(modes)['classification'], 'inconclusive', modes)
        self.assertEqual(report([restore], 'launchApp(PPSA99005)\n' + line('2160P_11988') +
                                'EXEC /app0/eboot.bin\n')['classification'], 'inconclusive')
        for prefix in ('', begin.replace('PPSA99005', 'PPSA99007'), begin + begin,
                       begin + 'launchApp(PPSA99007)\n', begin + 'GPU fault\n'):
            with self.assertRaises(ValueError):
                report(['2160P_11988', restore], prefix)


if __name__ == '__main__':
    unittest.main()
