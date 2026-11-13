# 仓库协作说明

## 项目与入口

本项目为 Nikon Z 5 C 1.50 扩展，包含卡上 ARM 模块、固件钩子源码和本地打包工具。先读 [README.md](README.md)，再按任务查看：

- [docs/CUSTOM_DISP_HANDOFF.md](docs/CUSTOM_DISP_HANDOFF.md)：水平仪、直方图、自定义 DISP 的绘制流程与已知边界。
- [docs/CARD_MODULE_BUILD.md](docs/CARD_MODULE_BUILD.md)：固定 SDK、源码构建、验证和发布步骤。
- [docs/PORTRAIT_UI.md](docs/PORTRAIT_UI.md)：原竖拍布局、触控及历史实机记录。
- [docs/PROGRESS.md](docs/PROGRESS.md)、[PLAN.md](PLAN.md)：当前进度和未完成事项。
- [docs/card-module-release.json](docs/card-module-release.json)：当前插件摘要、源码指纹、ABI 和实际验证范围。版本号、大小和摘要以此文件及实际产物为准，不从历史对话推断。

正式源码在 `src/`，不要从本地实验目录恢复废案作为默认实现。

| 路径 | 职责 |
| --- | --- |
| `src/card_module/main.c` | 模块生命周期、设置、菜单和持久状态 |
| `src/card_module/vui_card.inc` | 竖拍布局、触控及 LCD 直方图适配 |
| `src/card_module/custom_disp/` | LCD/EVF 自定义页、独立取数、共用绘制和缓存发布 |
| `src/card_module/custom_disp_{install,runtime}.inc` | 自定义页菜单、配置持久化、显示许可投影与恢复 |
| `src/firmware_hooks/` | 固件钩子生成与研究代码；普通卡模块修改无需改这里 |
| `patch.py`、`patch_data/patches.json` | 校验用户提供的固件并生成本地结果 |
| `tests/card_module/` | 编译实际 C 辅助函数的宿主逻辑测试 |
| `tools/windows_packager/` | Windows 工具、数据包生成和校验 |

## 工作范围与文件边界

- 默认用中文沟通，直接完成已授权的阅读、修改、构建和检查；保留用户已有的未提交改动，不顺手回滚或格式化无关文件。
- 自定义 DISP 工作默认只改卡模块，沿用现有固件。用户未明确要求时，不修改固件补丁、不刷机、不运行 QEMU 或 ARM 动态执行；可做只读固件分析、静态反汇编、宿主测试和交叉编译。
- `experiments/`、`build/`、`output/`、`third_party/` 是本地研究或生成目录，不是发布源码。不要强制加入 Git，不让正式构建依赖实验源码或相邻仓库的实验实现。
- 不提交原厂/修改后固件、解码 MAIN、内存转储或密钥；遵守 [NOTICE.md](NOTICE.md)。允许发布的是项目自己的 `card/Z5APP.BIN`、正式源码、补丁数据及默认五个音效。
- 不改 `.DS_Store` 和个人音效 `card/SHUTTER/6.WAV`；默认音效发布范围仅 `1.WAV` 至 `5.WAV`。不要把相机上的个人 `MENU.DAT` 当构建产物覆盖。
- 未获明确指示不提交、推送或发布；交付时说明改动、验证范围和仍需实机确认的部分。

## 卡模块实现约束

