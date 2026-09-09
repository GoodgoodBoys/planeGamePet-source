# C 拳头攻击预览 V2

用户要求：每轮 2 秒、去掉圆框、周围增加击中的爆炸特效。仅制作预览，未接入程序，V1 保留。

## 交付

- `c-attack-preview.gif`：480 × 480 深色底放大预览，无圆框。
- `c-attack-transparent.gif`：160 × 160 透明背景动画。
- `c-attack-48px.gif`：48 × 48 透明背景的小尺寸预览。
- `frames-transparent/`：50 张透明 PNG 帧，保留完整 alpha。
- `keyframes.png`：12 个关键时刻的读图检查表。
- `animate.ps1`：可重现的动画时间轴、图层编排和 GIF 编码脚本。

25 FPS × 50 帧 = 精确 2 秒。保留两次出拳，用同一 C 拳头原图作平移和等比缩放。每次出拳到位时，指节周围三个像素爆炸短暂扩散消失，不出现圆框、不改变手指造型。

检查通过：GIF 解码为 50 帧 / 2.000 秒；首尾透明 PNG 相同，GIF 解码后的首尾像素 MD5 也相同；拳头原图 SHA256 与 V1 相同；全帧可见主体及特效离画布边缘至少 4 像素，无边缘裁切；已读图检查关键帧。未进行真实邀请窗口测试。

## 素材来源与提示词

拳头原图沿用 V1 的 `c-fist-master.png`，未重绘。像素爆炸使用内置 imagegen 新生成，复制为本目录 `impact-master.png`，原始路径：
`C:/Users/81228/.codex/generated_images/01a05a85-ae82-78f0-97a3-5d5467bce706/exec-eadf1ce9-c0e1-4324-9e66-573f16284790.png`。

图像生成使用内置工具，未使用 CLI 图像 API 或视频模型；本地脚本仅编排图层动画并用 FFmpeg 编码 GIF。

最终提示词：

```text
Use case: stylized-concept.
Asset type: isolated pixel-art punch HIT EXPLOSION effect sprite, for compositing around an existing yellow fist animation.
Generate ONE compact asymmetric comic impact starburst, warm burnt-orange outer edge, bright orange middle, golden yellow inner region and a small pale-cream center. 5–7 chunky jagged rays with stair-stepped contours, plus only three tiny detached square orange sparks nearby. Strong crisp old-school pixel art, coherent square pixel grid, simple readable clusters suitable at 24–40 pixels, no gradients, no blur, no glow halo, no soft smoke, no noise.
Transparent RGBA background, genuinely transparent. Center the isolated effect with at least 20 percent transparent padding on all sides.
This is an impact burst that briefly appears near the knuckles when a fist lands. Do not draw any fist, hand, character, weapon, circle, ring, button, frame, background panel, lettering, watermark or text. Output a single effect sprite, not a sprite sheet.
```
