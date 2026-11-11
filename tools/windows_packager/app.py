"""Offline-first Windows frontend for the upstream Z5 Extensions patcher."""
import ctypes
import hashlib
import importlib.util
import json
import os
import shutil
import sys
import tempfile
import threading
import tkinter as tk
from datetime import datetime
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

from release_data import import_update, verify_installed

ROOT = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent))
ASSETS = ROOT / 'assets'
USER = Path(os.environ.get('LOCALAPPDATA', Path.home())) / 'Z5Extensions'


def fixed_disk(path):
    root = Path(path).resolve().anchor
    return os.name != 'nt' or ctypes.windll.kernel32.GetDriveTypeW(root) == 3


def new_output(parent):
    parent = Path(parent)
    if not parent.is_dir():
        raise ValueError('请选择已存在的输出位置。')
    stem = 'Z5Extensions-' + datetime.now().strftime('%Y%m%d-%H%M%S')
    for suffix in range(1000):
        candidate = parent / (stem if not suffix else f'{stem}-{suffix}')
        if not candidate.exists():
            return candidate
    raise ValueError('无法创建不重名的结果文件夹。')


def generate(source, dest, release=ASSETS):
    source, dest, release = Path(source), Path(dest), Path(release)
    if not source.is_file():
        raise ValueError('请选择原厂 Z_5_0150.bin 文件。')
    if dest.exists():
        raise ValueError('输出文件夹已经存在，请选择新的文件夹。')
    if not fixed_disk(dest):
        raise ValueError('请选择电脑本地固定磁盘，不能选择移动存储设备。')
    baseline = json.loads((ASSETS / 'patch_data' / 'patches.json').read_text(encoding='utf-8'))
    candidate = json.loads((release / 'patch_data' / 'patches.json').read_text(encoding='utf-8'))
    if candidate.get('z5mod') != baseline.get('z5mod') or candidate.get('stock_main_sha256') != baseline.get('stock_main_sha256') or candidate.get('stock_package_sha256') != baseline.get('stock_package_sha256'):
        raise ValueError('版本包的原厂固件或解码器配置不匹配。')
    if release != ASSETS:
        verify_installed(release)
    spec = importlib.util.spec_from_file_location('upstream_patch', ASSETS / 'patch.py')
    patch = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patch)
    patch.DATA = release / 'patch_data' / 'patches.json'
    patch.CARD_MODULE = release / 'card' / 'Z5APP.BIN'
    patch.HERE = release
    patch.THIRD_PARTY = USER / 'decoder'
    original_load = patch.load_decoder
    def checked_load(args, pin):
        for name, digest in pin['files'].items():
            path = patch.THIRD_PARTY / name
            if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise ValueError('本地解码器校验失败：' + name)
        return original_load(args, pin)
    patch.load_decoder = checked_load
    # Keep upstream's pinned decoder and original-byte/hash validation.
    dest.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix='.z5-build-', dir=dest.parent))
    old = sys.argv
    try:
        sys.argv = ['patch.py', '--source', str(source), '--output', str(staging)]
        patch.main()
        guide = (ROOT / '使用说明.txt').read_text(encoding='utf-8')
        (staging / '使用说明.txt').write_text(guide, encoding='utf-8-sig', newline='\r\n')
        for src, dst in (('AudioConverter.exe', '快门声音转换工具.exe'),
                         ('make_snd_wav.py', '音频转换工具源码.py'),
                         ('audio_app.py', '音频转换界面源码.py'),
                         ('GPL-2.0.txt', '音频转换工具-GPL-2.0.txt')):
            shutil.copyfile(ROOT / src, staging / dst)
        staging.rename(dest)
    finally:
        sys.argv = old
        if staging.exists():
            shutil.rmtree(staging)


