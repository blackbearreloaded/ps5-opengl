# PS5 OpenGL - OpenGL implementation for PlayStation 5.
# Copyright (C) 2026 BlackBearReloaded
# SPDX-License-Identifier: GPL-3.0-or-later

"""Check local links in the project's hand-written Markdown guides."""
from pathlib import Path
import re
import unittest
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]


def prose(path):
    return re.sub(r"```.*?```", "", path.read_text(encoding="utf-8"), flags=re.S)


class DocumentationTest(unittest.TestCase):
    def test_local_links(self):
        pages = [ROOT / "README.md", ROOT / "CONTRIBUTING.md"]
        for folder in ("docs", "examples", "integration", "native-app"):
            pages.extend((ROOT / folder).rglob("*.md"))
        for page in pages:
            for href in re.findall(r"\]\(([^\s)]+)\)", prose(page)):
                link = urlsplit(href)
                if link.scheme or link.netloc:
                    continue
                with self.subTest(page=str(page.relative_to(ROOT)), href=href):
                    target = (page.parent / unquote(link.path)).resolve() if link.path else page
                    self.assertTrue(target.exists(), "missing local link target")
                    if target.suffix == ".md" and link.fragment:
                        headings = re.findall(r"^#{1,6}\s+(.+)$", prose(target), flags=re.M)
                        anchors = {re.sub(r"[^\w\s-]", "", h.lower()).replace(" ", "-")
                                   for h in headings}
                        self.assertIn(unquote(link.fragment), anchors, "missing heading anchor")


if __name__ == "__main__":
    unittest.main()
