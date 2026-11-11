"""Build the Windows standalone program from an existing z5-extensions checkout."""
import argparse
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--repo', type=Path, required=True, help='z5-extensions checkout root')
    parser.add_argument('--output', type=Path, required=True, help='new build directory')
    args = parser.parse_args()
    repo = args.repo.resolve()
    out = args.output.resolve()
    if out.exists():
        raise SystemExit('Build directory already exists')
    out.mkdir(parents=True)
    sound_source = repo / 'tools' / 'make_snd_wav.py'
    sound_build = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--onefile', '--windowed',
                   '--name', 'AudioConverter', '--distpath', str(out / 'sound-dist'),
                   '--workpath', str(out / 'sound-work'), '--specpath', str(out / 'sound-spec'),
                   '--add-data', str(sound_source) + ';.', str(HERE / 'audio_app.py')]
    for module in ('argparse', 'hashlib', 'json', 'math', 'struct', 'time'):
        sound_build.extend(['--hidden-import', module])
    subprocess.run(sound_build, check=True)
    command = [sys.executable, '-m', 'PyInstaller', '--noconfirm', '--clean', '--onefile', '--windowed',
               '--name', 'Z5Extensions', '--distpath', str(out / 'dist'), '--workpath', str(out / 'work'),
               '--specpath', str(out / 'spec')]
    for module in ('__future__', 'dataclasses', 'tarfile', 'urllib.request', 'binascii'):
        command.extend(['--hidden-import', module])
    for source, target in [('patch.py', 'assets'), ('patch_data', 'assets/patch_data'),
                           ('card', 'assets/card'), ('LICENSE', 'assets'), ('NOTICE.md', 'assets'),
                           ('README.md', 'assets')]:
        command.extend(['--add-data', str(repo / source) + ';' + target])
    command.extend(['--add-data', str(HERE / '使用说明.txt') + ';.'])
    for path in (out / 'sound-dist' / 'AudioConverter.exe', sound_source, HERE / 'audio_app.py', HERE / 'GPL-2.0.txt'):
        command.extend(['--add-data', str(path) + ';.'])
    command.append(str(HERE / 'app.py'))
    subprocess.run(command, check=True)


if __name__ == '__main__':
    main()
