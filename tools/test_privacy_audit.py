#!/usr/bin/env python3
"""Exercise the privacy audit with isolated, synthetic Git repositories."""
import contextlib
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import privacy_audit


ROOT = Path(__file__).resolve().parents[1]
FIXTURE_PATH = "app/core/MediaHttpsSmoke.h"
FIXTURE = (ROOT / FIXTURE_PATH).read_bytes()
KEY_HEADER = b"-----BEGIN " + b"RSA PRIVATE KEY-----"
KEY_FOOTER = b"-----END " + b"RSA PRIVATE KEY-----"


class PrivacyAuditTests(unittest.TestCase):
    def audit(self, data, path=FIXTURE_PATH):
        scratch = ROOT / ".tmp"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="privacy-audit-", dir=scratch) as temporary:
            root = Path(temporary)
            target = root / path
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            subprocess.run(["git", "init", "--quiet"], cwd=root, check=True)
            subprocess.run(["git", "add", "--", path], cwd=root, check=True)
            output = io.StringIO()
            with patch.object(privacy_audit, "ROOT", root), \
                    patch.object(sys, "argv", ["privacy_audit.py"]), \
                    contextlib.redirect_stdout(output):
                result = privacy_audit.main()
            return result, output.getvalue()

    def assert_private_key_rejected(self, data, path=FIXTURE_PATH):
        result, output = self.audit(data, path)
        self.assertEqual(result, 1)
        self.assertIn("private-key:", output)
        self.assertNotIn(KEY_HEADER.decode(), output)

    def test_fixed_localhost_fixture_is_accepted(self):
        result, output = self.audit(FIXTURE)
        self.assertEqual(result, 0)
        self.assertIn("findings: 0", output)

    def test_added_unknown_private_key_is_rejected(self):
        unknown = KEY_HEADER + b"\nsynthetic-unknown-key\n" + KEY_FOOTER + b"\n"
        self.assert_private_key_rejected(FIXTURE + unknown)

    def test_one_changed_key_byte_is_rejected(self):
        offset = FIXTURE.index(KEY_HEADER) + len(KEY_HEADER) + 1
        replacement = b"A" if FIXTURE[offset:offset + 1] != b"A" else b"B"
        changed = FIXTURE[:offset] + replacement + FIXTURE[offset + 1:]
        self.assert_private_key_rejected(changed)

    def test_same_key_at_another_path_is_rejected(self):
        self.assert_private_key_rejected(FIXTURE, "app/core/OtherSmoke.h")

    def test_incomplete_key_header_before_or_after_fixture_is_rejected(self):
        for data in (KEY_HEADER + b"\n" + FIXTURE, FIXTURE + KEY_HEADER):
            with self.subTest(position="before" if data.startswith(KEY_HEADER) else "after"):
                self.assert_private_key_rejected(data)

    def test_damaged_key_footer_is_rejected(self):
        self.assert_private_key_rejected(FIXTURE.replace(KEY_FOOTER, b"invalid-footer", 1))

    def test_fixture_does_not_hide_github_token(self):
        token = b"ghp_" + b"a" * 36
        result, output = self.audit(FIXTURE + token)
        self.assertEqual(result, 1)
        self.assertIn("github-token:", output)
        self.assertNotIn(token.decode(), output)


if __name__ == "__main__":
    unittest.main()
