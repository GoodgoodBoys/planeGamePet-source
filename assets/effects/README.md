# 对战特效资源（3.0.4）

## 最终运行素材

3.0.4 在 3.0.3 的长拖尾基础上，仅将亮芯宽度 2 → 1.5px、外光 4 → 3px，各缩窄 25%；长度、保留时间、亮度、受损淡出及下列位图均保持不变。

| 资源 | 帧数 / 布局 | EXE 资源 ID | 运行方式 |
| --- | --- | --- | --- |
| hit-12.png | 12 / 4×3，每帧 32×32 | 171 | 0.48 秒，跟随被击中飞机的实际接触偏移 |
| explosion-b-12.png | 12 / 4×3，每帧 64×64 | 172 | B 款火焰转灰烟，1.08 秒，只播放一次 |
| smoke-b-6.png | 6 个独立变体 / 3×2，每格 32×32 | 173 | 随机选型、后向 ±15° 漂移、逐渐变大并淡出 |

A 拖尾不使用静态贴图。desktop/game_effects.h 以 60Hz 记录双翼后缘的世界坐标，历史点独立保留并向后漂移，移动和转弯会留下真实路径。3.0.3 将亮芯从 1px 增至 2px，增加低透明度的 4px 外光；寿命从 0.52 秒延长至 1.05 秒，后移速度由 42 增至 58px/s，静止时完整几何长度从约 22px 增至 61px。3/2 血不透明度上限 0.96，1 血 0.38，以 (1-相对年龄)^1.25 淡出，避免旧版尾段过早不可见；每侧机翼最多 72 个点，足以容纳完整路径。受损烟雾保持不变：2 血时以 0.22–0.32 秒的间隔发射，初始尺寸 6–8 像素；1 血时改为 0.075–0.115 秒和 10–13 像素。尺寸随年龄扩大至约 1.95 倍，存活 0.70–1.00 秒。发射角度限制针对每个粒子的初始速度，并非将转弯后已经形成的烟雾固定在飞机身后。

## 源图与导入

- 用户批准的 A 拖尾和 B 爆炸参照：../vfx-concepts/2026-09-08/wing-trails-abc-v1.png、defeat-explosion-abc-v1.png。
- 最新烟雾参照：../vfx-concepts/2026-09-08/damage-smoke-b-style-15deg-v5.png。
- 命中特效直接沿用此前批准的 ../hit-concepts/2026-09-08/12-frame-v1/hit-12-registered.png，没有采用本次重新生成的命中版本。
- source/explosion-b-keyed.png 与 source/smoke-b-keyed.png 是内置 imagegen 生成并整理的生产源图。
- 内置工具初次返回的是实色棋盘格而不是真透明。因此又通过内置工具只处理背景，改为品红色键。导入程序将色键转为 RGBA，去除边缘品红，注册帧中心并以最近邻采样缩小。没有用程序重画烟雾或火焰。
- 运行 ./assets/effects/prepare_effects.ps1 可从仓库源图重建三个 atlas。不需要 API、外部服务或用户临时目录。
- 每帧透明像素、非空内容及品红残留由 tests/game_effects_render_test.cpp 直接读取 EXE 资源检查。帧缓存只创建一次，不在每个刷新周期重新解码。

## 生成方式与最终提示词

使用 imagegen 技能的内置工具模式；未使用 CLI/API 回退。以下保留实际提示词，便于后续复现设计意图。AI 生成不保证逐像素复现，生产重建应使用上述已保存源图。

### B 爆炸 12 帧

