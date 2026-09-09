# 飞机拖尾、受损烟雾与战败爆炸：ABC 示意图

本次为用户选型预览，仅新增此目录下的示意图与制作记录；未修改应用代码、服务器或已有运行素材。绘图使用内置 image_gen，无 CLI 回退。图为深色背景放大示意，并非可直接使用的透明动画素材。

## 文件与读图

- wing-trails-abc-v1.png：满血拖尾，A 细直线、B 弧形细带、C 断续像素线；上蓝下红。
- damage-smoke-abc-v2.png：A 简洁烟团、B 分层烟团、C 破碎烟絮；上排 2 HP，下排 1 HP。1 HP 拖尾更淡，烟团更多更大。
- defeat-explosion-abc-v1.png：每行一种方案，每行从左向右为起爆、扩张、机体消失、碎片/烟尘扩散、消散。A 火花爆散，B 火焰与烟团，C 机体碎片。

## 选型与尚未实施的边界

三类效果可分别选择 A/B/C，不必整套一致。先将战败爆炸理解为血量归零的飞机爆炸；未将平局、退出或断线直接定义为机体毁灭，最终接入前需明确这些结束方式的表现。

生成图用于选型，并不保证所有像素和源飞机一一对应；尤其烟雾图重伤排的机身也略偏暗，正式接入时应继续使用原飞机素材，只改变拖尾和烟雾。正式动画须保持烟雾从中心发动机后端产生、随时间扩张并淡出，不改变碰撞体积或遮蔽子弹。

## 提示词

### 拖尾

Use case: stylized-concept.
Asset type: pixel-art game VFX design comparison board, THREE options A/B/C for WING TRAILS at full 3 HP.
Reference image 1: exact existing BLUE aircraft sprite. Reference image 2: exact existing RED aircraft sprite. Preserve their IDENTICAL delta-wing silhouettes, proportions, thin pale outlines, cockpit, pixel grid and team colors; do not invent a new aircraft.
Output: clean 3-column by 2-row comparison board on solid deep navy #090d16, landscape roughly 3:2. Big clear column headings "A", "B", "C"; small readable top title "3 HP / WING TRAILS". Only this text. Each column has one upward-pointing BLUE plane in the upper row and an upward-pointing RED plane in the lower row. Six planes in total, equal size and pose, straight top-down view. Each cell shows a complete plane and full-length trails with ample empty margins.
CRITICAL anatomy: TWO narrow colored trails, one originating EXACTLY from the outer REAR CORNER of the left MAIN WING and one from the outer REAR CORNER of the right MAIN WING. Trails extend straight BACKWARDS/downward, well to either side of the central tail/exhaust. They do not start at the nose, front wing edge, center engine, or tiny tail fins. Keep the original small orange engine flame between the two trails. No gray smoke at 3 HP.
Blue airplane: two BLUE/CYAN trails. Red airplane: two RED/CORAL trails. Near the wings the color is saturated, gradually becoming fainter and blending into the background farther behind. Pixel-stepped color/opacity falloff, not blurred glow.
A: minimal thin continuous twin lines, 1 logical pixel thick, taper smoothly and fade across about 1.0 aircraft body length behind the main wing rear corners. Restrained.
B: fine twin ribbons with a 2-logical-pixel bright start and 1-pixel tip, slight synchronized graceful bend as if leaving a curved flight path; about 1.3 body lengths, brightest near aircraft and faint at far end. Still narrow lines, never flame jets.
C: shorter twin pixel streak lines, mostly continuous at origin, ending in a few very fine separated rectangular dashes, about 0.8 body lengths, distinctly retro low-density.
The only differences among columns are these trail treatments. Keep planes SAME scale, shape, flight direction, same orange central exhaust in every cell. Crisp deliberate square pixel-art edges; no gaussian blur, no bloom, no realistic fog, no grids, no shadows, no HUD, no labels besides specified. This is a magnified concept board, not a gameplay screenshot. Do not crowd or let one cell's trails overlap another cell.

### 烟雾初稿

Use case: stylized-concept.
Asset type: pixel-art game VFX comparison board, THREE options A/B/C for damage SMOKE with 2 HP and 1 HP.
Reference image 1: existing BLUE airplane; reference image 2: existing RED airplane. Strictly preserve the identical delta-wing aircraft silhouettes, cockpit, pale outlines, pixel size, team colors and original compact orange central engine flame. Do not redesign aircraft.
Output: wide 3-column by 2-row comparison on uniform deep navy #090d16, roughly 3:2. Top column headings "A", "B", "C". Far left row labels "2 HP" and "1 HP", large clean readable sans-serif. No other text.
In EVERY cell put two small upward-facing aircraft SIDE BY SIDE, BLUE on left and RED on right, same size across the entire board. Twelve aircraft total. Enough space for smoke trails underneath each aircraft. No overlaps with adjacent cells, all aircraft and smoke fully inside their cells.
All aircraft retain TWO very fine team-color trails from the outer rear corners of their two MAIN WINGS, directed downward/back. Keep wing trails laterally separated from the central engine smoke. At 2 HP the trails retain normal saturation. At 1 HP the same blue/red trails are distinctly fainter (roughly half opacity), but remain visible. Never change plane body brightness or colors according to health.
SMOKE ORIGIN: ONLY the SINGLE centerline rear engine exhaust at the very back of the fuselage. Smoke flows BACKWARD/downward behind the plane, begins as a small medium gray puff close to the engine, each older puff becomes LARGER and progressively fainter until fully dissolved into navy. Do not put smoke in front of plane, at wingtips, around cockpit or enclosing whole plane. Use pixel-art block-edged rounded CLOUD CLUSTERS, not perfectly geometric circles, not opaque black balls, not realistic soft blur.
Top row 2 HP: lightly damaged; about three to four small sparse puffs, newest puff small/darker gray, oldest larger/lighter low opacity.
Bottom row 1 HP: heavily damaged; retain same type of smoke, roughly twice as many puffs and up to 1.5x larger older puffs, denser near engine, expanding then fading backward. Visually clearly heavier than corresponding top cell, but never obscure aircraft or wing trails. No new fire on cockpit or wings.
A: restrained compact gray puffs, clean silhouettes, only two gray shades, airy sparse gaps.
B: somewhat fuller layered gray cloud puffs, three restrained gray shades, pixel-cluster rounded edges and softer-looking stepped-opacity fading.
C: wispy small clustered smoke packets stretching backward, broken/dithered pixel fringes as the old puffs dissolve; denser near origin at 1 HP, still expanding puffs rather than mere lines.
All A/B/C choices MUST have the same health rules, same aircraft and wing-trail source positions. Distinguish puff construction without changing gameplay state. Prioritize clarity at tiny game scale and restrained smoke footprint; pixel art only. No environment, no hearts, no arrows, no orange smoke, no extra text, no glow or cinematic lighting.

