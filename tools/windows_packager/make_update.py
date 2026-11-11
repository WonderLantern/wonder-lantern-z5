"""Create a data-only version archive from a z5-extensions checkout."""
import argparse
import hashlib
import json
import re
import zipfile
from pathlib import Path

from release_data import REQUIRED, STOCK_SHA256

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True)
    parser.add_argument('--version', required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,63}', args.version):
        raise SystemExit('Invalid version')
    data = {name: (args.repo / name).read_bytes() for name in REQUIRED}
    settings = json.loads(data['patch_data/patches.json'])
    if settings.get('stock_package_sha256') != STOCK_SHA256:
        raise SystemExit('Unexpected stock firmware hash')
    manifest = {'format': 1, 'version': args.version,
                'files': {name: hashlib.sha256(value).hexdigest() for name, value in data.items()}}
    with zipfile.ZipFile(args.output, 'x', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('manifest.json', json.dumps(manifest, indent=2))
        for name, value in data.items():
            archive.writestr(name, value)

if __name__ == '__main__':
    main()
