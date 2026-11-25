# 卡模块构建与 fork 发布

普通用户使用根目录的预编译 `card/Z5APP.BIN`，不需要 SDK。自定义 DISP 在已有第 108 组固件上只替换插件并完全断电重启即可；上游连拍功能需要带其入口的新版固件。当前 patches.json 保留上游 b8ffd1c 的固件 edits/appends/输出摘要，本 fork 不另改固件补丁。以下步骤供维护者重建源码使用；构建器只读取 MAIN 文件，不生成或刷写固件。

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

如需从自己的更新文件提取 MAIN，可先按主 README 运行 `patch.py` 得到本地更新文件，再用其固定版本解码器的 `container.load()` / `container.main_module()` 读出 MAIN。上游带连拍入口的最终更新包摘要为 bf821031…，不等同于上述构建用基础 MAIN；请保留基础 MAIN 供源码构建。这里只接受具有上述摘要的输入，不分发解码后的模块、更新文件或内存转储。

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
python3 -B tests/card_module/verify_custom_menu.py
python3 -B tests/card_module/verify_disp_routing.py
python3 -B tests/card_module/verify_native_level.py
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

A39 设置检查额外覆盖精确 8／12／16／20 字节读取、总开关和两端逻辑页记忆、旧格式迁移、打开／定位／短读失败保留配置、500ms GUI 延迟重试、有效基底恢复后才允许编辑、不截断保存及长度／内容回读。路由检查编译实际 C，验证原厂按键门控、绘制前映射、两端循环及恢复；仍需相机冷启动确认介质持久性和原厂运行时行为。

宿主逻辑通过不代表真机帧率、触控、传感器或待机唤醒通过。当前验证范围见 [CUSTOM_DISP_HANDOFF.md](CUSTOM_DISP_HANDOFF.md)。

## A52：胶片颗粒

A52 在同一源码上用环境变量 `Z5_EXTRA` 追加定义（不设时等同 A39，构建结果与 A39 模块 `5ff920e0…` 逐字节相同）：

```sh
Z5_EXTRA='-DJPEG_LOG -DBURST_EXTRA_BITS=0x60u -DJPEG_A42 -DJPEG_A43=1 -DJPEG_A44 -DJPEG_A45 -DSETUP_SAVE_REOPEN -DLIGHTROOM_GRAIN -DA48_CRD -DJPEG_A50 -DBURST_BOOT_OFF -DA51_KERNEL2' python src/card_module/build.py --sdk third_party/z5mod-sdk/sdk --firmware /path/to/current-main.BIN   --legacy-firmware /path/to/legacy-main.BIN -o build/a52/Z5APP.BIN
```

结果 `b3ed50ebf369d332015b521fa231ef392920ef60e1c0853d3a0700f966f4132a`（GCC 13.3.1）。A52 配套固件 `a3704c6f…` 在 A39 配套固件上增加 MTP 列表加速、JPEG 编码入口闸门和 NEF `crd:` 闸门，钩子源码见 `src/firmware_hooks/z5_mtp_fw.py`、`z5_grain_fw.py`、`z5_crd_fw.py`；用户用 `patch.py` 从原厂文件生成。FILM 设置的主机测试：`tests/card_module/verify_film_config.py`。实现说明见 [FILM_GRAIN.md](FILM_GRAIN.md)。

## A81：照片画幅

A52 的 `Z5_EXTRA` 再加 `-DFONT_EXT -DASPECT43_FX`，GCC 13.3.1 结果 `274c864d156920d93ef8d85567ab1c55db888c6d6a0dc998dc158bb60e014a89`（从仓库源码重建一致）。配套固件 `13525d9ba3d461833e816ba838cf86369181445f0598378352069546118de6c5` 在 `a3704c6f…` 上增加照片画幅钩子。实现见 [PHOTO_ASPECT.md](PHOTO_ASPECT.md)。

## 更新发布文件

1. 把审核过的构建产物复制到根目录 `card/Z5APP.BIN`。
2. 只更新 `patch_data/patches.json` 中的 `card_module_sha256`；本轮不修改固件 edits、appends 或输出 SHA-256。
3. 根据本次 `report.json` 更新 [card-module-release.json](card-module-release.json)；更新版本、摘要和验证说明。
4. 运行上述测试，核对根目录插件与清单一致。
5. 重新构建 `Z5Extensions.exe`。发布页只提供这个程序，不再制作模块更新包。

## 提交边界

本地实验目录、构建结果、固件和转储都忽略；不要把这些文件强制加入 Git。正式源码、宿主测试、说明、插件和发布摘要才是维护入口。额外个人音效（例如 `6.WAV`）不在默认五个音效发布范围。用 `git diff --stat` 和 `git status --short` 核对本次变动后自行提交/推送 fork。

## A91：只拍 NEF 的回放（发布版）

A81 的 `Z5_EXTRA` 再加 `-DA83_TRACE -DA84_FW -DA43_MASK_COLOR=0xB3000000u`，即：

```sh
Z5_EXTRA='-DJPEG_LOG -DBURST_EXTRA_BITS=0x60u -DJPEG_A42 -DJPEG_A43=1 -DJPEG_A44 -DJPEG_A45 -DSETUP_SAVE_REOPEN -DLIGHTROOM_GRAIN -DA48_CRD -DJPEG_A50 -DBURST_BOOT_OFF -DA51_KERNEL2 -DFONT_EXT -DASPECT43_FX -DA83_TRACE -DA84_FW -DA43_MASK_COLOR=0xB3000000u' python src/card_module/build.py --sdk third_party/z5mod-sdk/sdk --firmware /path/to/current-main.BIN --legacy-firmware /path/to/legacy-main.BIN -o build/a91/Z5APP.BIN
```

GCC 13.3.1 结果 `1176e275fa2b3b928208ad51368e29f731e137bbfb40f8cdfeda2ac9da4e28cb`（作者实机测试的那一版）。`A84_FW`：照片画幅的判断逻辑在模块（`aspect_fw.inc`），固件只留跳转；
`A83_TRACE`：照片画幅的诊断记录写入“保存测试记录”；遮罩为 70% 黑。配套固件 `1f838b83f624397182813ee68c4e9fb0b697b40c5a48843f5bb6081a5dcb5cca`，构建与检查见
`src/firmware_hooks/z5_aspect43_a90m_fw.py`。实现见 [PHOTO_ASPECT.md](PHOTO_ASPECT.md)。
