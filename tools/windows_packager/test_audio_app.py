import importlib.util
import os
import tempfile
import unittest
from pathlib import Path

from audio_app import convert_file


class AudioTests(unittest.TestCase):
    def test_converts_and_never_overwrites(self):
        repo = Path(os.environ.get('Z5_TEST_ASSETS', Path(__file__).resolve().parents[2])).resolve()
        tool = repo / 'tools/make_snd_wav.py'
        example = repo / 'card/SHUTTER/1.WAV'
        if not tool.exists() or not example.exists():
            self.skipTest('release assets unavailable')
        spec = importlib.util.spec_from_file_location('converter_test', tool)
        converter = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(converter)
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / '2.WAV'
            convert_file(example, out, converter)
            self.assertIsNone(converter.module_check(out.read_bytes()))
            with self.assertRaises(FileExistsError):
                convert_file(example, out, converter)

if __name__ == '__main__':
    unittest.main()
