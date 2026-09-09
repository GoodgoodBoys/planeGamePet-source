# C 拳头：收到挑战时的攻击动画候选

仅供用户确认的素材预览，尚未接入 Plane Pet，也没有修改邀请逻辑或发布新版本。

## 文件

- `c-attack-preview.gif`：480 × 480 放大预览，蓝边黑底圆框固定。
- `c-attack-48px.gif`：48 × 48 按钮大小预览。
- `c-attack-transparent.gif`：160 × 160 透明拳头动画，无圆框。
- `c-fist-master.png`：imagegen 提取的独立透明 C 拳头原图。
- `frames-transparent/`：80 张透明 PNG 序列，保留 alpha，供后续确认后使用。
- `keyframes.png`：12 个关键时刻的接触表，用于读图检查。
- `animate.ps1`：重现动画及 GIF 编码，不重绘或变形手指。

## 动作

25 FPS，80 帧，3.2 秒无限循环。短暂等待 → 左下回收蓄力 → 右上出拳并均匀放大 → 收回 → 第二拳 → 平滑返回并停顿。圆框不移动，不作上下弹跳。所有帧使用同一素材，仅改变位置和等比缩放，采用三次缓动和最近邻采样。

本轮检查：三份 GIF 均解码为 80 帧 / 3.2 秒；透明帧首尾 PNG SHA256 相同；所有可见主体像素在圆框内；已检查关键帧的轮廓、手指形状和布局。小尺寸预览不代表已经在真实邀请窗口中验收。

## 来源与生成方式

使用内置 imagegen 编辑提取静态素材，再用本地绘制脚本进行时间轴编排和 FFmpeg GIF 编码；未使用图像生成 CLI 或视频模型。

原始 C 候选：`C:/Users/81228/.codex/generated_images/01a05a85-ae82-78f0-97a3-5d5467bce706/exec-64c41cbe-e840-491c-962d-31327a9a3aad.png`。

生成原图：`C:/Users/81228/.codex/generated_images/01a05a85-ae82-78f0-97a3-5d5467bce706/exec-5a79c92d-6824-4760-bf76-fe60cbe3cb53.png`，已复制为本目录 `c-fist-master.png`。

### 最终图像提示词

```text
Use case: background-extraction.
Input image 1 is the edit target: user-selected C yellow pixel-art fist card.
Extract ONLY the large upper yellow three-quarter-view punching fist as a standalone game sprite on a genuinely transparent RGBA background. Preserve the exact diagonal pose, silhouette, four curled fingers and folded thumb anatomy, golden yellow/amber/brown pixel palette, stepped edges, and short wrist. Do not redesign, rotate, change the gesture or add fingers. Remove the C label, small lower duplicate fist, circular blue button, and all background completely.
Composition: one fist centered in a square canvas with 14 percent transparent padding on every side; do not crop the wrist or knuckles. Faithful hard-edged pixel art, no new smoothing, no blur, no motion trails, no sparkles, no text, no button, no shadows outside the silhouette. This is the stable master sprite that will be translated/scaled in an attack animation, so preserve the selected C fist closely.
```
