import hashlib
import json
import tempfile
import unittest
import zipfile
from pathlib import Path

from release_data import STOCK_SHA256, import_update, verify_installed


class UpdateTests(unittest.TestCase):
    def make_package(self, root, *, bad_hash=False, extra=None):
        payload = b'example'
        manifest = {'format': 1, 'version': '1.0', 'files': {'patch_data/patches.json': hashlib.sha256(payload).hexdigest()}}
        if bad_hash:
            manifest['files']['patch_data/patches.json'] = '0' * 64
        path = root / 'update.zip'
        with zipfile.ZipFile(path, 'w') as z:
            z.writestr('manifest.json', json.dumps(manifest))
            z.writestr('patch_data/patches.json', payload)
            if extra:
                z.writestr(extra, b'x')
        return path

    def test_reject_bad_hash_without_installing(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaises(ValueError):
                import_update(self.make_package(root, bad_hash=True), root / 'versions')
            self.assertFalse((root / 'versions' / '1.0').exists())

    def test_reject_extra_traversal(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaises(ValueError):
                import_update(self.make_package(root, extra='../escape'), root / 'versions')

    def test_missing_required_files_is_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            with self.assertRaises(ValueError):
                import_update(self.make_package(root), root / 'versions')

    def test_valid_bundle_installed_and_cannot_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            card = b'card module'
            sounds = {f'{i}.WAV': bytes([i]) for i in range(1, 4)}
            data = {'camera': 'Nikon Z 5', 'firmware': 'C 1.50',
                    'stock_package_sha256': STOCK_SHA256,
                    'card_module_sha256': hashlib.sha256(card).hexdigest(),
                    'card_sounds': {k: hashlib.sha256(v).hexdigest() for k, v in sounds.items()}}
            files = {'patch_data/patches.json': json.dumps(data).encode(), 'card/Z5APP.BIN': card}
            files.update({'card/SHUTTER/' + k: v for k, v in sounds.items()})
            manifest = {'format': 1, 'version': 'v1', 'files': {k: hashlib.sha256(v).hexdigest() for k, v in files.items()}}
            archive = root / 'valid.zip'
            with zipfile.ZipFile(archive, 'w') as z:
                z.writestr('manifest.json', json.dumps(manifest))
                for name, value in files.items():
                    z.writestr(name, value)
            installed = import_update(archive, root / 'versions')
            self.assertEqual((installed / 'card/Z5APP.BIN').read_bytes(), card)
            self.assertEqual(verify_installed(installed), installed)
            with self.assertRaises(ValueError):
                import_update(archive, root / 'versions')
            (installed / 'card/Z5APP.BIN').write_bytes(b'changed')
            with self.assertRaises(ValueError):
                verify_installed(installed)


if __name__ == '__main__':
    unittest.main()
