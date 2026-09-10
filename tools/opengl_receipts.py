# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Read historical receipt names without rewriting their bytes or checksums."""

LEGACY_NAME = "pss-opengl"
CTS_QPA_SUFFIXES = ("-ps5-opengl-cts.qpa", f"-{LEGACY_NAME}-cts.qpa")


def normalize_log_tags(text):
    """Normalize tags for parsing only; hash and archive the original input."""
    return text.replace(f"[{LEGACY_NAME}", "[ps5-opengl")


def cts_receipt_parts(path):
    """Keep every sibling in the same namespace as its selected QPA receipt."""
    name = str(path)
    for suffix in CTS_QPA_SUFFIXES:
        if name.endswith(suffix):
            return name[:-len(suffix)], suffix[1:-len("-cts.qpa")]
    raise ValueError(f"not a native OpenGL CTS receipt: {path}")