- 这是独立运行的 ARM C 代码，不是桌面程序。沿用现有 `u32/u16/u8`、`Z5_FN` 和 `Z5_IMAGE` 用法；避免引入 libc 或意外的 `__aeabi_*` 链接依赖，变量除法参考现有 `level_div()`。
- 加载器会释放原始模块映像。常驻回调及其引用必须位于保留代码段、持久状态或显式堆区；不能保留原始映像中的临时函数、字符串或数据指针。
- 保持固件读取的 ABI 和 SDK 状态容量断言有效，特别是 `cm_t.vui_fn` 的偏移。私有状态布局改变时同步更新状态修订号，避免复用不兼容的常驻状态。
- 原厂地址、调用链、SDK 和固件指纹需要有静态依据。修改 `platform.json`、`target.h.in` 或兼容性配置后通过构建器重新校验；不要为了让构建或 AE 错误消失而直接放宽指纹检查。生成的目标头在构建目录中，不手改生成文件。
- 自定义显示配置已有详细信息页：原厂内部详细页为 `DISP0`、简单页为 `DISP1`；菜单名“DISP2 自定义”不是内部页码。关闭详细信息时投影简单页显示许可，保留基础信息与电池，并比较后恢复原值。
- LCD/EVF 共用刷新调度、图形同步、尾部绘制和稀疏缓存发布；目前目标是 60Hz 请求、直方图取数开始间隔至少 120ms。请求率、回调率和物理显示 fps 分别说明。
- EVF 有效接入依赖其原厂当前 bank、尺寸和映射，不能套用 LCD 专用页面对象、UI-mode 和提交 descriptor 限制。保留两端必要的尺寸、位置和线宽适配；不要恢复已删除的独立 EVF 绘制回退。
- 刷新任务只通知 GUI；取 ISP 统计只在独立工作任务中进行，GUI 固定已完成的私有统计包。像素写入在 GUI 路径等待图形同步并复查输出/bank 后进行；恢复旧笔画在原厂绘制之前，保留原厂后来写入的像素。
- 不在原厂控件遍历过程中移动链表节点。避免重新引入整屏缓存、逐帧强制重画原厂整页或每次维护整个 EVF 位图；保留当前稀疏像素恢复和局部缓存发布机制。

## 构建与验证

在仓库根目录执行。Python 3.10+、宿主 `cc`；交叉编译另需 `arm-none-eabi-gcc`/binutils、`capstone==5.0.7`，以及应用了 `src/card_module/z5mod-sdk.patch` 的固定 SDK。SDK 基础提交为 `8cf0fb425a4988abdb9442f33a2773c7a242dc23`；文件摘要由 `custom_disp/sdk_sources.json` 校验。具体准备步骤见构建文档。

```sh
python3 -B src/card_module/build.py \
  --sdk /path/to/patched-z5mod/sdk \
  --firmware /path/to/current-main.BIN \
  -o build/card-new/Z5APP.BIN
```

输出目录必须尚不存在。`--firmware` 是自备的第 108 组 MAIN，不是外层官方更新包。重现双固件兼容发布版时额外传 `--legacy-firmware /path/to/legacy-main.BIN`；接受的摘要见 `custom_disp/compatibility.json`。不要因缺少私有输入而伪造构建或实机结果。

卡模块相关检查：

```sh
python3 -B tests/card_module/verify_custom_disp.py
python3 -B tests/card_module/verify_config.py
python3 -B tests/card_module/verify_refresh_override.py
python3 -B tests/card_module/verify_histogram_raster.py
python3 -B tests/card_module/verify_output_sync.py
```

`verify_custom_disp.py` 可重复传 `--firmware`，检查真实 MAIN 的初始显示掩码。打包工具回归使用带 Tkinter 的 Python，不会打开窗口：

```sh
python3 -B -m unittest discover -s tools/windows_packager -p 'test_*.py'
```

运行与改动相关的检查；仅文档变更无需重建插件。构建器包含原厂依赖、CRC、保留映像/直接分支和 ABI 检查，不要绕过。宿主测试不运行相机 GUI/ISP，不能据此声称实机帧率或完整组合通过。

## 更新插件产物

卡模块交付需同步以下内容，不能只改二进制或只改摘要：

1. 将通过检查的产物复制到 `card/Z5APP.BIN`。
2. 更新 `patch_data/patches.json` 的 `card_module_sha256`；卡模块任务保持固件 edits、appends 和输出摘要不变，并核对差异。
3. 从本次 `report.json` 更新 `docs/card-module-release.json`，如实记录源码摘要、检查结果、是否实机验证。更新用户文档中的版本和绘制行为；中文/英文 README 保持一致。
4. 需要数据包时运行 `python3 tools/windows_packager/make_update.py --repo . --version <version> --output build/<version>.zip`，输出文件必须尚不存在。校验包只含清单、插件、补丁数据和五个默认音效，并测试导入/校验流程。
5. 最后检查 `git diff --check`、`git diff --stat` 和 `git status --short`，确认没有实验文件、私有输入或无关改动混入。

现有兼容固件用户只需替换卡模块并完全断电重启。上游旧 Windows EXE 不自动包含本 fork 插件，需导入本 fork 数据包或重建工具。
