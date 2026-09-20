# nestlone-desktop

**nestlone-D** 是一个 Windows 桌面收纳工具。它保持文件的原始路径，仅保存图标归属、分组和布局信息。

![nestlone-D 效果展示：透明桌面盒子的图标视图、列表视图与右键菜单](docs/images/desktop-preview.png)

## 功能

- 半透明盒子、图标/列表视图、缩放、折叠和原地重命名。
- Windows Shell 图标、双击打开、拖放整理和右键菜单。
- 拖动盒子标题合并为标签分组；拖动标签可脱离分组。
- 自动分类预设与按文件夹、扩展名定义的规则。
- 时间戳布局备份、应用和删除。

## 环境要求

- Windows 10 或 Windows 11
- Visual Studio Build Tools，包含 MSVC C++ 桌面工具与 Windows SDK（`cl.exe`、`rc.exe`）

不依赖第三方运行时或包管理器。

## 构建

```bat
build.bat
```

输出文件为 `build\nestlone-D.exe`。启动后通过系统托盘访问功能。

## 使用方式

1. 从托盘菜单新建盒子并拖入桌面项目。
2. 拖动顶部栏移动，拖动右下角缩放，双击标题重命名。
3. 将一个盒子拖到另一个标题栏以合并分组；拖出标签以拆分。
4. 在设置中心配置自动分类和布局备份。

## 安全边界与已知限制

nestlone-D 不移动文件、不改写隐藏属性、不修改壁纸或全局桌面选项。运行时会暂时隐藏 Explorer 桌面列表，并在退出或隐藏时恢复。

- 关闭桌面“自动排列图标”后才能应用位置规则；程序不代为更改此选项。
- UIPI 会限制跨权限定位，程序应与 Explorer 使用相同权限运行。
- 与 Explorer 的完整键盘导航和无障碍行为仍有差异。
- 非默认图标间距、特殊视图和混合 DPI 多显示器需要进一步验证。

## License

本项目采用 [MIT License](LICENSE)。

## Credit

First introduced on the [LINUX DO](https://linux.do/) community — thanks to everyone there for the first round of discussion.
