# SPDX-License-Identifier: GPL-2.0-or-later
"""Simple WAV conversion interface; conversion algorithm is supplied by z5-extensions.

The interface language (zh / en) is fixed at build time (lang.txt next to the program's files)."""
import ctypes
import importlib.util
import os
import sys
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

ROOT = Path(getattr(sys, '_MEIPASS', Path(__file__).resolve().parent))
APP_DIR = Path(sys.executable).resolve().parent if getattr(sys, 'frozen', False) else Path.cwd()
try:
    LANG = 'en' if (ROOT / 'lang.txt').read_text(encoding='ascii').strip() == 'en' else 'zh'
except OSError:
    LANG = 'zh'
TEXT = {
    'zh': dict(need_wav='请选择 WAV 声音文件。', same='输出不能与原文件相同。', name='文件名必须是 1.WAV 到 5.WAV。',
               fixed='请保存到电脑本地固定磁盘，再由您自行复制到存储卡。', title='Z5 快门声音转换',
               intro='选择 WAV 声音文件，转换为相机可用的格式。', pick='选择 WAV', wav='WAV 音频', number='声音编号',
               save_title='选择电脑上的保存位置', fail='转换失败', cut='\n音频过长，已截短并淡出。',
               clip='\n音频音量过高，可能削波。', ok_title='转换完成',
               ok='已保存到：\n{0}\n请自行复制到卡上的 SHUTTER 文件夹。{1}', button='转换并保存',
               note='仅支持 WAV 输入；不会覆盖已有文件，也不会写入存储卡。'),
    'en': dict(need_wav='Choose a WAV sound file.', same='The output cannot be the source file.',
               name='The file name must be 1.WAV to 5.WAV.',
               fixed='Save to a fixed local disk on the computer, then copy it to the card yourself.',
               title='Z5 shutter sound converter',
               intro='Choose a WAV sound file to convert it to the format the camera uses.', pick='Choose WAV',
               wav='WAV audio', number='Sound number', save_title='Choose where to save on the computer',
               fail='Conversion failed', cut='\nThe sound was too long; it was shortened and faded out.',
               clip='\nThe sound is very loud and may clip.', ok_title='Converted',
               ok='Saved to:\n{0}\nCopy it to the SHUTTER folder on the card yourself.{1}', button='Convert and save',
               note='WAV input only; existing files are never overwritten and nothing is written to the card.'),
}
T = TEXT[LANG]


def convert_file(source, destination, converter):
    source, destination = Path(source), Path(destination)
    if not source.is_file() or source.suffix.lower() != '.wav':
        raise ValueError(T['need_wav'])
    if source.resolve() == destination.resolve():
        raise ValueError(T['same'])
    if destination.name.upper() not in {f'{n}.WAV' for n in range(1, 6)}:
        raise ValueError(T['name'])
    if os.name == 'nt' and ctypes.windll.kernel32.GetDriveTypeW(destination.resolve().anchor) != 3:
        raise ValueError(T['fixed'])
    converted, info = converter.convert(source.read_bytes())
    problem = converter.module_check(converted)
    if problem:
        raise ValueError(problem)
    with destination.open('xb') as out:
        out.write(converted)
    return info


def main():
    spec = importlib.util.spec_from_file_location('wav_converter', ROOT / 'make_snd_wav.py')
    converter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(converter)
    root = tk.Tk()
    root.title(T['title'])
    root.geometry('520x250')
    frame = ttk.Frame(root, padding=20)
    frame.pack(fill='both', expand=True)
    source, number = tk.StringVar(), tk.StringVar(value='1')
    ttk.Label(frame, text=T['intro']).pack(anchor='w', pady=5)
    row = ttk.Frame(frame)
    row.pack(fill='x', pady=5)
    ttk.Button(row, text=T['pick'], command=lambda: source.set(filedialog.askopenfilename(filetypes=[(T['wav'], '*.wav')]) or source.get())).pack(side='left', padx=6)
    ttk.Entry(row, textvariable=source).pack(side='left', fill='x', expand=True)
    row2 = ttk.Frame(frame)
    row2.pack(fill='x', pady=5)
    ttk.Label(row2, text=T['number']).pack(side='left')
    ttk.Combobox(row2, textvariable=number, values=['1', '2', '3', '4', '5'], state='readonly', width=4).pack(side='left', padx=8)
    def run():
        output_dir = filedialog.askdirectory(title=T['save_title'], initialdir=str(APP_DIR))
        if not output_dir:
            return
        destination = Path(output_dir) / (number.get() + '.WAV')
        try:
            info = convert_file(source.get(), destination, converter)
        except Exception as e:
            messagebox.showerror(T['fail'], str(e))
            return
        warning = T['cut'] if info['truncated'] else ''
        if info['clipped']:
            warning += T['clip']
        messagebox.showinfo(T['ok_title'], T['ok'].format(destination, warning))
    ttk.Button(frame, text=T['button'], command=run).pack(anchor='w', pady=10)
    ttk.Label(frame, text=T['note']).pack(anchor='w')
    root.mainloop()

if __name__ == '__main__':
    main()
