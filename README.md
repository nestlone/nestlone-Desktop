# nestlone-desktop

**nestlone-D** 是一个 Windows 桌面收纳工具：读取真实桌面路径和系统图标，在桌面层绘制可折叠的自有盒子视图。文件不会被移动到应用目录，盒子只保存路径归属、分组和显示布局。

![nestlone-D 效果展示：透明桌面盒子的图标视图、列表视图与右键菜单](docs/images/desktop-preview.png)

*上图为早期展示效果；当前版本使用自有图标绘制层，并提供分组标签、自动分类与布局备份。*

## 功能

- 高透、清新的赛璐璐风格盒子；可调整背景色与背景透明度，文字与图标保持清晰。
- 保持真实桌面文件路径不变，使用 Windows Shell 图标和右键菜单；支持选择、双击打开和拖放整理。
- 拖动标题栏移动盒子，拖动右下角手柄缩放；双击标题原地重命名盒子。
- 将一个盒子拖到另一盒子的标题栏即可合并为分组；点击标签切换分类，拖动标签可脱离分组。
- 设置中心的“自动分类”提供目录、文档、图片、压缩包预设，以及按文件夹和扩展名创建的自定义规则。
- 设置中心的“备份与还原”可创建、应用和删除时间戳布局快照。
- 自有盒子内支持图标点击、双击打开、拖动排版和 Shell 右键菜单委托；路径仍保持原位置。
- 标题栏“▦”整理图标位置，标题栏右键可收纳矩形内已有图标。
- 托盘菜单提供显示切换、新建盒子、设置与退出入口。
- 启动、隐藏、删除盒子和退出均保留真实图标；收起背景时图标也保持可见。
- 应用程序与托盘均使用内嵌的透明图标资源。

## 环境要求

- Windows 10 或 Windows 11
- Visual Studio Build Tools，包含 MSVC C++ 桌面工具与 Windows SDK（`cl.exe`、`rc.exe`）

不依赖第三方运行时或包管理器。

## 构建

在“x64 Native Tools Command Prompt for Visual Studio”或普通 `cmd` / PowerShell 中执行：

```bat
build.bat
```

默认输出为 `build\nestlone-D.exe`。指定输出目录和文件名：

```bat
build.bat release nestlone-D.exe
```

运行生成的 `nestlone-D.exe` 后，可从系统托盘打开菜单。

## 发布版本

推送以 `v` 开头的 Git 标签会触发 GitHub Actions，在 Windows x64 Runner 上重新编译并创建 / 更新同名 GitHub Release。Release 附件包含单独的 `nestlone-D.exe` 与 `nestlone-D-windows-x64.zip`。

```bat
git tag v0.1.1
git push origin v0.1.1
```

工作流需要仓库的 Actions `GITHUB_TOKEN` 拥有 **Contents: read and write** 权限。详情见 [.github/workflows/release.yml](.github/workflows/release.yml)。

## 使用方式

1. 通过托盘菜单新建一个盒子。
2. 拖动盒子的顶部栏移动位置；拖动右下角手柄调整背景大小。
3. 顶部栏按钮依次为整理图标位置、收起 / 展开背景、删除盒子。
4. 双击盒子标题，在原位置直接输入新名称；按 Enter 或点击别处保存，按 Esc 取消。
5. 将图标拖入盒子即可收纳；拖回桌面会自动吸附到最近的空网格，避免重叠。
6. 将盒子标题拖到另一标题栏可合并为标签分组；从标签拖出即可恢复独立盒子。
7. 在设置中心启用自动分类，或勾选预设分类盒子来立即扫描并收纳已有项目。
8. 在“备份与还原”中创建布局快照；应用备份会恢复盒子、分组和分类规则。

## 安全边界与已知限制

nestlone-D 不移动文件、不改写隐藏属性、不修改壁纸、不改变“自动排列”或其他全局桌面选项。应用会暂时隐藏 Explorer 的桌面列表并在同一桌面层绘制 Shell 图标；退出或隐藏时会恢复 Explorer 列表。

- 关闭桌面“自动排列图标”后才能应用位置规则；程序不代为更改此选项。
- UIPI 会限制跨权限定位，程序应与 Explorer 使用相同权限运行。
- 当前自有视图支持按盒子折叠隐藏图标、框选和 Shell 右键菜单；与 Explorer 的完整键盘导航和无障碍行为仍有差异。
- 自动分类规则会保存，但针对后续新文件的持续监测仍在完善中；预设分类可立即扫描当前桌面。
- Explorer 重启后重建背景与标题栏；图标由 Explorer 自行恢复，程序不在重连时强制重排。

该机制仍属实验性功能：非默认图标间距、特殊桌面视图模式和混合 DPI 多显示器尚需更多人工验证。完整设计与验证范围见 [docs/DESKTOP-INTEGRATION.md](docs/DESKTOP-INTEGRATION.md)。

## 测试

```bat
tests\render\run.bat
tests\hosting\session.bat
```

- `render`：背景透明度、折叠状态与“不绘制任何图标”回归测试。
- `hosting`：自有图标表面、Explorer 列表隐藏/恢复、Z 序、路径和布局不变测试，使用专用临时布局。
- 可选真实定位测试：`tests\hosting\session.bat --position-test`，仅移动已有专用测试目录 `DeskBox-hosting-test-20260914`，随后恢复其原位置。默认测试不移动用户图标。

## 目录结构

```text
assets/     应用与托盘图标资源
docs/       设计说明与已知限制
src/        Win32 C++ 源码与资源脚本
tests/      当前维护的渲染和桌面交互测试
build.bat   一键构建脚本
```

## License

本项目采用 [MIT License](LICENSE)。

## Credit

First introduced on the [LINUX DO](https://linux.do/) community — thanks to everyone there for the first round of discussion.
