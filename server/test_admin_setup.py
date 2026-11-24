"""First-admin CLI regression checks against isolated temporary SQLite files."""
from concurrent.futures import ThreadPoolExecutor
from contextlib import closing
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest

SERVER = sys.argv.pop(1)


class AdminBootstrapTests(unittest.TestCase):
    def invoke(self, path, data, *options):
        return subprocess.run([SERVER, "--db", str(path), *options], input=data.encode("utf-8"),
                              capture_output=True, timeout=20)

    def test_first_admin_is_created_once_without_printing_password(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "accounts.sqlite"
            result = self.invoke(path, "operator\nsecret-test-123\nsecret-test-123\n", "--setup-admin")
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn(b"secret-test-123", result.stdout + result.stderr)
            second = self.invoke(path, "another\nsecret-test-123\nsecret-test-123\n", "--setup-admin")
            self.assertNotEqual(second.returncode, 0)
            with closing(sqlite3.connect(path)) as db:
                self.assertEqual(db.execute("SELECT COUNT(*) FROM network_users").fetchone()[0], 1)
                self.assertEqual(db.execute("SELECT COUNT(*) FROM network_user_access WHERE is_admin=1").fetchone()[0], 1)
                self.assertEqual(db.execute("SELECT action FROM admin_audit").fetchone()[0], "initialize_admin")
                self.assertTrue(db.execute("SELECT password_hash FROM network_users").fetchone()[0].startswith("$argon2"))

    def test_confirmation_eof_and_short_password_leave_no_account(self):
        for data in ("operator\nsecret-test-123\ndifferent\n", "operator\nsecret-test-123\n",
                     "operator\n123\n123\n", "\nsecret-test-123\nsecret-test-123\n"):
            with self.subTest(data=data), tempfile.TemporaryDirectory() as temp:
                path = Path(temp) / "accounts.sqlite"
                self.assertNotEqual(self.invoke(path, data, "--setup-admin").returncode, 0)
                with closing(sqlite3.connect(path)) as db:
                    self.assertEqual(db.execute("SELECT COUNT(*) FROM network_users").fetchone()[0], 0)

    def test_unicode_passwords_use_six_characters_in_terminal_too(self):
        for password, accepted in (("12345", False), ("中文", False), ("一二三四五", False),
                                   ("😀😀😀", False), ("123456", True), ("一二三四五六", True),
                                   ("😀😀😀😀😀😀", True)):
            with self.subTest(password=password), tempfile.TemporaryDirectory() as temp:
                path = Path(temp) / "accounts.sqlite"
                result = self.invoke(path, f"operator\n{password}\n{password}\n", "--setup-admin")
                self.assertEqual(result.returncode == 0, accepted, result.stderr)
                with closing(sqlite3.connect(path)) as db:
                    self.assertEqual(db.execute("SELECT COUNT(*) FROM network_users").fetchone()[0], int(accepted))

    def test_existing_normal_account_is_not_promoted_and_legacy_input_works(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "accounts.sqlite"
            self.assertEqual(self.invoke(path, "secret-test-123\n", "--add-user", "reader").returncode, 0)
            self.assertNotEqual(self.invoke(path, "reader\nsecret-test-123\nsecret-test-123\n", "--setup-admin").returncode, 0)
            self.assertEqual(self.invoke(path, "secret-test-123\n", "--add-admin", "maintenance").returncode, 0)
            with closing(sqlite3.connect(path)) as db:
                self.assertEqual(list(db.execute("SELECT is_admin FROM network_user_access ORDER BY user_id")), [(0,), (1,)])

    def test_two_initializers_cannot_both_succeed(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "accounts.sqlite"
            self.assertEqual(self.invoke(path, "", "--init").returncode, 0)
            with ThreadPoolExecutor(max_workers=2) as pool:
                jobs = [pool.submit(self.invoke, path, f"operator{i}\nsecret-test-123\nsecret-test-123\n", "--setup-admin")
                        for i in range(2)]
                results = [job.result() for job in jobs]
            self.assertEqual(sum(result.returncode == 0 for result in results), 1)
            with closing(sqlite3.connect(path)) as db:
                self.assertEqual(db.execute("SELECT COUNT(*) FROM network_user_access WHERE is_admin=1").fetchone()[0], 1)


if __name__ == "__main__":
    unittest.main()
