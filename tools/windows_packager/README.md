# Windows 固件生成工具

当前安装器生成 B18 固件（SHA-256 以 b1f8cdc9 开头）和 B18 卡上模块（以 eb0d323d 开头）。

发布页提供两个程序，功能相同：
- `Z5Extensions.exe`：中文界面，结果里附中文《使用说明.txt》和中文快门声音转换工具；
- `Z5Extensions-EN.exe`：英文界面，结果里附英文 `Instructions.txt` 和英文 `ShutterSoundConverter.exe`。

程序是独立的 Windows 程序，**电脑上不需要安装 Python**：`patch.py` 和首次运行时下载的 z5mod 解码器都在程序自带的 Python 里运行，它们用到的模块全部打包在程序内（`app.py` 的 `REQUIRED`）。解码器通过 HTTPS 下载时，证书同时按 Windows 证书库和程序自带的 certifi 根证书验证（不会关闭验证），缺根证书的电脑也能连上 GitHub。仍然失败时（GitHub 加速器、代理或 VPN、杀毒软件替换了证书），程序会提示原因、把下载链接复制到剪贴板，用户用浏览器下载解码器压缩包（.zip 或 .tar.gz）后在程序里选择它；每个文件都按 `patch_data/patches.json` 里固定的 SHA-256 核对，导入后生成不再需要联网。命令行用 `--decoder-archive`。每次构建后，构建脚本会在“没有 Python”的环境里运行一次程序自检（PATH 只留 Windows 系统目录、去掉所有 PYTHON 变量、使用全新的 LOCALAPPDATA），自检不通过就算构建失败。

使用：选择从尼康官网下载的 Z 5 C 1.50 原厂文件和电脑上的保存位置，点击“生成固件和卡上文件”（英文版为 “Generate firmware and card files”）；程序自动创建不重名的结果文件夹。首次生成需要联网下载并校验固定版本的 z5mod 解码器。输入文件不会改变，程序不会连接相机或写入 SD 卡。

命令行（与窗口功能相同，也用于自检）：

```powershell
Z5Extensions.exe --source Z_5_0150.bin --output-dir D:\out --result-file D:\out\result.txt
Z5Extensions.exe --selftest --result-file selftest.txt
Z5Extensions.exe --decoder-archive z5mod.zip --source Z_5_0150.bin --output-dir D:\out
```

快门声音转换器接受 WAV 输入，转换为相机需要的格式，并按选定编号命名为 `1.WAV` 至 `5.WAV`；不会覆盖现有文件或写入存储卡。结果仍需实机试听（`camera_verified: false`）。转换器使用 `tools/make_snd_wav.py`，遵循 GPL-2.0-or-later；结果文件夹随附两个源码文件和 GPL 许可证。

生成的 `EG165050.BIN` 和 `card` 子目录用途、固件更新步骤、恢复步骤以及适用范围请阅读仓库根目录的 README。不要分发原厂或生成的固件。刷入修改固件可能损坏相机。

## 更新

发布页只提供这两个 exe，不再提供模块更新包，程序也不再导入版本包。新版本下载新的 exe。

## 从源码构建

在 Windows、Python 3.12 和 PyInstaller 环境下，在仓库根目录执行：

```powershell
py -3.12 -m pip install pyinstaller==6.22.3 certifi==2025.11.12
py -3.12 tools/windows_packager/build.py --repo . --output <新的构建目录>
py -3.12 tools/windows_packager/build.py --repo . --output <另一个新的构建目录> --lang en
```

结果分别是 `dist/Z5Extensions.exe` 和 `dist/Z5Extensions-EN.exe`。构建最后会打印自检结果。不包含原厂固件、生成的固件或解码密钥。程序中附有项目许可证、NOTICE 和原项目 README。