def main():
    root = tk.Tk()
    root.title('Z5 Extensions 固件生成工具')
    root.geometry('650x350')
    root.resizable(False, False)
    frame = ttk.Frame(root, padding=20)
    frame.pack(fill='both', expand=True)
    source = tk.StringVar()
    destination = tk.StringVar(value=str(Path.home() / 'Documents'))
    version = tk.StringVar(value='内置公开版本')
    release = [ASSETS]
    active_file = USER / 'active-version.txt'
    if active_file.is_file():
        try:
            active = active_file.read_text(encoding='utf-8').strip()
            if Path(active).name != active:
                raise ValueError('无效版本名')
            release[0] = verify_installed(USER / 'versions' / active)
            version.set('已选择版本：' + active)
        except (OSError, ValueError, json.JSONDecodeError):
            version.set('已保存版本无效，使用内置公开版本')
    ttk.Label(frame, text='Nikon Z 5 · C 1.50', font=('Microsoft YaHei', 15, 'bold')).grid(row=0, column=0, columnspan=3, sticky='w', pady=(0, 12))
    ttk.Label(frame, text='原厂固件').grid(row=1, column=0, sticky='w')
    ttk.Entry(frame, textvariable=source, width=56).grid(row=2, column=0, columnspan=2, sticky='ew', pady=4)
    ttk.Button(frame, text='选择文件', command=lambda: source.set(filedialog.askopenfilename(filetypes=[('固件文件', '*.bin'), ('所有文件', '*.*')]) or source.get())).grid(row=2, column=2, padx=8)
    ttk.Label(frame, text='结果保存位置（自动创建新文件夹）').grid(row=3, column=0, sticky='w', pady=(8, 0))
    ttk.Entry(frame, textvariable=destination, width=56).grid(row=4, column=0, columnspan=2, sticky='ew', pady=4)
    def choose_output():
        parent = filedialog.askdirectory(title='选择输出位置')
        if parent:
            destination.set(parent)
    ttk.Button(frame, text='选择位置', command=choose_output).grid(row=4, column=2, padx=8)
    ttk.Label(frame, textvariable=version).grid(row=5, column=0, sticky='w', pady=8)
    def update():
        path = filedialog.askopenfilename(title='选择可信来源的版本包', filetypes=[('版本包', '*.zip')])
        if not path:
            return
        if not messagebox.askyesno('导入更新', '只导入可信来源的版本包。哈希只能检查完整性，不能证明发布者身份。继续吗？'):
            return
        try:
            release[0] = import_update(path, USER / 'versions')
            active_file.write_text(release[0].name, encoding='utf-8')
            version.set('已选择版本：' + release[0].name)
        except Exception as e:
            messagebox.showerror('更新失败', str(e))
    ttk.Button(frame, text='导入版本包', command=update).grid(row=5, column=2, padx=8)
    status = tk.StringVar(value='首次生成需要联网获取并校验解码器。')
    ttk.Label(frame, textvariable=status).grid(row=6, column=0, columnspan=3, sticky='w', pady=8)
    def build():
        selected_source, selected_dest, selected_release = source.get(), destination.get(), release[0]
        button.state(['disabled'])
        status.set('正在校验并生成，请稍候……')
        def run():
            try:
                result = new_output(selected_dest)
                generate(selected_source, result, selected_release)
                msg, ok = '生成成功：\n' + str(result), True
            except (Exception, SystemExit) as e:
                msg, ok = str(e), False
            def done():
                button.state(['!disabled'])
                status.set('生成完成' if ok else '生成失败，请检查提示。')
                (messagebox.showinfo if ok else messagebox.showerror)('生成结果', msg)
            root.after(0, done)
        threading.Thread(target=run, daemon=True).start()
    button = ttk.Button(frame, text='生成固件和卡上文件', command=build)
    button.grid(row=7, column=0, sticky='w', pady=10)
    ttk.Label(frame, text='实验性项目；仅适用于 Z 5 C 1.50。刷入修改固件可能损坏相机。').grid(row=8, column=0, columnspan=3, sticky='w')
    root.mainloop()


if __name__ == '__main__':
    main()
