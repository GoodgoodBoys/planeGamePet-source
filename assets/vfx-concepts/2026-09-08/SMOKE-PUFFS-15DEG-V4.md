# 受损烟雾 v4：后向 ±15° 烟雾团示意

本次按用户反馈将方向收敛为后向左右各约 15°（总夹角约 30°），恢复有团块体积的灰色像素烟雾，不再采用宽扇形烟絮。上排 2 血少且小，下排 1 血多且大，左蓝右红。形状与间距有变化。

使用内置 image_gen 生成静态预览，无 CLI 回退。新增 damage-smoke-puffs-15deg-v4.png 与本说明；没有修改程序、原素材、服务器或已满意的拖尾和爆炸。此前各版保留不覆盖。

图片仅用于确认形状和收拢程度，不能证明实际发射频率或精确角度。后续动画拟对每次发射单独取 [-15°,15°] 随机后向偏角，2 血使用更低发射频率与更小初始尺寸，1 血提升这两项；烟雾独立漂移、随年龄扩张淡出。非整条烟柱统一摆动。精确时序、角度边界、淡出曲线和游戏中的可见度需在确认后实现并验证。

## 最终提示词

Use case: stylized-concept.
Asset type: revised pixel-art airplane DAMAGE SMOKE concept board, ONE finalized visual direction with TWO health states, preview only.
Reference image 1: exact existing BLUE aircraft. Reference image 2: exact existing RED aircraft. Preserve the same matching silhouettes, top-down upward-facing pose, wing geometry, white outline, cockpit details, original team colors and tiny orange rear engine flames. Plane body brightness/color must be identical at 2 HP and 1 HP.

Primary request: COMPACT ROUNDED GRAY SMOKE CLOUD PUFFS emitted randomly within ONLY plus/minus FIFTEEN DEGREES of STRAIGHT BACKWARDS. Not broad spraying, not torn strings of smoke, not wispy fragments. The user has now selected rounded smoke CLOUD CLUMPS, while still requiring natural variation and a narrow randomized spread.

Output: one square clean 2-column by 2-row board on solid dark navy #090d16. BLUE plane in left column and RED plane in right column. Upper row "2 HP", lower row "1 HP" shown in large readable white type at far left. A small clear centered top heading reading exactly "+/-15°". No A/B/C variants. Exactly four aircraft, all same size and brightness, each fully visible with its smoke beneath it and generous margins. Each plane about 170 pixels long in a 1024 square board; thin colored wing trails and plume have ample space in its panel. Nose points UP, back points DOWN.

NARROW-CONE GEOMETRY IS THE MOST IMPORTANT CORRECTION:
All smoke births originate at the SINGLE central engine outlet on the very back of the fuselage, just behind the tiny orange exhaust flame.
At any rearward/downward distance d from that origin, each puff CENTER's lateral displacement x must satisfy |x| <= 0.268*d. This is +/-15 degrees, a TOTAL spread of only 30 degrees, NOT +/-30, 45 or 60.
Most puff centers closer to the centerline, with a few reaching the +/-15 degree limit. Use independently random small left/right offsets, not fixed alternation or mirror symmetry. Do not create two diagonal trails or a wide V.
Keep the visible plume compact; even the farthest expanded smoke plus its radius is approximately no wider than one aircraft wingspan. Newly emitted smoke very close to center; older puffs slightly displaced but still recognizably directly behind plane. Do not draw cone boundary lines, arrows, construction geometry or angle wedges.

SMOKE SHAPE CORRECTION:
Draw puffy rounded gray CLOUD CLUMPS formed from several joined unequal rounded lobes with crisp stepped PIXEL edges, 2-3 restrained gray shades and softer-looking stepped-opacity fading.
They should be immediately recognizable as small billowing smoke clouds, not spheres, bubbles, rocks, plus signs, sharp ragged strings, fire, or a chain of identical beads.
Vary each clump's proportions, lobes, initial size and shading. Some wider/flatter, some taller, some asymmetric, but all remain coherent rounded smoke clumps. No black outlines, no spherical specular highlights, no white cloud colors.
Spacing is irregular, NOT perfectly even. Some puffs partially overlap naturally in heavy damage, with irregular empty gaps.
Each older puff expands somewhat and becomes fainter until dissolving into navy. Furthest puffs are the LOWEST CONTRAST, not the brightest, no dense black soot. Source puffs small medium-gray; older puffs somewhat larger translucent dim gray. Crisp pixel-art contours, no Gaussian blur.

HEALTH DIFFERENCES:
2 HP / top row: low emission rate and SMALL puffs. Show only about 3 distinguishable small cloud clumps plus perhaps one nearly invisible old remnant. Large irregular gaps, visibly sparse. Small footprint.
1 HP / bottom row: approximately 2.5 times the emission rate and 1.5 times initial puff diameter compared with 2 HP. Show about 7-9 varied rounded cloud clumps, several gently overlapping, visibly denser and larger but still compact and not hiding aircraft. The rearward angle is the SAME +/-15 degrees; do not widen cone for 1 HP. Increased smoke volume comes from frequency and size, NOT a wider fan.
Smoke remains GRAY for both blue and red aircraft.

PRESERVE TEAM WING TRAILS AS CONTEXT:
Two fine downward trails from the left and right main wing OUTER REAR corners, independent of the center exhaust smoke, blue for blue plane, red for red plane. 2 HP trails normal brightness fading to background, 1 HP trails faint around 35% brightness. Keep plane bodies fully bright at both health states. Trails thin and non-dominant; do not change them into flame jets. Keep tiny original orange engine flames unchanged.
No explosions, scenery, HUD, hearts, extra labels, background gradients, glow, realism, anti-aliased vector illustration or cartoon outlines. Magnified low-resolution game-art concept, with readable soft-looking pixel cloud masses.
