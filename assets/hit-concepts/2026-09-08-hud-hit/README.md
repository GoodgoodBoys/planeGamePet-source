# HUD HIT! 关键帧设计预览

日期：2026-09-08。

## 状态与范围

仅为静态风格与关键帧预览，尚未制作动图或接入游戏。用户已选择修正配色后的 C（橙金街机），随后要求稍微收紧字母及感叹号间距；最新文件为 C-arcade-orange-tight-keyframes.png，等待本轮确认。保留原始 C 以便对比。

采用 imagegen 技能的内置 image_gen 模式，不使用 CLI。图片为生成式概念稿，不是像素级定位或实际运行验收截图。未修改程序、碰撞、服务器、其他项目或运行时素材。

## 设计规则

- 击中对手的一方在自己血量旁、靠中间延迟的位置出现 HIT!；不是受击方的血量旁。
- 当前游戏各客户端以蓝色左侧代表自己。
- 关键帧顺序：出现 → 突然放大 → 亮带进入 → 扫过中间 → 扫至右侧 → 亮带扫完 → 轻微缩小 → 缓慢淡出。
- 亮带为平行斜向玻璃反光，从左向右扫过字面，不是玻璃碎裂。
- 必须等亮带完全退出再缩小。最终比例与时长尚未通过动图或游戏运行确认。
- 最新 C 的提示词要求仅减少字符间空隙约 20–25%；这只是生成指导值，不是精确测量结果。

## 文件

- A-gold-white-keyframes.png：金白锐光。
- B-ice-blue-keyframes.png：冰蓝镜光。
- C-arcade-orange-keyframes.png：用户选定的橙金街机原间距预览。
- C-arcade-orange-tight-keyframes.png：最新收紧间距版。

## 输入参考

初始参考为 dist/vfx-verification-3.0.4/side-0-018.bmp，仅用作状态栏布局、配色、飞机和爱心的风格参考。C 配色修正及间距修正分别以上一版概念图作为编辑目标。

## 初始生成提示词

### A

```text
Use case: ui-mockup.
Asset type: static pixel-art game HUD animation keyframe concept board, NOT an animation and NOT production code.
Input image 1: real 240x372 screenshot; reference for the TOP STATUS BAR only, its tiny blue/red plane icons, pixel hearts, colors, and placement. Do not reproduce the playfield. Create a new presentation board.
Primary request: design the exact text "HIT!" (letters H I T plus one exclamation mark), in italic right-slanted pixel lettering, as feedback for the SHOOTER when they hit their friend. In each client the player is BLUE on the LEFT. Place HIT! on that own/left side, immediately inward from the blue hearts toward the middle latency area. Do not place it on the victim's right/red side.
Composition: landscape 1536x1024 concept sheet with generous margins, dark navy #101622 backdrop. Top: a concise variant title, then ONE enlarged HUD placement mockup maintaining the original 240:52 proportions. In this HUD preserve blue aircraft far left, three blue pixel hearts near left, timer "170s" at top center and "80ms" below center, red pixel hearts toward right and red aircraft far right. Add a small HIT! in the clear gap between blue hearts and center text (around logical x=82,y=23), fully contained in HUD, never overlapping hearts, timer, or latency. HUD itself never grows or moves.
Below: EIGHT evenly sized keyframe cells in a 4-column x 2-row grid, numbered 01 through 08 in chronological reading order. Each cell shows ONLY enlarged HIT! on the same dark slate #1b1f2b field, the same center anchor and same letter design, and a short Chinese stage caption below. All cells use the same reference magnification so the scale differences are visible.
Exact sequence:
01 caption "出现": text at 85% baseline scale, bright, no shine bands yet.
02 caption "突然放大": text jumps to 125%, bright, no sweep yet.
03 caption "亮带进入": KEEP 125% size; two closely spaced parallel diagonal glass-reflection highlight bands enter the LEFT of H.
04 caption "扫过中间": KEEP 125% size; those SAME two parallel bands have translated horizontally across the MIDDLE of the word.
05 caption "扫至右侧": KEEP 125% size; the same bands reach T and the exclamation mark at the RIGHT edge.
06 caption "亮带扫完": KEEP 125% size; bands have completely exited and are now absent, restoring the base letter colors.
07 caption "轻微缩小": ONLY NOW shrink to 110%, no shine bands, still legible.
08 caption "缓慢淡出": keep 110% size and same center; fade entire word and outline to about 18% opacity into the dark background, no shine bands.
Shine is glass LIGHT REFLECTION, not broken glass: two parallel diagonal strips of white glint with a stepped pale highlight edge, clipped INSIDE the letter silhouettes, scanning left to right. No long external beams crossing the whole panel, no glass fragments.
Text style: authentic deliberate pixel typography, straight blocky strokes and visible square stepped diagonal edges with a consistent pixel grid; clearly italic, NOT cursive, NOT smooth vector, NOT fuzzy. Crisp enlarged nearest-neighbor-like pixels. Letterform identity stays the same across all eight cells, no spelling changes. Typography only, no enclosing badge, no hearts in keyframe cells, no explosion or particles.
Do not add shake, drifting, bounce cycles, glyph distortion, independent letter movement, lens blur, large radiating stars, or large comic speech bubbles. Do not shrink during the sweep. Minimal Chinese captions clean and easy to read. No extra explanatory paragraphs. This is a still storyboard showing scale, sequential reflection positions, settling, and fade.
Variant title, verbatim: "A · 金白锐光".
Variant styling: A: condensed bold italic pixel sans, ivory-white upper face with warm gold lower rows, thin amber edge and very short dark brown one-pixel shadow. Compact, clean, sharply readable; warm glass glints. The visual emphasis is elegant high-contrast white-gold, not orange.
```

