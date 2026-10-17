# LoFi100 菜单图标

- `lofi100.png`：非选中状态，92×64。
- `lofi100-selected.png`：选中状态，92×64，内部金色高光与柔和光晕。
- 同名 SVG 为可编辑源文件；字形保持宽高比，按横向画布重新排版。

`CMakeLists.txt` 按非选中、选中的顺序将两张 PNG 打包进 `.g3a`。
菜单显示名留空，避免系统叠加的名称盖住 AUDIO；文件名仍为 `LoFi100.g3a`。
修改 SVG 后，需要同步导出对应的 92×64、不带透明通道的 RGB PNG；构建直接使用 PNG，无需额外的 SVG 渲染工具。

设计改编自 [Hi-Res Audio 原始 SVG](https://en.wikipedia.org/wiki/File:Hi-Res_Audio_(logo).svg)，
将文字改为 Lo·Res，并调整字形、间距和布局。原始页面标注为 PD-textlogo。