Use case: precise-object-edit. Input is an approved aircraft explosion choice sheet. Use ONLY ROW B, the orange/yellow fire-to-gray-smoke destruction; ignore A and C. Produce the PRODUCTION VFX-ONLY animation as EXACTLY 12 frames, 4 columns by 3 rows equal square cells, 4:3 canvas, no outer margins, no text/grid/gutters. Genuinely transparent alpha backdrop, no checkerboard or opaque color. DO NOT INCLUDE ANY AIRCRAFT in any frame: the game separately draws the existing airplane beneath the first flash. 01 tiny hot white-yellow ignition, 02 growing orange-lobed core, 03 bigger flame, 04 full bright white/yellow/orange blast, 05 peak B-style flame plus surrounding rounded medium-gray cloud lobes, 06 fading core with smoke billowing, 07 smaller orange flame separated by expanding gray puffs, 08 sparse orange embers inside expanding gray clouds, 09 only gray clouds and few embers, 10 gray clouds breaking into low-opacity remnants, 11 very faint gray remnants, 12 almost invisible tiny smoke traces. SAME origin exact center in each cell; same pixel block size across all frames; do not auto-fit different frames to cell width. Peak covers about 70 percent cell width, everything inside generous transparent margin, no clipped sparks. B palette and detailed layered cloud pixel art exactly like reference. Compact airplane destruction, no rings or mushroom cloud. Temporal progression continuous, no second explosion, no spinning or central drift.

### 烟雾 6 个变体

Use case: precise-object-edit. Input shows approved detailed B-style gray smoke behind airplanes. Create ONLY SIX SEPARATE detailed GRAY SMOKE PUFF sprites based faithfully on those approved cloud lobes. Production 3 columns by 2 rows, equal square cells, exactly 3:2 canvas, no outer border/gutters/text/planes/flames/trails. Actual transparent alpha background, not painted checkerboard/navy. One centered coherent rounded multilobed smoke clump per cell, all approximately same maximum diameter and generous 20 percent padding, no clipped edges. Six subtle silhouette variations with different overlapping lobes and interior shading, not six stages/sizes of an explosion. Preserve B's charcoal recesses, medium warm-gray body, restrained light-gray lobe highlights, rich crisp square pixel clusters, no white smoke, black outline, blur or rock texture. Every puff is neutral gray and full opacity at its core with naturally feathered pixel-cluster alpha edges; game handles size expansion, rotation and fading. No loose distant fragments or tiny plus signs. Target recognizable detail at 16-28 display pixels.

### B 爆炸背景整理

Use case: precise-object-edit. ONLY replace ALL white/light-gray checkerboard BACKDROP with perfectly uniform vivid magenta RGB(255,0,255), hexadecimal #FF00FF, for game color-key import. Preserve all twelve explosion/fire/smoke frames EXACTLY, including bright white/yellow fire cores and dark gray cloud details. Do not alter shape, positions, dimensions, scale, frame layout or internal pixels. Gaps between smoke/sparks are background and must also become uniform magenta. Absolutely NO checkerboard, transparency simulation, shading or gradients in background. Uniform magenta only outside the actual sprites. Preserve the existing sheet aspect ratio and frame order. No text. This is a technical background-only edit for keyed-sprite export; do not redesign artwork.

### 烟雾背景整理

Use case: precise-object-edit. ONLY replace ALL white/light-gray checkerboard BACKDROP with perfectly uniform vivid magenta RGB(255,0,255), hexadecimal #FF00FF, for game color-key import. Preserve all six detailed gray smoke puffs EXACTLY, including bright white/yellow fire cores and dark gray cloud details. Do not alter shape, positions, dimensions, scale, frame layout or internal pixels. Gaps between smoke/sparks are background and must also become uniform magenta. Absolutely NO checkerboard, transparency simulation, shading or gradients in background. Uniform magenta only outside the actual sprites. Preserve the existing sheet aspect ratio and frame order. No text. This is a technical background-only edit for keyed-sprite export; do not redesign artwork.

## 接入约束

特效使用独立的单调时钟；不修改移动速度、碰撞体积、弹道、生命值、协议、战绩或匿名统计。命中特效沿用同一战斗呈现时刻和碰撞分数，禁止预播未来事件，按命中 ID 去重。所有效果受游戏区域裁剪，不能绘制到顶部状态栏。

收到击毁结算时立即显示 WIN!/LOST!，不等待爆炸。提前单击、按键或点击关闭时记住返回操作，爆炸播完后执行。对方先返回导致服务器进入菜单时，本端短暂保留结算画面，后台连接照常处理。3 分钟平局、断线和同步中止不伪造击毁爆炸；紧急隐藏与退出不受动画阻挡。