### B

```text
Use case: ui-mockup.
Asset type: static pixel-art game HUD animation keyframe concept board, NOT an animation and NOT production code.
Input image 1: real 240x372 screenshot; reference for the TOP STATUS BAR only, its tiny blue/red plane icons, pixel hearts, colors, and placement. Do not reproduce the playfield. Create a new presentation board.
Primary request: design the exact text "HIT!" (letters H I T plus one exclamation mark), in italic right-slanted pixel lettering, as feedback for the SHOOTER when they hit their friend. In each client the player is BLUE on the LEFT. Place HIT! on that own/left side, immediately inward from the blue hearts toward the middle latency area. Do not place it on the victim's right/red side.
Composition: landscape 1536x1024 concept sheet with generous margins, dark navy #101622 backdrop. Top: a concise variant title, then ONE enlarged HUD placement mockup maintaining the original 240:52 proportions. In this HUD preserve blue aircraft far left, three blue pixel hearts near left, timer "170s" at top center and "80ms" below center, red pixel hearts toward right and red aircraft far right. Add a small HIT! in the clear gap between blue hearts and center text (around logical x=82,y=23), fully contained in HUD, never overlapping hearts, timer, or latency. HUD itself never grows or moves.
Below: EIGHT evenly sized keyframe cells in a 4-column x 2-row grid, numbered 01 through 08 in chronological reading order. Each cell shows ONLY enlarged HIT! on the same dark slate #1b1f2b field, the same center anchor and same letter design, and a short Chinese stage caption below. All cells use the same reference magnification so the scale differences are visible.
Exact sequence:
01 caption "出现": text at 85% baseline scale, bright, no shine bands yet.
02 caption "突然放大": text jumps to 125%, bright, no sweep yet.
03 caption "亮带进入": KEEP 125% size; two closely spaced parallel diagonal glass-reflection highlight bands enter the LEFT of H.
04 caption "扫过中间": KEEP 125% size; those SAME two parallel bands have translated horizontally across the MIDDLE of the word.
05 caption "扫至右侧": KEEP 125% size; the same bands reach T and the exclamation mark at the RIGHT edge.
06 caption "亮带扫完": KEEP 125% size; bands have completely exited and are now absent, restoring the base letter colors.
07 caption "轻微缩小": ONLY NOW shrink to 110%, no shine bands, still legible.
08 caption "缓慢淡出": keep 110% size and same center; fade entire word and outline to about 18% opacity into the dark background, no shine bands.
Shine is glass LIGHT REFLECTION, not broken glass: two parallel diagonal strips of white glint with a stepped pale highlight edge, clipped INSIDE the letter silhouettes, scanning left to right. No long external beams crossing the whole panel, no glass fragments.
Text style: authentic deliberate pixel typography, straight blocky strokes and visible square stepped diagonal edges with a consistent pixel grid; clearly italic, NOT cursive, NOT smooth vector, NOT fuzzy. Crisp enlarged nearest-neighbor-like pixels. Letterform identity stays the same across all eight cells, no spelling changes. Typography only, no enclosing badge, no hearts in keyframe cells, no explosion or particles.
Do not add shake, drifting, bounce cycles, glyph distortion, independent letter movement, lens blur, large radiating stars, or large comic speech bubbles. Do not shrink during the sweep. Minimal Chinese captions clean and easy to read. No extra explanatory paragraphs. This is a still storyboard showing scale, sequential reflection positions, settling, and fade.
Variant title, verbatim: "B · 冰蓝镜光".
Variant styling: B: moderately narrow italic pixel lettering with squared technical cuts and slightly lighter stroke than A; cool white and ice-cyan face, deep blue thin pixel edge, little to no extrusion. Glass-silver parallel highlights. A clean restrained cool digital style, no diffuse glow cloud.
```

