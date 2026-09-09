# 随机后向受损烟雾 · v3 选型示意

用户要求：烟雾不要排成相同球体的队列；从发动机后方一定角度范围随机喷出。2 血低频、小烟；1 血更高频、更大烟。已有拖尾与爆炸方向不重做。

本次仅新增 damage-smoke-random-fan-abc-v3.png 和本说明，未修改任何程序逻辑、服务器、原飞机、拖尾或爆炸素材。采用内置 image_gen 绘制，未使用 CLI 回退。保持上一版图片，未覆盖。

## 读图

三列依次为 A 收敛、B 中等扩散、C 较宽扩散。上排 2 HP，下排 1 HP。本次统一使用蓝飞机放大展示，红飞机未来使用相同的灰色烟雾规则。为静态构图示意，不是动画，也不代表已验证真实发射频率、角度或运动。

## 拟采用的动态行为（待确认后实施）

- 每次发射独立随机选择向后偏角、初速、初始尺寸和烟团形状；不使用固定左右轮流、固定间距或随飞机一起移动的整串图案。
- 烟从发动机后端产生，向选定后向方向漂移，逐渐扩张并淡出；已产生的烟独立运动，飞机转弯时不突然跟随旋转或平移。
- 2 血较低发射频率、较小初始烟团；1 血提高发射频率与初始尺寸，保留同一后向角度规则，避免全屏遮挡。
- 1 血仅彩色拖尾变淡，实际机身颜色保持不变。
- 图中后向角度是风格示意，需在动画预览或接入时按最终选择核对。

## 最终提示词

Use case: stylized-concept.
Asset type: REVISED pixel-art aircraft DAMAGE SMOKE comparison sheet for user approval, not game integration.
Input image 1 is the exact original BLUE aircraft sprite, used as strict airplane identity and pixel-style reference. Preserve its blue color, white outline, cockpit, main wing and tail proportions, and small orange central exhaust. All six instances must be equally bright: do NOT dim the actual aircraft at 1 HP.

Primary request: show NATURAL IRREGULAR GRAY ENGINE SMOKE randomly emitted within a REARWARD angular fan, NOT a necklace of identical round balls aligned behind an airplane. Entirely rethink the old repeated ball approach.

Layout: landscape 3:2 board on uniform solid very dark navy #090d16. Exactly THREE columns labeled "A", "B", "C". Exactly TWO rows, left row labels "2 HP", "1 HP". ONE blue upward-facing aircraft per cell, six total, at consistent equal size. Keep generous space BELOW each aircraft for the smoking plume, with no inter-cell overlap. No other text or drawn guide lines. All planes in top-down view, nose points straight UP, the back and smoke go DOWN. Plane body may occupy about the top half of each cell, smoke the bottom half.

CRITICAL SMOKE PLACEMENT AND MORPHOLOGY:
- Emission origin is the SINGLE centerline engine exhaust immediately behind the rear of the fuselage, where the orange exhaust flame ends.
- Each separate emission travels at its OWN randomly chosen backward angle within a cone, so older smoke is offset unpredictably left or right, with different speeds and distances.
- A snapshot should show an ASYMMETRIC loose spray/plume, with some emissions near center, others off-center, not a mirror-symmetric V and not two neat streams. Keep the smoke generally behind the plane, no forward smoke.
- NO repeated spheres, beads, identical rounded puffs, equal spacing, regular rows, alternating zigzag sequence, neat symmetry, or circular outlines.
- Draw varied irregular smoke silhouettes: stretched ragged wisps, torn squashed cloudlets, slightly overlapping lopsided pixel clusters, some hollowed thin patches, each shape visibly different. Shape must look like drifting smoke, NOT gray rocks, plus signs, cotton balls, or stars.
- Newly emitted smoke is small medium-gray; it expands and is stretched/deformed in the airflow as it ages; older more distant smoke becomes larger but much fainter, edges break up and disappear into the dark background. Crisp square-pixel art with several restrained gray shades and stepped transparency illusion, no Gaussian blur or realistic rendering.
- Keep the plume small enough to preserve gameplay readability. Do not cover the aircraft, central cockpit, or wing origins.

HEALTH DIFFERENCE, ESSENTIAL:
Top 2 HP row: LOW emission frequency, SMALL initial smoke size. Show only 3 to 4 distinguishable irregular small gray wisps scattered at unequal positions and intervals in the rearward fan, with lots of clear negative space. They should be individually varied and include barely visible older expanded remnants, not all solid.
Bottom 1 HP row: substantially HIGHER emission frequency (roughly 2.5 to 3 times top row) AND LARGER smoke emissions (roughly 1.6 times top row diameter at equal age). About 8 to 11 unequal, irregular, partly overlapping wisps, forming a broken airy plume, not separate balls. Much stronger damaged appearance, with aging smoke growing and dissolving. Random offsets, irregular gaps, mixed ages, no uniformly filled triangle.
Keep the SAME rearward spread angle for 2 HP and 1 HP within a given column; health changes density and size, not direction.

THREE SPREAD VARIANTS:
A: restrained rearward cone, about +/-15 degrees. Mostly rearward but visibly varied lateral drift; do not align all smoke on axis.
B: natural moderate random fan, about +/-25 degrees. Clear organic side-to-side variability, stronger asymmetry and uneven negative spaces; not left/right alternating. Nice small-game balance.
C: loose random fan about +/-35 degrees, more dispersed wispy torn edges, still all backward and not an enormous wide cloud. Each variant should feel like a different random snapshot, not the same smoke scaled wider.

PRESERVE APPROVED NON-SMOKE EFFECT:
Behind each main wing's outer REAR CORNER, keep TWO extremely thin straight blue-to-transparent colored wing trails, one per wing, downward, clean and independent of engine smoke. Top row blue wing trails normal brightness, bottom row wing trails only about 35% brightness. Do NOT change plane body colors or brightness. Original orange exhaust tiny and unchanged at every health state. Smoke is gray, never blue, orange, or black soot balls.

Style: crisp deliberate low-resolution game pixel clusters with consistent pixel grid and scale, not painterly, no smooth vector outlines, no glow, bloom, lens effects, white backgrounds, extra aircraft, fireball, explosion, UI hearts, arrows, labels beyond A B C and 2 HP 1 HP. Magnified concept sheet emphasizing randomness of rear smoke.
