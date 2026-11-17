"""Configuration startup checks; only new temporary databases are used."""
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

SERVER = sys.argv.pop(1)


class StartupConfigTests(unittest.TestCase):
    def invoke(self, *args, cwd):
        return subprocess.run([SERVER, *map(str, args)], cwd=cwd, capture_output=True, timeout=20)

    def test_config_database_is_relative_to_config_not_working_directory(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            configured = root / "config"
            configured.mkdir()
            ini = configured / "server.ini"
            ini.write_text("[server]\nlisten=127.0.0.1\nport=8765\ndatabase=demo.sqlite\n", encoding="utf-8")
            result = self.invoke("--config", ini, "--init", cwd=root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue((configured / "demo.sqlite").is_file())
            self.assertFalse((root / "warehouse.sqlite").exists())
            override = root / "override.sqlite"
            result = self.invoke("--config", ini, "--db", override, "--init", cwd=root)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertTrue(override.is_file())
            ephemeral = root / "ephemeral.sqlite"
            result = self.invoke("--db", ephemeral, "--port", "0", "--init", cwd=root)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_missing_or_invalid_config_does_not_create_database(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            missing = self.invoke("--config", root / "missing.ini", "--init", cwd=root)
            self.assertNotEqual(missing.returncode, 0)
            ini = root / "bad.ini"
            for content in ("[server]\nport=0\ndatabase=bad.sqlite\n",
                            "[server]\nport=no\ndatabase=bad.sqlite\n",
                            "[server]\nlisten=garbage\ndatabase=bad.sqlite\n",
                            "[server]\nlisten=127.0.0.1\nport=8765\n"):
                ini.write_text(content, encoding="utf-8")
                self.assertNotEqual(self.invoke("--config", ini, "--init", cwd=root).returncode, 0)
            self.assertFalse((root / "bad.sqlite").exists())
            self.assertFalse((root / "warehouse.sqlite").exists())

    def test_configured_missing_database_is_not_silently_created_on_launch(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            ini = root / "server.ini"
            ini.write_text("[server]\nlisten=127.0.0.1\nport=8765\ndatabase=absent.sqlite\n", encoding="utf-8")
            result = self.invoke("--config", ini, cwd=root)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse((root / "absent.sqlite").exists())


if __name__ == "__main__":
    unittest.main()
