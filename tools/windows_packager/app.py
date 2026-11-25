"""Offline-first Windows frontend for the upstream Z5 Extensions patcher.

The program is a standalone PyInstaller build: it runs patch.py and the downloaded z5mod decoder inside its own
bundled Python, so the computer needs no Python installation. Every module patch.py and the decoder import is
listed in REQUIRED and bundled explicitly; `--selftest` imports them all from the frozen program.

Command line (also used by the build's self-check):
    Z5Extensions.exe --selftest --result-file <file>
    Z5Extensions.exe --source Z_5_0150.bin --output-dir <folder> [--result-file <file>]
Without arguments the window opens. The interface language (zh / en) is fixed at build time (lang.txt).
"""
import ctypes
import hashlib
import importlib
import importlib.util
import os
import shutil
import sys
import tempfile
import threading
from datetime import datetime
from pathlib import Path

ROOT = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent))
ASSETS = ROOT / 'assets'
USER = Path(os.environ.get('LOCALAPPDATA', Path.home())) / 'Z5Extensions'
VERSION = 'A91'
# patch.py and the z5mod decoder (z5mod_fw.container, z5mod_fw.platform.z5_150) are loaded from source at run time,
# so PyInstaller cannot see their imports; build.py passes these as hidden imports and --selftest checks them.
REQUIRED = ('__future__', 'argparse', 'binascii', 'dataclasses', 'gzip', 'hashlib', 'importlib', 'importlib.util',
            'io', 'json', 'pathlib', 'shutil', 'ssl', 'struct', 'tarfile', 'tempfile', 'urllib.error',
            'urllib.request', 'zlib')


def language():
    try:
        value = (ROOT / 'lang.txt').read_text(encoding='ascii').strip()
    except OSError:
        value = 'zh'
    return 'en' if value == 'en' else 'zh'


LANG = language()
TEXT = {
    'zh': dict(
        title='Z5 Extensions 固件生成工具', firmware='原厂固件', choose_file='选择文件', fw_files='固件文件',
        all_files='所有文件', output='结果保存位置（自动创建新文件夹）', choose_dir='选择位置', choose_dir_title='选择输出位置',
        version=f'版本：{VERSION}（固件和卡上模块都在程序内，不需要安装 Python）', first='首次生成需要联网获取并校验解码器。',
        busy='正在校验并生成，请稍候……', done='生成完成', failed='生成失败，请检查提示。', result_title='生成结果',
        success='生成成功：\n', button='生成固件和卡上文件',
        warning='实验性项目；仅适用于 Z 5 C 1.50。刷入修改固件可能损坏相机。',
        no_parent='请选择已存在的输出位置。', no_name='无法创建不重名的结果文件夹。', no_source='请选择原厂 Z_5_0150.bin 文件。',
        dest_exists='输出文件夹已经存在，请选择新的文件夹。', not_fixed='请选择电脑本地固定磁盘，不能选择移动存储设备。',
        decoder_bad='本地解码器校验失败：', guide='使用说明.txt', guide_out='使用说明.txt',
        copies=(('AudioConverter.exe', '快门声音转换工具.exe'), ('make_snd_wav.py', '音频转换工具源码.py'),
                ('audio_app.py', '音频转换界面源码.py'), ('GPL-2.0.txt', '音频转换工具-GPL-2.0.txt'))),
    'en': dict(
        title='Z5 Extensions firmware generator', firmware='Official firmware file', choose_file='Browse...',
        fw_files='Firmware files', all_files='All files', output='Output location (a new folder is created)',
        choose_dir='Browse...', choose_dir_title='Choose the output location',
        version=f'Version: {VERSION} (firmware and card module are inside this program; no Python needed)',
        first='The first run downloads and checks the decoder (internet needed).',
        busy='Checking and generating, please wait...', done='Done', failed='Failed, see the message.',
        result_title='Result', success='Generated:\n', button='Generate firmware and card files',
        warning='Experimental; Z 5 firmware C 1.50 only. Flashing modified firmware can damage the camera.',
        no_parent='Choose an existing output location.', no_name='Could not create a new result folder.',
        no_source='Choose the official Z_5_0150.bin file.', dest_exists='The output folder already exists; choose a new one.',
        not_fixed='Choose a fixed local disk, not a removable drive.', decoder_bad='Local decoder check failed: ',
        guide='Instructions.txt', guide_out='Instructions.txt',
        copies=(('AudioConverter.exe', 'ShutterSoundConverter.exe'), ('make_snd_wav.py', 'sound_converter_source.py'),
                ('audio_app.py', 'sound_converter_ui_source.py'), ('GPL-2.0.txt', 'ShutterSoundConverter-GPL-2.0.txt'))),
}
T = TEXT[LANG]


def fixed_disk(path):
    root = Path(path).resolve().anchor
    return os.name != 'nt' or ctypes.windll.kernel32.GetDriveTypeW(root) == 3


def new_output(parent):
    parent = Path(parent)
    if not parent.is_dir():
        raise ValueError(T['no_parent'])
    stem = 'Z5Extensions-' + datetime.now().strftime('%Y%m%d-%H%M%S')
    for suffix in range(1000):
        candidate = parent / (stem if not suffix else f'{stem}-{suffix}')
        if not candidate.exists():
            return candidate
    raise ValueError(T['no_name'])


