# 12 帧小型命中爆炸预览

本次仅制作素材预览，未接入游戏、未修改应用逻辑或服务器。

- 绘制模式：内置 image_gen；未使用 CLI/API 回退。
- 动作：12 个不同的动作帧，每帧 40 毫秒，单次爆炸约 0.48 秒。
- GIF：320 × 320 放大预览，循环之间约 0.68 秒空白。编码共 14 帧，其中最后两帧为空白停顿，不是额外动作帧。
- 背景：深色实底预览，非透明生产素材。
- `hit-12-preview.gif`：循环动图。
- `hit-12-registered.png`：按播放顺序从左到右、从上到下排列的 4 × 3 帧图。
- `frames/hit-01.png` 至 `hit-12.png`：单帧文件。
- `assemble-preview.ps1`：裁切、对齐与 GIF 组装脚本；不重绘素材。

检查：12 个动作帧内容互不重复，中心位置已校对；GIF 前 12 帧均非空且每帧 40 毫秒，随后为空白停顿，无限循环。

## 绘制要求摘要

以原四帧为关键帧补绘为十二帧，保留黄白亮心、橙色外沿、方块像素风。过程依次为闪光、扩张、中心收缩、火花分离、余烬消散。固定爆炸中心，不增加烟雾、光圈或飞机。第一轮生成带有实色棋盘底，因此进行了仅替换背景并校准中心的第二轮编辑。

## 最终编辑提示词（原文）

Use case: precise-object-edit. Edit target: supplied 12-frame pixel explosion sheet. Preserve ALL TWELVE existing explosion drawings, their colors, their relative evolution and sizes, and their order, WITHOUT redesigning the effect. Change only the background and registration. Replace every white/gray CHECKERBOARD BACKGROUND pixel, including the empty holes between sparks, with one absolutely uniform solid dark navy #090d16. IMPORTANT: preserve the bright white hot centers in frames 1-7; these are part of the explosion, not background. Do not draw a checkerboard, transparency simulation, gradient, glow, texture or frame borders. The result is a 4-column by 3-row sheet on solid dark navy for GIF preview. Make canvas exactly 4:3, with 12 exactly equal square cells and no outside margin. Place the fixed explosion ORIGIN at the exact center of EVERY cell, so it does not drift across an animation. Frame 1 through 7 origins are their hot central flash; for 8 through 12, align the central empty point between the SAME outward-going spark trajectories to that same origin. Do not center based on changing spark bounding boxes. Maintain a single identical pixel-block scale through the entire sheet; do not scale individual frames to fill their cells. The first flash stays small, peak stays medium, sparse final embers are spread out but dim. All frames have enough padding and never touch cell boundaries. Do not add any other elements, text, numbers or watermarks. Only background replacement and fixed-origin registration, keep the twelve-frame artwork otherwise unchanged.
