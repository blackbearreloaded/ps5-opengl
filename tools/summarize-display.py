#!/usr/bin/env python3
# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Correlate a bounded window profile with its captured HDMI negotiation."""
import argparse
import hashlib
import importlib
import json
from pathlib import Path
import re

PROFILE = importlib.import_module('summarize-imgui-profile')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def hdmi_report(klog, title, width, height, active_hz):
    require(re.fullmatch(r'PPSA\d{5}', title), 'invalid title')
    require(re.findall(r'launchApp\((PPSA\d{5})\)', klog) == [title],
            'capture must contain exactly one launch of the tested title')
    require(klog.count('EXEC /app0/eboot.bin') == 1, 'native entry missing or duplicated')
    require(not re.search(r'kernel panic|GPU fault|GPU hang|VM fault|A user thread receives a fatal signal|App Crash',
                          klog, re.I), 'capture contains a runtime fault')
    # Exclude a pre-entry display change; never borrow a mode from another run.
    text = klog.split('EXEC /app0/eboot.bin', 1)[1]
    lines = re.findall(r'\[AvControl\][ \t]+video: port:HDMI ([^\r\n]*)', text)
    rows = []
    for line in lines:
        mode = line.split()[0] if line.strip() else ''
        match = re.fullmatch(r'(?:(1920|2560|3840)_)?(1080|1440|2160)P_(5994|6000|8991|9000|11988|12000)', mode)
        row = dict(mode=mode, details=line, width=None, height=None, refresh_hz=None)
        if match:
            prefix, y, rate = match.groups()
            x = {1080: 1920, 1440: 2560, 2160: 3840}[int(y)]
            if prefix is None or int(prefix) == x:
                row.update(width=x, height=int(y), refresh_hz=int(rate) / 100)
        rows.append(row)
    active = [i for i, row in enumerate(rows)
              if row['refresh_hz'] is not None and abs(row['refresh_hz'] - active_hz) < 0.2]
    stable = False
    selected = None
    restored = False
    if rows and all(row['width'] is not None for row in rows) and active:
        first, last = active[0], active[-1]
        identities = {(rows[i]['width'], rows[i]['height'], rows[i]['refresh_hz']) for i in active}
        # A 60-Hz interruption or a resolution change during HFR is ambiguous.
        stable = len(identities) == 1 and active == list(range(first, last + 1))
        restored = last + 1 < len(rows) and all(abs(row['refresh_hz'] - 59.94) < 0.2 for row in rows[last + 1:])
        if stable and restored:
            selected = rows[first]
    match = selected is not None and (selected['width'], selected['height']) == (width, height)
    return dict(classification=('verified-match' if match else 'verified-mismatch') if selected else 'inconclusive',
                render_size=dict(width=width, height=height), negotiated_active=selected,
                captured_hdmi_sequence=rows, stable_high_refresh=stable, restored_60hz=restored,
                render_size_matches_hdmi=match if selected else None,
                sink_independently_verified=False,
                scope='Console HDMI negotiation log, not a TV/capture-device measurement or a guarantee of every displayed frame')


def summarize(receipt, height):
    require(receipt.name.endswith('-opengl.log'), 'use an exact native runner receipt')
    prefix = str(receipt).removesuffix('-opengl.log')
    paths = {kind: Path(prefix + suffix) for kind, suffix in
             [('klog', '-klog.log'), ('cycle', '-result.json'), ('runner', '-runner.json')]}
    cycle, runner = [json.loads(paths[k].read_text(encoding='utf-8-sig')) for k in ('cycle', 'runner')]
    require(cycle['outcome'] == 'entered-eboot' and cycle['teardownSignal'] == 'runtime-layers-released' and
            runner['postHealthChecked'] is True and runner['lockReleased'] is True,
            'cycle must have clean teardown, health and exact-token release')
    require(runner['gate'] == 'egl_public_core33_imgui_tv.o', 'wrong native workload')
    profile = PROFILE.summarize(receipt.read_text(), window_target=120, window_height=height,
                                output_status=True, prepare_profile=True)
    report = hdmi_report(paths['klog'].read_text(encoding='utf-8-sig'), cycle['titleId'],
                         height * 16 // 9, height, profile['videoout_status']['reported_refresh_hz'])
    report.update(window_benchmark=profile['window_benchmark'], videoout_status=profile['videoout_status'],
                  title=cycle['titleId'], eboot_sha256=cycle['ebootSha256'].lower(),
                  source_companion=runner['checkoutCommit'], clean_cycle=True,
                  raw_sha256={kind: hashlib.sha256(path.read_bytes()).hexdigest()
                              for kind, path in dict(paths, app=receipt).items()})
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('receipt', type=Path)
    parser.add_argument('--height', type=int, choices=(1080, 1440, 2160), default=2160)
    args = parser.parse_args()
    report = summarize(args.receipt, args.height)
    print(json.dumps(report, indent=2))
    raise SystemExit(2 if report['classification'] == 'inconclusive' else 0)