def generate(source, dest):
    source, dest = Path(source), Path(dest)
    if not source.is_file():
        raise ValueError(T['no_source'])
    if dest.exists():
        raise ValueError(T['dest_exists'])
    if not fixed_disk(dest):
        raise ValueError(T['not_fixed'])
    spec = importlib.util.spec_from_file_location('upstream_patch', ASSETS / 'patch.py')
    patch = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(patch)
    patch.DATA = ASSETS / 'patch_data' / 'patches.json'
    patch.CARD_MODULE = ASSETS / 'card' / 'Z5APP.BIN'
    patch.HERE = ASSETS
    patch.THIRD_PARTY = USER / 'decoder'
    original_load = patch.load_decoder
    def checked_load(args, pin):
        for name, digest in pin['files'].items():
            path = patch.THIRD_PARTY / name
            if path.exists() and hashlib.sha256(path.read_bytes()).hexdigest() != digest:
                raise ValueError(T['decoder_bad'] + name)
        return original_load(args, pin)
    patch.load_decoder = checked_load
    # Keep upstream's pinned decoder and original-byte/hash validation.
    dest.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix='.z5-build-', dir=dest.parent))
    old = sys.argv
    try:
        sys.argv = ['patch.py', '--source', str(source), '--output', str(staging)]
        patch.main()
        guide = (ROOT / T['guide']).read_text(encoding='utf-8')
        (staging / T['guide_out']).write_text(guide, encoding='utf-8-sig', newline='\r\n')
        for src, dst in T['copies']:
            shutil.copyfile(ROOT / src, staging / dst)
        staging.rename(dest)
    finally:
        sys.argv = old
        if staging.exists():
            shutil.rmtree(staging)


def selftest():
    """Import everything patch.py and the decoder need, from this (possibly frozen) program."""
    missing = []
    for name in REQUIRED:
        try:
            importlib.import_module(name)
        except Exception as e:                      # noqa: BLE001 - report every failure
            missing.append(f'{name}: {e}')
    for path in (ASSETS / 'patch.py', ASSETS / 'patch_data' / 'patches.json', ASSETS / 'card' / 'Z5APP.BIN',
                 ROOT / T['guide']) + tuple(ROOT / src for src, _ in T['copies']):
        if not path.exists():
            missing.append(f'missing file: {path.name}')
    return missing


def command_line(argv):
    import argparse
    parser = argparse.ArgumentParser(prog='Z5Extensions')
    parser.add_argument('--selftest', action='store_true')
    parser.add_argument('--source')
    parser.add_argument('--output-dir')
    parser.add_argument('--result-file')
    args = parser.parse_args(argv)
    lines, code = [f'Z5Extensions {VERSION} {LANG} frozen={bool(getattr(sys, "frozen", False))}'], 0
    try:
        if args.selftest:
            missing = selftest()
            lines += missing or ['selftest ok']
            code = 1 if missing else 0
        else:
            result = new_output(args.output_dir)
            generate(args.source, result)
            lines.append(f'ok {result}')
    except (Exception, SystemExit) as e:            # noqa: BLE001
        lines.append(f'error {e}')
        code = 1
    text = '\n'.join(lines) + '\n'
    if args.result_file:
        Path(args.result_file).write_text(text, encoding='utf-8')
    elif sys.stdout:
        sys.stdout.write(text)
    return code


def main():
    if len(sys.argv) > 1:
        raise SystemExit(command_line(sys.argv[1:]))
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    root = tk.Tk()
    root.title(T['title'])
    root.geometry('680x350')
    root.resizable(False, False)
    frame = ttk.Frame(root, padding=20)
    frame.pack(fill='both', expand=True)
    source = tk.StringVar()
    destination = tk.StringVar(value=str(Path.home() / 'Documents'))
    font = 'Microsoft YaHei' if LANG == 'zh' else 'Segoe UI'
    ttk.Label(frame, text='Nikon Z 5 · C 1.50', font=(font, 15, 'bold')).grid(row=0, column=0, columnspan=3, sticky='w', pady=(0, 12))
    ttk.Label(frame, text=T['firmware']).grid(row=1, column=0, sticky='w')
    ttk.Entry(frame, textvariable=source, width=56).grid(row=2, column=0, columnspan=2, sticky='ew', pady=4)
    ttk.Button(frame, text=T['choose_file'], command=lambda: source.set(filedialog.askopenfilename(filetypes=[(T['fw_files'], '*.bin'), (T['all_files'], '*.*')]) or source.get())).grid(row=2, column=2, padx=8)
    ttk.Label(frame, text=T['output']).grid(row=3, column=0, sticky='w', pady=(8, 0))
    ttk.Entry(frame, textvariable=destination, width=56).grid(row=4, column=0, columnspan=2, sticky='ew', pady=4)
    def choose_output():
        parent = filedialog.askdirectory(title=T['choose_dir_title'])
        if parent:
            destination.set(parent)
    ttk.Button(frame, text=T['choose_dir'], command=choose_output).grid(row=4, column=2, padx=8)
    ttk.Label(frame, text=T['version']).grid(row=5, column=0, columnspan=3, sticky='w', pady=8)
    status = tk.StringVar(value=T['first'])
    ttk.Label(frame, textvariable=status).grid(row=6, column=0, columnspan=3, sticky='w', pady=8)
    def build():
        selected_source, selected_dest = source.get(), destination.get()
        button.state(['disabled'])
        status.set(T['busy'])
        def run():
            try:
                result = new_output(selected_dest)
                generate(selected_source, result)
                msg, ok = T['success'] + str(result), True
            except (Exception, SystemExit) as e:
                msg, ok = str(e), False
            def done():
                button.state(['!disabled'])
                status.set(T['done'] if ok else T['failed'])
                (messagebox.showinfo if ok else messagebox.showerror)(T['result_title'], msg)
            root.after(0, done)
        threading.Thread(target=run, daemon=True).start()
    button = ttk.Button(frame, text=T['button'], command=build)
    button.grid(row=7, column=0, sticky='w', pady=10)
    ttk.Label(frame, text=T['warning']).grid(row=8, column=0, columnspan=3, sticky='w')
    root.mainloop()


if __name__ == '__main__':
    main()
