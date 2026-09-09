# 破损涂装三版预览（2026-09-08）

本次仅生成选择用预览，没有替换运行资源、编译 EXE、修改游戏代码或改动碰撞体积。

## 最终选择图

- A-scorch.png：焦黑灼痕，受损部位逐渐焦黑。
- B-chipped-armor.png：金属掉漆，使用最终收回白边以内的修正版。
- C-impact-scars.png：弹痕与裂纹，内部装甲出现撞击痕迹。

各图均为上排蓝飞机、下排红飞机；从左到右为 3 HP / 2 HP / 1 HP。3 HP 作为视觉参照，2/1 HP 仅设计内部表面损伤，不设计缺翼、缺尾或外扩结构。预览中不叠加烟雾、拖尾、命中特效。

## 原资源保护与生产限制

原图输入是 ../../pet-blue-d2.png 和 ../../pet-red-d2.png，均已在生成前通过 view_image 检查。

生成后再次核对，原始贴图和碰撞代码 SHA256 未变：
- 蓝：B5E6B719B29E6C343B08428BB1224AB38DA5145E2F4EC9371708F19A197D009C
- 红：E50B296C5820D7D85925759C54E8CEC312FE3911FA0EFCDB30E5C70841C15A91
- common/pc_battle.h：5283BFA2CC35E7EE42A59143A3CBA9CA042FA1BBC373FCD90536B937E86F0B5E

已人工读图检查，没有以机翼或尾部缺失来表现受损。B 首版掉漆触及白色边线，已通过内置 imagegen 再编辑收回内部。

注意：这些是 AI 生成的配色/损伤风格预览板，不是已完成逐像素轮廓注册的运行 atlas。不能仅凭生成提示词宣称板中每架飞机的 alpha、比例与原 512×512 资源逐像素相同。用户选定后，正式接入应保留原模型、白色轮廓和透明度掩膜，仅把损伤内容约束在机身内部；再验证原始外轮廓、显示大小、碰撞几何和游戏内可读性一致。当前真实程序的上述内容未发生变化。

## 生成方式与实际提示词

使用 imagegen 技能的内置 image_gen 工具模式；未使用 CLI/API 回退，未用程序重画或处理预览中的飞机。三版分别调用一次，B 另调用一次做单项修正。下文保留完整提示词。

### A 首次生成

```text
Use case: precise-object-edit.
Asset type: pixel-art game aircraft damage-livery comparison board; preview for a real existing game, NOT a new aircraft design.
Input images: Image 1 is the EXACT existing BLUE aircraft edit target; Image 2 is the EXACT existing RED aircraft edit target. Both have the same D2 jet structure, top-down nose pointing up, white pixel outline, compact fuselage, swept main wings, small tail fins and orange exhaust. The input transparent backdrop may display black.
Primary request: make one 1536x1024 landscape comparison board for the livery variant specified below. Exactly SIX aircraft: top row BLUE, bottom row RED; three columns labeled "3 HP", "2 HP", "1 HP". The 3 HP column is the unchanged original aircraft for direct comparison. The 2 HP and 1 HP columns repaint the SAME aircraft interior with mild and severe damage, respectively.
CRITICAL SHAPE LOCK: preserve the input aircraft's exact outer silhouette, white contour, nose, canopy position, wing spans and wing angles, tail outline, exhaust silhouette, proportions, footprint, orientation, size and registration. Clone the same silhouette for every cell. Do not reshape any airplane, cut off a wing, bend a wing, open a transparent hole, remove fuselage pixels, create debris, enlarge the plane, add attachments or alter the white outer rim. This must represent surface paint damage ONLY, not structural geometry damage. Leave cockpit/canopy readable. Keep the outside white border fully intact even at 1 HP. Preserve original orange exhaust identically at all health levels.
Blue and red versions MUST use the same damage pattern at corresponding locations, with only the underlying team livery color changing. All six aircraft same scale. Damage occupies only a few large readable interior pixel clusters so it is legible at the actual 36px game sprite size; no dense tiny noise, realistic texture, cracks in the background, smooth vector redraw or 3D lighting. Preserve the original chunky pixel grid and highlights. 1 HP is an intensified superset of 2 HP damage at the same locations, not a different random pattern. Keep enough original blue/red paint that team identity is unmistakable.
Composition: plain solid near-black navy #070A13 background, neatly aligned spacious 3x2 board, clean white pixel-font column labels, short variant title at top; no stars, smoke, trails, explosion, motion lines, hit text, hearts or HUD. Do not include annotations claiming collision verification. No watermark. All noses, wingtips, tails and exhaust fully visible with clear margins.

Title text (verbatim): "A / SCORCH".
Variant instructions: A — SCORCH: broad soft-edged but crisply pixel-stepped charcoal soot and burn discoloration on the inner wing roots and lower fuselage around the engine housing. 2 HP: two small asymmetric dark gray soot patches, most saturated paint remains clean. 1 HP: those same patches expand and darken, with restrained gray ash highlight pixels and heat-dulled team paint, approximately 20 percent body surface so original color remains dominant. No silver paint-chipping focus, no crack network. Smoky painted damage but NO emitted smoke.
```

