"""Build the Windows standalone program from an existing z5-extensions checkout.

    py -3.12 tools/windows_packager/build.py --repo . --output <new build dir> [--lang zh|en]

zh (default) gives dist/Z5Extensions.exe with Chinese interface and guide; en gives dist/Z5Extensions-EN.exe with
English interface, guide and shutter sound converter. Every module patch.py and the downloaded decoder import is
bundled explicitly (app.REQUIRED), and the finished exe is run with --selftest in an environment without any
Python (no Python on PATH, no PYTHON* variables, a fresh LOCALAPPDATA); the build fails if that self-check fails.
"""
import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import app  # noqa: E402  (REQUIRED only; the window is not opened)

GUIDE = {'zh': '使用说明.txt', 'en': 'Instructions.txt'}
NAME = {'zh': 'Z5Extensions', 'en': 'Z5Extensions-EN'}


def no_python_env(local_appdata):
    """A minimal Windows environment: system directories only, no Python anywhere."""
    system_root = os.environ.get('SystemRoot', r'C:\Windows')
    env = {k: v for k, v in os.environ.items()
           if k.upper() in ('SYSTEMROOT', 'WINDIR', 'TEMP', 'TMP', 'USERPROFILE', 'HOMEDRIVE', 'HOMEPATH',
                            'COMPUTERNAME', 'USERNAME', 'NUMBER_OF_PROCESSORS', 'PROCESSOR_ARCHITECTURE', 'OS',
                            'COMSPEC', 'PATHEXT', 'SYSTEMDRIVE', 'PROGRAMDATA', 'APPDATA')}
    env['PATH'] = os.pathsep.join([str(Path(system_root) / 'System32'), system_root])
    env['LOCALAPPDATA'] = str(local_appdata)
    return env


def selftest(exe, out):
    with tempfile.TemporaryDirectory(dir=out) as tmp:
        result = Path(tmp) / 'selftest.txt'
        subprocess.run([str(exe), '--selftest', '--result-file', str(result)], env=no_python_env(tmp),
                       cwd=tmp, check=False, timeout=300)
        text = result.read_text(encoding='utf-8') if result.exists() else '(no result file)'
    print(text)
    if 'selftest ok' not in text:
        raise SystemExit('self-check of the frozen program failed')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True, help='z5-extensions checkout root')
    parser.add_argument('--output', type=Path, required=True, help='new build directory')
    parser.add_argument('--lang', choices=('zh', 'en'), default='zh')
    args = parser.parse_args()
    repo = args.repo.resolve()
    out = args.output.resolve()
    if out.exists():
        raise SystemExit('Build directory already exists')
    out.mkdir(parents=True)
    lang_file = out / 'lang.txt'
    lang_file.write_text(args.lang, encoding='ascii')
    sound_source = repo / 'tools' / 'make_snd_wav.py'
    sound_build = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--onefile', '--windowed',
                   '--name', 'AudioConverter', '--distpath', str(out / 'sound-dist'),
                   '--workpath', str(out / 'sound-work'), '--specpath', str(out / 'sound-spec'),
                   '--add-data', str(sound_source) + ';.', '--add-data', str(lang_file) + ';.',
                   str(HERE / 'audio_app.py')]
    for module in ('argparse', 'hashlib', 'json', 'math', 'struct', 'time'):
        sound_build.extend(['--hidden-import', module])
    subprocess.run(sound_build, check=True)
    command = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--onefile', '--windowed',
               '--name', NAME[args.lang], '--distpath', str(out / 'dist'), '--workpath', str(out / 'work'),
               '--specpath', str(out / 'spec')]
    for module in app.REQUIRED:
        command.extend(['--hidden-import', module])
    for source, target in [('patch.py', 'assets'), ('patch_data', 'assets/patch_data'),
                           ('card', 'assets/card'), ('LICENSE', 'assets'), ('NOTICE.md', 'assets'),
                           ('README.md', 'assets'), ('README.en.md', 'assets'), ('licenses', 'assets/licenses')]:
        command.extend(['--add-data', str(repo / source) + ';' + target])
    command.extend(['--add-data', str(HERE / GUIDE[args.lang]) + ';.', '--add-data', str(lang_file) + ';.'])
    for path in (out / 'sound-dist' / 'AudioConverter.exe', sound_source, HERE / 'audio_app.py', HERE / 'GPL-2.0.txt'):
        command.extend(['--add-data', str(path) + ';.'])
    command.append(str(HERE / 'app.py'))
    subprocess.run(command, check=True)
    selftest(out / 'dist' / (NAME[args.lang] + '.exe'), out)


if __name__ == '__main__':
    main()