### C

```text
Use case: ui-mockup.
Asset type: static pixel-art game HUD animation keyframe concept board, NOT an animation and NOT production code.
Input image 1: real 240x372 screenshot; reference for the TOP STATUS BAR only, its tiny blue/red plane icons, pixel hearts, colors, and placement. Do not reproduce the playfield. Create a new presentation board.
Primary request: design the exact text "HIT!" (letters H I T plus one exclamation mark), in italic right-slanted pixel lettering, as feedback for the SHOOTER when they hit their friend. In each client the player is BLUE on the LEFT. Place HIT! on that own/left side, immediately inward from the blue hearts toward the middle latency area. Do not place it on the victim's right/red side.
Composition: landscape 1536x1024 concept sheet with generous margins, dark navy #101622 backdrop. Top: a concise variant title, then ONE enlarged HUD placement mockup maintaining the original 240:52 proportions. In this HUD preserve blue aircraft far left, three blue pixel hearts near left, timer "170s" at top center and "80ms" below center, red pixel hearts toward right and red aircraft far right. Add a small HIT! in the clear gap between blue hearts and center text (around logical x=82,y=23), fully contained in HUD, never overlapping hearts, timer, or latency. HUD itself never grows or moves.
Below: EIGHT evenly sized keyframe cells in a 4-column x 2-row grid, numbered 01 through 08 in chronological reading order. Each cell shows ONLY enlarged HIT! on the same dark slate #1b1f2b field, the same center anchor and same letter design, and a short Chinese stage caption below. All cells use the same reference magnification so the scale differences are visible.
Exact sequence:
01 caption "出现": text at 85% baseline scale, bright, no shine bands yet.
02 caption "突然放大": text jumps to 125%, bright, no sweep yet.
03 caption "亮带进入": KEEP 125% size; two closely spaced parallel diagonal glass-reflection highlight bands enter the LEFT of H.
04 caption "扫过中间": KEEP 125% size; those SAME two parallel bands have translated horizontally across the MIDDLE of the word.
05 caption "扫至右侧": KEEP 125% size; the same bands reach T and the exclamation mark at the RIGHT edge.
06 caption "亮带扫完": KEEP 125% size; bands have completely exited and are now absent, restoring the base letter colors.
07 caption "轻微缩小": ONLY NOW shrink to 110%, no shine bands, still legible.
08 caption "缓慢淡出": keep 110% size and same center; fade entire word and outline to about 18% opacity into the dark background, no shine bands.
Shine is glass LIGHT REFLECTION, not broken glass: two parallel diagonal strips of white glint with a stepped pale highlight edge, clipped INSIDE the letter silhouettes, scanning left to right. No long external beams crossing the whole panel, no glass fragments.
Text style: authentic deliberate pixel typography, straight blocky strokes and visible square stepped diagonal edges with a consistent pixel grid; clearly italic, NOT cursive, NOT smooth vector, NOT fuzzy. Crisp enlarged nearest-neighbor-like pixels. Letterform identity stays the same across all eight cells, no spelling changes. Typography only, no enclosing badge, no hearts in keyframe cells, no explosion or particles.
Do not add shake, drifting, bounce cycles, glyph distortion, independent letter movement, lens blur, large radiating stars, or large comic speech bubbles. Do not shrink during the sweep. Minimal Chinese captions clean and easy to read. No extra explanatory paragraphs. This is a still storyboard showing scale, sequential reflection positions, settling, and fade.
Variant title, verbatim: "C · 橙金街机".
Variant styling: C: chunky strongly right-slanted arcade pixel lettering, gold-yellow upper face stepping into saturated orange lower rows; burnt-orange thin bevel and compact dark maroon two-pixel offset shadow. Powerful but still very compact and readable. White-gold parallel glint bands; no additional bursts or particles.
```

