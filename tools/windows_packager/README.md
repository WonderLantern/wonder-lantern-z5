# Windows 固件生成工具

Windows 用户直接运行发布页提供的 `Z5Extensions.exe`，无需安装 Python。选择从尼康官网下载的 Z 5 C 1.50 原厂文件和电脑上的保存位置，点击“生成固件和卡上文件”；程序自动创建不重名的结果文件夹。首次生成需要联网下载并校验固定版本的 z5mod 解码器。输入文件不会改变，程序不会连接相机或写入 SD 卡。

结果文件夹同时包含中文 `使用说明.txt` 和 `快门声音转换工具.exe`。转换器接受 WAV 输入，转换为相机需要的格式，并按选定编号命名为 `1.WAV` 至 `5.WAV`；不会覆盖现有文件或写入存储卡。结果仍需实机试听（`camera_verified: false`）。转换器使用 `tools/make_snd_wav.py`，遵循 GPL-2.0-or-later；结果文件夹随附两个源码文件和 GPL 许可证。

生成的 `EG165050.BIN` 和 `card` 子目录用途、固件更新步骤、恢复步骤以及适用范围请阅读仓库根目录的 README。不要分发原厂或生成的固件。刷入修改固件可能损坏相机。

## 后续版本包

维护者在经过验证的仓库版本中运行：

```powershell
py -3 tools/windows_packager/make_update.py --repo . --version v2 --output Z5-update-v2.zip
```

图形程序的“导入版本包”会检查版本、文件清单、文件哈希、机型、原厂固件哈希与卡上资源；导入成功后会保存当前版本并保留旧版本。版本包只能更新数据和卡上文件，不能替换程序本身；新版本如需更改解码器或程序逻辑，须重新发布 Windows 程序。哈希校验不能证明包的来源，**仅导入可信来源的版本包**。生成新的固件前仍执行补丁器的原字节、CRC 和输出哈希校验。

## 从源码构建

在 Windows、Python 3.12 和 PyInstaller 环境下，在仓库根目录执行：

```powershell
py -3.12 -m pip install pyinstaller==6.22.3
py -3.12 tools/windows_packager/build.py --repo . --output <新的构建目录>
```

结果是构建目录中 `dist/Z5Extensions.exe`。不包含原厂固件、生成的固件或解码密钥。程序中附有项目许可证、NOTICE 和原项目 README。