### B 首次生成

```text
Use case: precise-object-edit.
Asset type: pixel-art game aircraft damage-livery comparison board; preview for a real existing game, NOT a new aircraft design.
Input images: Image 1 is the EXACT existing BLUE aircraft edit target; Image 2 is the EXACT existing RED aircraft edit target. Both have the same D2 jet structure, top-down nose pointing up, white pixel outline, compact fuselage, swept main wings, small tail fins and orange exhaust. The input transparent backdrop may display black.
Primary request: make one 1536x1024 landscape comparison board for the livery variant specified below. Exactly SIX aircraft: top row BLUE, bottom row RED; three columns labeled "3 HP", "2 HP", "1 HP". The 3 HP column is the unchanged original aircraft for direct comparison. The 2 HP and 1 HP columns repaint the SAME aircraft interior with mild and severe damage, respectively.
CRITICAL SHAPE LOCK: preserve the input aircraft's exact outer silhouette, white contour, nose, canopy position, wing spans and wing angles, tail outline, exhaust silhouette, proportions, footprint, orientation, size and registration. Clone the same silhouette for every cell. Do not reshape any airplane, cut off a wing, bend a wing, open a transparent hole, remove fuselage pixels, create debris, enlarge the plane, add attachments or alter the white outer rim. This must represent surface paint damage ONLY, not structural geometry damage. Leave cockpit/canopy readable. Keep the outside white border fully intact even at 1 HP. Preserve original orange exhaust identically at all health levels.
Blue and red versions MUST use the same damage pattern at corresponding locations, with only the underlying team livery color changing. All six aircraft same scale. Damage occupies only a few large readable interior pixel clusters so it is legible at the actual 36px game sprite size; no dense tiny noise, realistic texture, cracks in the background, smooth vector redraw or 3D lighting. Preserve the original chunky pixel grid and highlights. 1 HP is an intensified superset of 2 HP damage at the same locations, not a different random pattern. Keep enough original blue/red paint that team identity is unmistakable.
Composition: plain solid near-black navy #070A13 background, neatly aligned spacious 3x2 board, clean white pixel-font column labels, short variant title at top; no stars, smoke, trails, explosion, motion lines, hit text, hearts or HUD. Do not include annotations claiming collision verification. No watermark. All noses, wingtips, tails and exhaust fully visible with clear margins.

Title text (verbatim): "B / CHIPPED ARMOR".
Variant instructions: B — CHIPPED ARMOR: chipped colored paint reveals flat opaque steel-gray armor inside the intact silhouette. 2 HP: two clear angular gray scuffed paint patches and one short scratch on the fuselage/inner wing panels. 1 HP: those same chipped-paint patches expand, adding a few broader angular metal abrasions and dark metal grooves, around 20 percent body surface. Distinguish this style from soot: readable lighter silver-gray metal against blue/red. The actual armor remains fully intact and opaque; absolutely no missing wing edges, detached pieces, holes, protrusions or exposed mechanical greebles.
```

### C 首次生成

