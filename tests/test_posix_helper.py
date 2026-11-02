"""POSIX helper safety tests. Run with python3 -m unittest discover -s tests -p test_posix_helper.py."""
import base64
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

HELPER = Path(__file__).resolve().parents[1] / "server" / "tc-posix-symlink-helper.py"


def frame(share, source, destination):
    fields = (share, source, destination)
    return ("TCS-POSIX-1\n" + "".join(
        base64.b64encode(v.encode("utf-8")).decode("ascii") + "\n" for v in fields)).encode("ascii")


class PosixSymlinkHelperTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "Test"
        self.root.mkdir()
        (self.root / "My Target").mkdir()
        (self.root / "links").mkdir()
        (self.root / "ordinary.txt").write_text("keep")

    def invoke(self, share, source, destination):
        return subprocess.run(
            [sys.executable, str(HELPER), "--share", "Test", "--root", str(self.root)],
            input=frame(share, source, destination),
            capture_output=True, timeout=5, check=False)

    def test_created_verified_relative_directory_link(self):
        result = self.invoke("test", "My Target", "links/Folder Link")
        self.assertEqual(result.stdout.strip(), b"TCS-POSIX-OK")
        path = self.root / "links" / "Folder Link"
        self.assertTrue(path.is_symlink())
        self.assertEqual(os.readlink(path), "../My Target")
        self.assertTrue(path.is_dir())

    def test_existing_file_preserved(self):
        result = self.invoke("Test", "My Target", "ordinary.txt")
        self.assertEqual(result.stdout.strip(), b"TCS-POSIX-ERR:already-exists")
        self.assertEqual((self.root / "ordinary.txt").read_text(), "keep")

    def test_cross_share_denied(self):
        result = self.invoke("Other", "My Target", "links/Bad")
        self.assertEqual(result.stdout.strip(), b"TCS-POSIX-ERR:share-mismatch")

    def test_invalid_and_outside_paths_denied(self):
        for source, dest in (
            ("My Target", "../outside"),
            ("../bad", "links/bad"),
            ("My Target", "/tmp/bad"),
            ("My Target", "links//bad"),
            ("ordinary.txt", "links/bad"),
        ):
            result = self.invoke("Test", source, dest)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((self.root / "links" / "bad").exists())

    def test_symlink_parent_denied(self):
        (self.root / "alias").symlink_to(self.root / "links", target_is_directory=True)
        result = self.invoke("Test", "My Target", "alias/bad")
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((self.root / "links" / "bad").exists())

    def test_unicode_name(self):
        result = self.invoke("Test", "My Target", "links/שָׁלוֹם Папка")
        self.assertEqual(result.stdout.strip(), b"TCS-POSIX-OK")
        self.assertTrue((self.root / "links" / "שָׁלוֹם Папка").is_symlink())


if __name__ == "__main__":
    unittest.main()
