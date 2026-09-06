"""The portable export must fail closed on damaged or incomplete evidence."""
import contextlib
import importlib
import io
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch

VERIFY = importlib.import_module("verify-published-validation")
FETCH = importlib.import_module("fetch-sources")


class PublishedValidationTest(unittest.TestCase):
    def test_export_and_tamper_rejection(self):
        with tempfile.TemporaryDirectory() as directory:
            evidence = Path(directory) / "evidence"
            shutil.copytree(VERIFY.EVIDENCE, evidence)
            sums = evidence / "SHA256SUMS"
            original = sums.read_bytes()
            with patch.object(VERIFY, "EVIDENCE", evidence), contextlib.redirect_stdout(io.StringIO()):
                VERIFY.main()
                for bad, message in [(b"", "incomplete"),
                                     (b"0" * 64 + b"  ../outside\n", "unsafe"),
                                     (original + original, "duplicate")]:
                    sums.write_bytes(bad)
                    with self.assertRaisesRegex(ValueError, message):
                        VERIFY.main()
                sums.write_bytes(original)
                (evidence / "cases.csv.gz").write_bytes(b"damaged")
                with self.assertRaisesRegex(ValueError, "checksum mismatch"):
                    VERIFY.main()

    def test_compiler_source_identity_rejection(self):
        with patch.object(FETCH, "digest", return_value="wrong"):
            with self.assertRaisesRegex(ValueError, "patch hash mismatch"):
                FETCH.verify_psbc()
        with patch.object(FETCH, "digest", return_value=FETCH.PINS["psbc_patch"]["sha256"]), \
                patch.object(FETCH, "git", return_value="wrong-tree"):
            with self.assertRaisesRegex(ValueError, "source tree mismatch"):
                FETCH.verify_psbc()


if __name__ == "__main__":
    unittest.main()
