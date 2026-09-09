# W1 WIN! 循环动态素材

选择记录：WIN 使用 W1 橙金街机；LOST 已选 L1 绯红街机，本次只制作 WIN 动画，未接入游戏、未编译或修改应用版本。

## 文件

- `win-w1-preview.gif`：768×320 深色背景预览，50 fps，120 帧，每轮 2.4 秒。
- `win-w1-transparent.apng`：384×160 真 RGBA 透明底动画，120 帧。
- `rgba-frames/000.png`–`119.png`：透明底逐帧 PNG。
- `win-w1-spritesheet.png`：3840×1920，10 列×12 行，按行顺序，每格 384×160。
- `win-w1-master.png`：透明底母版，同一字形在所有帧中复用。
- `animation.json`：帧率、时间轴、布局参数。
- `win-w1-keyframe-check.png` / `gif-decoded-check.png`：导出前和 GIF 解码后的检查图。

## 时间轴

0–180 ms 从 100% 放大到 112%；180–700 ms 保持 112%，从左至右扫过加宽的玻璃亮带，包括最后的感叹号；700–900 ms 缩回 100%；900–2400 ms 原样停留，随后循环。不做淡出。

主亮带和伴随亮带的归一化宽度均是此前已批准 HIT 动图的 2 倍（参照原 `PreviewAnimation.cs` 中的 14/6 像素宽度，按母版文字宽度归一化），不是只加宽显示画布。亮带只作用于字面，不修改轮廓或字距。

## 制作与验收

使用内置 image_gen 整理已批准的 W1 母版；初次输出带实色棋盘格，随后用内置 image_gen 改为干净的深色底。经技术性背景抠色得到真透明 PNG，未把棋盘格或黑底当作透明层。动画沿用现有 C# 补帧方法与 ffmpeg 编码；没有使用图像 API CLI 回退。

已确认：字母为 WIN!；120 帧/50 fps/2400 ms；APNG 解码为 RGBA；所有停留帧与首帧逐像素一致；GIF 与 APNG 各自的首尾解码帧 MD5 一致；全部帧不裁切；扫光完整经过感叹号再缩回。人工检查关键帧与 GIF 解码检查图通过。

运行 `pwsh -NoProfile -File animate.ps1` 可重导出。只写此素材目录，保留已批准的参考图，不改应用代码。

## 内置 image_gen 提示词

### W1 母版整理

Use case: background-extraction.
Input image: approved WIN choice sheet. Edit target is ONLY W1, the orange-gold "WIN!" in FIRST ROW, column "01 原样".
Produce a single clean transparent-background master sprite of that exact W1 word. Text exactly "WIN!" (W I N !). Preserve its chunky, tightly spaced, right-slanted pixel arcade glyphs, broad W, slim I, diagonal N, exclamation mark, orange-gold stepped fill, pale yellow upper pixel bevel, orange lower edge and shallow dark reddish-brown extrusion. Isolate that W1 lettering faithfully, enlarged cleanly as a raster game sprite. NOT W2, NOT W3, NOT the title. No new decorative elements and no changes to typography or palette.
Output ONLY ONE "WIN!" centered on truly transparent RGBA canvas, approximately 1024x512 landscape. Word fills about 80% of canvas width. Keep all contours crisp, deliberate stepped square pixel edges. Full word including the dark outline/extrusion must be intact, with transparent padding all around.
No moving shine stripe in this master: use the resting 01 appearance, only its normal fixed bevel shading. Do not include other frames, labels, numbers, border, grid, captions, opaque/checkerboard background, ground shadow, starbursts, particles, crowns, blur or watermark. Transparency must be real alpha, not an illustrated checkerboard.

### 背景修正

Use case: precise-object-edit. The referenced raster is the edit target. Change ONLY the checkerboard background: replace ALL gray checkerboard and its cloudy artifacts, including the gaps between letters, with a PERFECTLY FLAT SOLID dark navy RGB(7,10,19) #070A13 background. No gradients or texture in background. Keep the existing WIN! lettering EXACTLY unchanged in its shape, size, position, colors, pixel edges, bevel and reddish dark outline. Do not redraw or restyle the word. Do not add a sweeping light. No checkerboard, labels, marks, halos, new shadows, blur or decorative elements. Same composition and aspect ratio. Output one clean still master raster only.
