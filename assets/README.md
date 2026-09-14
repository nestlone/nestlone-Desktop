# nestlone-D visual direction

高透清新的赛璐璐视觉：冰白玻璃、青蓝主色、薄荷辅助色、珊瑚色状态点。

- `nestlone-mark.svg` 是可编辑的品牌母题源稿。
- `nestlone-icon.png` 是带 Alpha 透明通道的正式图标母图，发布版位于 `release/assets/`。
- `IconFactory.cpp` 使用同一组几何与配色生成 Windows 托盘/程序图标，避免缩小后出现模糊边缘。
- 界面样式集中在 `DesktopCanvas.cpp` 的 `Theme` 常量，后续可只改这一处完成换肤。