### 烟雾针对性修订

Use case: precise-object-edit. Edit the supplied 3-column A/B/C, 2-row 2 HP / 1 HP pixel-art smoke comparison board. Preserve all twelve planes, their exact original colors, sizes, positions, silhouettes, A/B/C labels, HP labels, navy background, and the three different smoke construction styles. Only correct TWO visual behaviors:
1. In the entire bottom "1 HP" row, substantially REDUCE the brightness/opacity of the TWO COLORED WING TRAILS behind every airplane to about 35% of the current value. These lines must be visibly much dimmer than top row's bright blue/red trails, still just visible. This change applies ONLY to colored TRAIL LINES behind main wings, not the plane sprite, outlines, or orange exhaust. Upper "2 HP" row trails must stay bright and unchanged. Make this visual difference unmistakable.
2. All engine smoke trails should read as aging smoke which starts smaller near central exhaust, expands further back and FADES OUT. Preserve compact A puffs, layered B puffs and wispy C puffs. After the near-exhaust puffs, successive older puffs get gradually wider but also lower contrast / lower opacity into navy; final oldest puff should be almost invisible, not a bright solid cloud or a trail of tiny solid plus signs. The heavy 1 HP row remains clearly denser (roughly twice as many puffs) and bigger than the top 2 HP row. Keep clouds compact enough that both wing trails stay visible. No smoke over the plane, no shifting origins away from central rear engine.
Preserve pixel-art square edges without gaussian blur, bloom, gradients in the background, or new elements. Keep the overall layout and all text exactly unchanged. No other redesign.

### 战败爆炸

Use case: stylized-concept.
Asset type: THREE-OPTION PIXEL-ART AIRCRAFT DEFEAT EXPLOSION storyboard, for selection before integration.
Reference 1: existing BLUE plane sprite. Reference 2: existing RED plane sprite. Reference 3: previously approved SMALL BULLET HIT explosion sheet, COLOR/PIXEL STYLE reference only, NOT the size or composition to copy.
Create a clean wide roughly 3:2 board on solid dark navy #090d16. Exactly THREE HORIZONTAL ROWS, labeled only "A", "B", "C" at far left. Each row contains exactly FIVE equal-spaced storyboard stages, left to right. Label column headings only "01", "02", "03", "04", "05". No additional text. Large adequate empty margins; no gridlines. Each cell shares the same fixed explosion center so stages read as motion. Aircraft and fire use identical logical pixel size.
At stage 01 each row shows the SAME recognizable existing upward-facing BLUE aircraft, a tiny bright damage flash at its fuselage center; its shape must match reference 1. Reference 2 demonstrates red team version uses identical geometry, don't add a second plane. Subsequent stages depict destruction of this one aircraft at the same place. Aircraft MUST disappear by stage 03; no intact plane or ghost silhouette remaining in stages 03,04,05. This is a 0-HP defeat explosion, substantially more decisive than the small nonfatal bullet hit reference.
A: compact clean arcade burst. 01 flash on airplane; 02 bright ivory/yellow core with orange lobed blast covering central fuselage; 03 full compact orange-yellow burst, max diameter about 1.25 aircraft body lengths, aircraft gone; 04 separated orange pixel embers radiating out; 05 a few very faint burnt-orange squares almost dissolved. No smoke.
B: restrained fire-to-smoke. 01 flash on aircraft; 02 small white-hot ignition with orange jagged lobes; 03 a fuller orange burst plus a few medium gray pixel cloud lobes, max diameter about 1.4 aircraft body lengths, aircraft gone; 04 shrinking embers inside outward-expanding faint gray smoke clusters; 05 sparse dim gray remnants about to disappear. No mushroom cloud.
C: crisp fragment burst. 01 flash on airplane; 02 sharper star-shaped warm yellow-orange ignition; 03 orange sparks and a VERY SMALL number (4 to 6) of tiny blue/pale aircraft pixel fragments breaking outward, aircraft gone; 04 fragments farther apart and rapidly fading with tiny orange sparks; 05 barely visible isolated blue-gray pixel debris, nearly empty. Debris tiny abstract pixels, no whole detached wing models. Max footprint around 1.3 body lengths.
All three options belong to the same original low-resolution retro airplane game. Warm fire palette: white/cream center, yellow middle, orange edges. No bloom, blur, photoreal flames, shockwave rings, screen flashes, camera shake, text overlays, victory labels, huge explosions, scenery or UI. Crisp square-pixel contours. This is a storyboard preview of a one-shot event, not a looping explosion or full game redesign.
