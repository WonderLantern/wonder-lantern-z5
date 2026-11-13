# 卡模块构建与 fork 发布

普通用户使用根目录的预编译 `card/Z5APP.BIN`，不需要 SDK。已有第 108 组固件只替换插件并完全断电重启即可。以下步骤供维护者重建源码使用；构建器只读取 MAIN 文件，不生成或刷写固件。

## 依赖

- Python 3.10+、宿主 C 编译器 `cc`（宿主测试）。
- `arm-none-eabi-gcc` 和配套 binutils 在 PATH；当前发布使用 GCC 14.3.1。
- Python `capstone==5.0.7`，用于 SDK 的保留映像检查。
- z5mod SDK 基础提交 `8cf0fb425a4988abdb9442f33a2773c7a242dc23`，应用本仓库的 [z5mod-sdk.patch](../src/card_module/z5mod-sdk.patch)。这是 SDK 来源提交；与 `patch.py` 下载固件解码器的提交是两回事。
- 自备的第 108 组 **MAIN 模块**，不是外层官方更新包。摘要为 `6389847f5ae1e65fb8b06569bde48252486ec1818041b076b03263c562b604bf`，地址基准 `0x10020000`。

准备 SDK 的示例，所有命令在本仓库根目录运行：

```sh
python3 -m venv build/venv
. build/venv/bin/activate
python -m pip install capstone==5.0.7

git clone https://github.com/youyli03/z5mod third_party/z5mod-sdk
git -C third_party/z5mod-sdk checkout --detach 8cf0fb425a4988abdb9442f33a2773c7a242dc23
# -p1 后路径是 sdk/...，因此在 z5mod 根目录应用。
patch --batch -p1 -d third_party/z5mod-sdk < src/card_module/z5mod-sdk.patch
```

补丁只包含本模块实际需要的六个 SDK 文件修改，修复状态锚点、音频和 UI 等兼容点；构建前逐一校验 [sdk_sources.json](../src/card_module/custom_disp/sdk_sources.json) 中的文件摘要。不要跳过指纹检查或盲目改成新 SDK 的哈希；应先审核 ABI 和服务调用。

如需从自己的更新文件提取 MAIN，可先按主 README 运行 `patch.py` 得到本地更新文件，再用其固定版本解码器的 `container.load()` / `container.main_module()` 读出 MAIN。这里只接受具有上述摘要的输入，不分发解码后的模块、更新文件或内存转储。

## 两种构建

仅适配当前第 108 组固件，不需要旧实验固件：

```sh
python src/card_module/build.py \
  --sdk third_party/z5mod-sdk/sdk \
  --firmware /path/to/current-main.BIN \
  -o build/card-current/Z5APP.BIN
```

重建仓库预编译的双固件指纹版本，还需提供自己的旧版 MAIN：

```sh
python src/card_module/build.py \
  --sdk third_party/z5mod-sdk/sdk \
  --firmware /path/to/current-main.BIN \
  --legacy-firmware /path/to/legacy-main.BIN \
  -o build/card-release/Z5APP.BIN
```

旧 MAIN 的 SHA-256 为 `e7e4ec5477a301f6128d30352429f2e6d31eaaa22f13255ceebf531215e24d61`。这个可选参数保留早期 L3/L6 用户的 自定义 DISP 函数指纹兼容性；发布、安装和竖拍功能仍以当前第 108 组固件为基准，不代表旧固件支持全部竖拍钩子。没有旧文件时使用第一条命令即可，输出会因指纹条件不同而与预编译版本不同。

输出目录必须不存在。目录中有模块、`report.json`、生成目标头、布局检查和 ELF；均为本机构建产物。构建会检查原厂函数/常量、模块 CRC、三个常驻代码段的搬移和直接分支，并编译 ABI/SDK 容量断言。原有四个 dx/dy/dw/dh 未使用变量告警保留，其他警告按错误处理。

## 测试

以下测试编译实际 C 函数在宿主运行，不运行 ARM、QEMU、GUI 或 ISP：

```sh
python3 -B tests/card_module/verify_custom_disp.py
python3 -B tests/card_module/verify_config.py
python3 -B tests/card_module/verify_refresh_override.py
python3 -B tests/card_module/verify_histogram_raster.py
python3 -B tests/card_module/verify_output_sync.py
```

显示掩码测试还可核对自备 MAIN 的真实初始描述符，参数可重复：

```sh
python3 -B tests/card_module/verify_custom_disp.py \
  --firmware /path/to/current-main.BIN \
  --firmware /path/to/legacy-main.BIN
```

现有打包工具回归（Python 需带 Tkinter；测试不会打开窗口）：

```sh
python3 -B -m unittest discover -s tools/windows_packager -p 'test_*.py'
```

宿主逻辑通过不代表真机帧率、触控、传感器或待机唤醒通过。当前验证范围见 [CUSTOM_DISP_HANDOFF.md](CUSTOM_DISP_HANDOFF.md)。

## 更新发布文件

1. 把审核过的构建产物复制到根目录 `card/Z5APP.BIN`。
2. 只更新 `patch_data/patches.json` 中的 `card_module_sha256`；本轮不修改固件 edits、appends 或输出 SHA-256。
3. 根据本次 `report.json` 更新 [card-module-release.json](card-module-release.json)；更新版本、摘要和验证说明。
4. 运行上述测试，核对根目录插件与清单一致。数据包可用以下命令生成，`--output` 必须是新文件：

```sh
python3 tools/windows_packager/make_update.py \
  --repo . --version l21-custom-disp \
  --output build/l21-custom-disp.zip
```

该 ZIP 是数据包，包含插件、补丁数据和五个默认音效，不含生成后的尼康固件。既有上游 Windows EXE 不会自动包含本 fork 的插件；需导入本 fork 的数据包或重新构建 EXE。没有在本次任务中发布 GitHub release。

## 提交边界

本地实验目录、构建结果、固件和转储都忽略；不要把这些文件强制加入 Git。正式源码、宿主测试、说明、插件和发布摘要才是维护入口。额外个人音效（例如 `6.WAV`）不在默认五个音效发布范围。用 `git diff --stat` 和 `git status --short` 核对本次变动后自行提交/推送 fork。