```text
Use case: precise-object-edit.
Asset type: pixel-art game aircraft damage-livery comparison board; preview for a real existing game, NOT a new aircraft design.
Input images: Image 1 is the EXACT existing BLUE aircraft edit target; Image 2 is the EXACT existing RED aircraft edit target. Both have the same D2 jet structure, top-down nose pointing up, white pixel outline, compact fuselage, swept main wings, small tail fins and orange exhaust. The input transparent backdrop may display black.
Primary request: make one 1536x1024 landscape comparison board for the livery variant specified below. Exactly SIX aircraft: top row BLUE, bottom row RED; three columns labeled "3 HP", "2 HP", "1 HP". The 3 HP column is the unchanged original aircraft for direct comparison. The 2 HP and 1 HP columns repaint the SAME aircraft interior with mild and severe damage, respectively.
CRITICAL SHAPE LOCK: preserve the input aircraft's exact outer silhouette, white contour, nose, canopy position, wing spans and wing angles, tail outline, exhaust silhouette, proportions, footprint, orientation, size and registration. Clone the same silhouette for every cell. Do not reshape any airplane, cut off a wing, bend a wing, open a transparent hole, remove fuselage pixels, create debris, enlarge the plane, add attachments or alter the white outer rim. This must represent surface paint damage ONLY, not structural geometry damage. Leave cockpit/canopy readable. Keep the outside white border fully intact even at 1 HP. Preserve original orange exhaust identically at all health levels.
Blue and red versions MUST use the same damage pattern at corresponding locations, with only the underlying team livery color changing. All six aircraft same scale. Damage occupies only a few large readable interior pixel clusters so it is legible at the actual 36px game sprite size; no dense tiny noise, realistic texture, cracks in the background, smooth vector redraw or 3D lighting. Preserve the original chunky pixel grid and highlights. 1 HP is an intensified superset of 2 HP damage at the same locations, not a different random pattern. Keep enough original blue/red paint that team identity is unmistakable.
Composition: plain solid near-black navy #070A13 background, neatly aligned spacious 3x2 board, clean white pixel-font column labels, short variant title at top; no stars, smoke, trails, explosion, motion lines, hit text, hearts or HUD. Do not include annotations claiming collision verification. No watermark. All noses, wingtips, tails and exhaust fully visible with clear margins.

Title text (verbatim): "C / IMPACT SCARS".
Variant instructions: C — IMPACT SCARS: a few readable dark impact dents with short angular pixel cracks confined entirely inside painted armor panels. 2 HP: one compact charcoal impact mark on the lower central fuselage and short stress cracks on one wing root. 1 HP: these same impact scars enlarge and gain a few more branching dark cracks plus restrained surrounding soot, about 20 percent body surface. Add only a few steel-gray highlight pixels to make the dents readable as opaque indented metal rather than transparent holes. Keep all large colored panels recognizable. No glowing lava cracks, orange sparks, peeled-up plates, missing structures or damage outside the original white perimeter.
```

### B 最终边线修正

```text
Use case: precise-object-edit.
Input image: the B / CHIPPED ARMOR aircraft comparison board is the EDIT TARGET.
Change ONLY the locations and edge treatment of the gray chipped-paint marks on the 2 HP and 1 HP aircraft, in both blue and red rows. All gray marks must be INSIDE the colored painted body panels. Move wingtip damage inward toward each wing root and reduce it as needed, leaving a continuous untouched WHITE outer perimeter and at least one original colored logical pixel between each gray patch and the white outline. The gray damage must NEVER cross onto the white outline or outside the original aircraft, NEVER create a hole, ragged external edge, missing part or silhouette change. Use broad clean angular pixel clusters, no tiny fuzzy stipple. Keep the same pattern matched between blue and red; 1 HP has more exposed metal than 2 HP at the same locations.
Preserve EVERYTHING ELSE unchanged: exact six aircraft positions, scale, geometry, white outlines, wing tips, nose, tails, canopy, undamaged 3 HP column, original team colors and pixel-art style, exhaust, board background, title "B / CHIPPED ARMOR", and "3 HP", "2 HP", "1 HP" labels.
Do not redesign, rotate, resize or move any aircraft. Do not add smoke, trails, flames beyond existing exhaust, debris, new text, or transparency holes. This is a small corrective surface-paint edit for silhouette-preserving damaged liveries.
```
