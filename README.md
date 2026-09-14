# nestlone-desktop

**nestlone-D** 是一个 Windows 桌面收纳盒：用可自由移动、缩放、折叠与换肤的透明盒子整理桌面项目，同时保留 Windows 原生图标的外观与打开方式。

> 当前处于实验阶段，建议先在非关键桌面环境中体验桌面图标托管功能。

## 功能

- 高透、清新的赛璐璐风格盒子；可调整背景色与背景透明度，文字与图标保持清晰。
- 列表 / 图标两种视图，支持折叠、拖动、边缘缩放与双击标题原地重命名。
- 使用 Windows Shell 提供的原生文件、文件夹和快捷方式图标。
- 托盘菜单提供显示切换、新建盒子、设置与退出入口。
- 图标从桌面拖入盒子后，仅遮罩该原生图标的显示区域；下次启动会恢复该盒子状态。拖回桌面、退出、暂停或异常结束时恢复原生桌面区域。
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
git tag v0.1.0
git push origin v0.1.0
```

工作流需要仓库的 Actions `GITHUB_TOKEN` 拥有 **Contents: read and write** 权限。详情见 [.github/workflows/release.yml](.github/workflows/release.yml)。

## 使用方式

1. 通过托盘菜单新建一个盒子。
2. 拖动盒子的顶部栏移动位置；拖动边缘调整大小。
3. 单击顶部栏右侧按钮切换列表 / 图标视图或折叠盒子。
4. 双击盒子标题，在原位置直接输入新名称；按 Enter 或点击别处保存，按 Esc 取消。
5. 将现有桌面图标拖入盒子。该图标在原生桌面的位置会被局部遮罩，并在下次启动时保持这个状态；从盒子拖回空白桌面可恢复它。

## 安全边界与已知限制

nestlone-D 不移动桌面文件、不改写隐藏属性、不修改壁纸、不改变“自动排列”或其他全局桌面选项。它使用 Windows Shell 的桌面视图并对明确拖入盒子的图标进行局部显示遮罩。

该机制仍属实验性功能：非默认图标间距、特殊桌面视图模式和混合 DPI 多显示器尚需更多人工验证。完整设计与验证范围见 [docs/DESKTOP-INTEGRATION.md](docs/DESKTOP-INTEGRATION.md)。

## 测试

```bat
tests\render\run.bat
tests\hosting\session.bat
```

- `render`：透明度、折叠状态和 Shell 图标透明边缘回归测试。
- `hosting`：原生桌面不变、局部遮罩、拖回恢复及异常恢复测试。测试会使用专用的临时布局文件。

## 目录结构

```text
assets/     应用与托盘图标资源
docs/       设计说明与已知限制
src/        Win32 C++ 源码与资源脚本
tests/      当前维护的渲染和桌面交互测试
build.bat   一键构建脚本
```
