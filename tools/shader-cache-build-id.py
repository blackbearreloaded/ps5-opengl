#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Invalidate native shader records whenever compiler objects or driver inputs change."""
import hashlib
from pathlib import Path
import subprocess
import sys

digest = hashlib.sha256()
for name in sys.argv[2:]:
    path = Path(name).resolve()
    data = path.read_bytes()
    digest.update(len(data).to_bytes(8, 'little'))
    digest.update(data)
    if data.startswith(b'!<thin>\n'):
        for member in subprocess.check_output(['ar', 't', str(path)], text=True).splitlines():
            member = Path(member)
            data = (member if member.is_absolute() else path.parent / member).read_bytes()
            digest.update(len(data).to_bytes(8, 'little'))
            digest.update(data)
output = Path(sys.argv[1])
text = '#define PS5_SHADER_CACHE_BUILD_ID "' + digest.hexdigest() + '"\n'
if not output.exists() or output.read_text() != text:
    output.write_text(text)
