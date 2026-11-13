"""Validate and install data-only release bundles; hashes do not authenticate authors."""
import hashlib
import json
import re
import shutil
import tempfile
import zipfile
from pathlib import Path

REQUIRED = {'patch_data/patches.json', 'card/Z5APP.BIN',
            'card/SHUTTER/1.WAV', 'card/SHUTTER/2.WAV', 'card/SHUTTER/3.WAV',
            'card/SHUTTER/4.WAV', 'card/SHUTTER/5.WAV'}
SOUNDS = {'1.WAV', '2.WAV', '3.WAV', '4.WAV', '5.WAV'}
STOCK_SHA256 = 'efc0ce70dcafdd96c64e95d8d50f6b7f0509fc0c2840b28d12b6f9e4877ee776'


def import_update(archive, versions):
    versions = Path(versions).resolve()
    with zipfile.ZipFile(archive) as z:
        infos = z.infolist()
        names = [i.filename for i in infos]
        if len(names) != len(set(names)) or sum(i.file_size for i in infos) > 20_000_000:
            raise ValueError('更新包文件重复或体积过大')
        if 'manifest.json' not in names:
            raise ValueError('缺少清单')
        manifest = json.loads(z.read('manifest.json'))
        version = manifest.get('version', '')
        files = manifest.get('files', {})
        if manifest.get('format') != 1 or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{0,63}', version):
            raise ValueError('版本信息无效')
        if set(files) != REQUIRED or set(names) != REQUIRED | {'manifest.json'}:
            raise ValueError('更新包含未知或缺少必需文件')
        content = {}
        for name, digest in files.items():
            data = z.read(name)
            if hashlib.sha256(data).hexdigest() != digest:
                raise ValueError('文件校验失败: ' + name)
            content[name] = data
        data = json.loads(content['patch_data/patches.json'])
        if data.get('camera') != 'Nikon Z 5' or data.get('firmware') != 'C 1.50':
            raise ValueError('更新包机型或固件版本不兼容')
        if data.get('stock_package_sha256') != STOCK_SHA256:
            raise ValueError('更新包原厂固件校验值不匹配')
        if hashlib.sha256(content['card/Z5APP.BIN']).hexdigest() != data.get('card_module_sha256'):
            raise ValueError('模块与补丁清单不一致')
        for name, digest in data.get('card_sounds', {}).items():
            if name not in SOUNDS or hashlib.sha256(content['card/SHUTTER/' + name]).hexdigest() != digest:
                raise ValueError('音效与补丁清单不一致')
        if set(data.get('card_sounds', {})) != SOUNDS:
            raise ValueError('音效清单不完整')
    versions.mkdir(parents=True, exist_ok=True)
    target = versions / version
    if target.exists():
        raise ValueError('版本已存在')
    temp = Path(tempfile.mkdtemp(prefix='.install-', dir=versions))
    try:
        for name, data in content.items():
            path = temp / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (temp / 'manifest.json').write_text(json.dumps(manifest), encoding='utf-8')
        temp.rename(target)
    finally:
        if temp.exists():
            shutil.rmtree(temp)
    return target


def verify_installed(folder):
    folder = Path(folder)
    manifest = json.loads((folder / 'manifest.json').read_text(encoding='utf-8'))
    if manifest.get('format') != 1 or manifest.get('version') != folder.name or set(manifest.get('files', {})) != REQUIRED:
        raise ValueError('已安装版本清单无效')
    for name, digest in manifest['files'].items():
        if hashlib.sha256((folder / name).read_bytes()).hexdigest() != digest:
            raise ValueError('已安装版本文件校验失败: ' + name)
    data = json.loads((folder / 'patch_data/patches.json').read_text(encoding='utf-8'))
    if data.get('stock_package_sha256') != STOCK_SHA256 or data.get('camera') != 'Nikon Z 5' or data.get('firmware') != 'C 1.50':
        raise ValueError('已安装版本不兼容')
    return folder
