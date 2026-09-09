# B 风格烟雾细化 · v5

按用户提供的参考图中 B 列，仅细化已有 ±15° 烟雾示意图中的灰色烟团：多层重叠团块、内部明暗、细致像素边缘。保持 2 血较少较小、1 血较多较大、后向窄角度随机偏移的设计。未采用参考图的整齐直线排布作为运动规则。

采用内置 image_gen 图像编辑模式，无 CLI 回退。新增 damage-smoke-b-style-15deg-v5.png 与本说明，未改程序、服务器或已有运行资源，前几版保留。仅静态预览，精确角度和频率仍待确认后制作动画并验证。

编辑目标：damage-smoke-puffs-15deg-v4.png。
样式参考：用户附件 codex-clipboard-7ab32446-24c3-468e-9697-b8124fc4bd44.png 的 B 列。

## 最终提示词

Use case: precise-object-edit.
Asset type: refinement of an existing pixel-art airplane smoke concept sheet.

INPUT ROLES:
Image 1 is the EDIT TARGET: square sheet labeled "+/-15°", two columns of blue/red planes, top 2 HP and bottom 1 HP.
Image 2 is a STYLE REFERENCE ONLY. Use ONLY the middle column labeled B for the smoke artwork. Ignore its A and C columns and ignore its straight-line particle arrangement.

REQUEST:
Change ONLY the GRAY SMOKE CLOUD DRAWINGS in image 1 to the detailed layered pixel-smoke style in column B of image 2. The current smoke in image 1 is too flat and simple. Repaint the individual smoke clumps with the SAME rich, billowing, multilobed construction as the B reference:
- Each cloud is an irregular collection of overlapping rounded lobes, not a flat polygon, sphere, disk or cotton ball.
- Use several deliberate shades of neutral/warm gray: charcoal recessed pockets, medium-gray body, a few lighter raised lobe highlights, stepped soft-looking edges.
- Visible interior folds, recesses, and layered puffs create volume. Pixel clusters are sufficiently fine to show these details at the existing cloud size. Do not just enlarge the coarse square pixels.
- Maintain recognizable cohesive cloud masses, not shredded strands, random grain or rock textures. Clouds should look gently turbulent, varied, and airy.
- Slightly irregular lobe silhouettes and different arrangements between puffs; do not copy one identical cloud repeatedly.
- Match B's restrained muted gray palette. No shiny spherical highlights, white clouds, hard black outlines, Gaussian blur, painterly rendering or glow.
- Older distant puffs retain their lower opacity and dissolve into the navy background; don't make the farthest puffs opaque or brighter than new puffs.

STRICT INVARIANTS:
Keep image 1's canvas, background, typography, "+/-15°", "2 HP", "1 HP", all four airplanes, exact positions, scale, orientation, original blue/red colors and body brightness, orange flames and colored wing trails unchanged. Do not redesign the aircraft or add new effects.
Keep all smoke clump centers, approximate bounding sizes, relative spacing and densities from image 1. They must retain the narrowed rearward +/-15 degree spread and irregular lateral offsets. Do NOT copy the vertical evenly spaced smoke rows from image 2. Smoke comes from the single central rear engine outlet only.
Top row 2 HP stays sparse and small. Bottom row 1 HP stays markedly denser and larger with varied gently overlapping clumps; same narrow +/-15 degree angular range. Do not add smoke in front of aircraft, do not broaden plume, do not tint smoke red or blue.
The sole material change is adding the detailed layered B-style smoke-cloud artwork to the already-approved narrow-spread composition. No extra labels, panels, frames, clouds, arrows or embellishments.
