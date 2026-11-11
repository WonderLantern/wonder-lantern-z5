import tempfile
import os
import unittest
from pathlib import Path
from unittest.mock import patch

import app


class AppTests(unittest.TestCase):
    def test_automatic_folder_avoids_existing_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            first = app.new_output(tmp)
            first.mkdir()
            second = app.new_output(tmp)
            self.assertNotEqual(first, second)
            self.assertFalse(second.exists())

    def test_wrong_firmware_produces_no_result_folder(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp)
            source = base / 'bad.bin'
            source.write_bytes(b'not firmware')
            assets = Path(os.environ.get('Z5_TEST_ASSETS', Path(__file__).resolve().parents[2])).resolve()
            if not (assets / 'patch.py').exists():
                self.skipTest('run integration case with local release checkout')
            with patch.object(app, 'ASSETS', assets):
                with self.assertRaises(SystemExit):
                    app.generate(source, base / 'result', assets)
            self.assertFalse((base / 'result').exists())


if __name__ == '__main__':
    unittest.main()