## C 配色定向修正提示词

```text
Use case: precise-object-edit.
Image 1 is the edit target: C HUD HIT! keyframe concept sheet.
Change ONLY the color/material of EVERY "HIT!" word: the HUD preview at top plus all eight keyframe cells. Currently these words are incorrectly BLUE; recolor them to the orange-gold arcade palette matching the existing orange-gold title "C · 橙金街机".
All HIT! face fills must be golden yellow on the upper rows and saturated orange on the lower rows, with a thin burnt-orange bevel, compact dark maroon/brown pixel offset shadow. Absolutely no blue or cyan in any HIT! word. Keep white-gold shine bands in frames 03,04,05, preserving their respective left, middle, right positions. Frame 08 stays faded with the new orange-gold colors at low opacity.
Preserve EVERYTHING ELSE: the title, 4-by-2 grid, all captions and numbers, letter silhouettes, italic pixel edges, scale changes, center anchoring, blue airplane and BLUE HEARTS (these MUST STAY BLUE), red airplane and red hearts, top timer and latency, dark backgrounds, margins, exact text "HIT!".
Do not recolor the player's blue plane or blue hearts. Keep shooter-side HIT! between blue hearts and central display. Do not shrink until after frame06. This is a targeted palette correction, not a redesign.
```

## C 字间距定向修正提示词

```text
Use case: precise-object-edit.
Image 1 is the approved orange-gold arcade "C · 橙金街机" HIT! keyframe sheet. Make ONE very small typography change only: slightly tighten the spacing BETWEEN H, I, T and ! in EVERY HIT! instance (the top HUD preview and all eight keyframes).
Reduce the empty inter-character gaps by approximately 20–25% so "HIT!" feels a little more cohesive, especially the gap between T and !. This is a subtle tracking adjustment, NOT a redesign and NOT a dramatic condensation. Keep a clearly visible dark gap between every glyph; no characters may touch. Keep each individual glyph's width, height, stroke thickness, rightward italic angle, stepped pixel silhouette, orange-gold material, bevel and shadow exactly the same. Only move whole glyphs horizontally closer to one another; keep the resulting word centered on its previous center anchor.
Preserve the approved orange/gold palette (no blue text), crisp pixel style, all eight keyframe sizes and timeline order, pop enlargement, and left/middle/right parallel white-gold glass highlights in 03/04/05. The shine positions remain logically relative to the tightened word: frame03 at H, frame04 at I/middle, frame05 at !/right, frame06 fully finished with no shine; frame07 only then settles slightly smaller; frame08 fades. Preserve the exact fade opacity.
Do not change the title, layout, frames, captions, numbering, HUD rectangle, blue/red aircraft icons, heart icons, timer "170s", latency "80ms", dark backgrounds, or any other image content. Keep HUD HIT! in its original position between the blue hearts and central timer. Do not stretch or compress the whole image or scale glyphs. Text remains exactly "HIT!".
Output the same full storyboard with only this subtle letterspacing adjustment.
```
