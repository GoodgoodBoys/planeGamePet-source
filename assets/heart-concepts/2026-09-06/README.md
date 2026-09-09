# Pixel heart concept sheets

Generated with the built-in image_gen tool on 2026-09-06. These are visual candidates, not yet registered as runtime assets. The application source, EXE and existing hearts are unchanged.

- A-classic.png: classic pixel heart family, blue/red/gray.
- B-rounded.png: rounder shaded pixel heart family, blue/red/gray.
- C-flat-preview.png: cleaned flat-style concept preview. Contains a visible checkerboard backdrop; not a production transparent sprite.

After selection, align all three state silhouettes to one shared native-size pixel grid, prepare individual clean-alpha assets, and inspect at the actual display size before integration. Generated previews are not claimed to be exact 16/20/24-pixel grid exports or perfectly identical alpha masks.

## Original generation prompts

### A

Use case: stylized-concept. Asset type: raster pixel-art heart HP sprite sheet for a small Windows airplane game's history panel.
Create a brand-new horizontal sprite sheet containing EXACTLY THREE isolated hearts in one row: BLUE full-health heart on the left, RED full-health heart in the middle, GRAY depleted-heart slot on the right. These are three color/state versions of ONE design, with EXACTLY the same outer silhouette, pixel alignment, size and center. Genuinely transparent background (alpha), no checkerboard drawn into the image, no panels, no text, no labels, no watermark.
They must unmistakably be HEARTS: two distinct equal upper lobes with a deep V-shaped center notch and a single tapered bottom point. NOT diamonds, shields, arrowheads, gems, badges or medical anatomical hearts. Use strict uniformly sized square pixels, clean staircase edges, no antialiasing, no soft gradients, no glow, no blur, no perspective, no 3D extrusion, no horizontal seam through the middle.
Game palette: saturated cyan-blue approximately #00B4FF and coral-red approximately #FF465A. Gray depleted slot uses a dark charcoal interior #2A3140 and a visible muted gray outline #758197, SAME HEART SHAPE as the filled states, no cross or broken-heart slash.
Composition: three equal square cells, generous equal transparent padding between cells and around the canvas, centered in a single baseline. Hearts occupy roughly 65% of each cell. Enlarge the logical pixel art cleanly with nearest-neighbor appearance for inspection; must remain legible when later exported around 20-24 physical pixels high.
Variant A — CLASSIC 8-BIT. Design on a 16 by 16 logical grid. Compact broad balanced silhouette, distinct stepped rounded lobes, a 2-pixel-deep central notch. One logical pixel dark navy outer border, largely flat colored body, one tiny 2-pixel light accent near the upper-left lobe. Maximum 4 flat colors per active sprite. Simple, immediately readable, authentic retro game HUD.

### B

Use case: stylized-concept. Asset type: raster pixel-art heart HP sprite sheet for a small Windows airplane game's history panel.
Create a brand-new horizontal sprite sheet containing EXACTLY THREE isolated hearts in one row: BLUE full-health heart on the left, RED full-health heart in the middle, GRAY depleted-heart slot on the right. These are three color/state versions of ONE design, with EXACTLY the same outer silhouette, pixel alignment, size and center. Genuinely transparent background (alpha), no checkerboard drawn into the image, no panels, no text, no labels, no watermark.
They must unmistakably be HEARTS: two distinct equal upper lobes with a deep V-shaped center notch and a single tapered bottom point. NOT diamonds, shields, arrowheads, gems, badges or medical anatomical hearts. Use strict uniformly sized square pixels, clean staircase edges, no antialiasing, no soft gradients, no glow, no blur, no perspective, no 3D extrusion, no horizontal seam through the middle.
Game palette: saturated cyan-blue approximately #00B4FF and coral-red approximately #FF465A. Gray depleted slot uses a dark charcoal interior #2A3140 and a visible muted gray outline #758197, SAME HEART SHAPE as the filled states, no cross or broken-heart slash.
Composition: three equal square cells, generous equal transparent padding between cells and around the canvas, centered in a single baseline. Hearts occupy roughly 65% of each cell. Enlarge the logical pixel art cleanly with nearest-neighbor appearance for inspection; must remain legible when later exported around 20-24 physical pixels high.
Variant B — POLISHED 16-BIT. Design on a 24 by 24 logical grid. A slightly fuller, rounder heart silhouette, deeply separated soft stepped lobes, short tapered bottom, width approximately equal to height. One logical pixel dark outline, a controlled small ivory-tinted pixel highlight on the upper-left, saturated midtone and a single lower-right shadow band, 5 flat colors maximum. Charming but restrained, not candy photorealism; no gemstone facets, no shine spilling outside the heart. The gray version is a calm recessed empty slot without a white highlight.

### C

Use case: stylized-concept. Asset type: raster pixel-art heart HP sprite sheet for a small Windows airplane game's history panel.
Create a brand-new horizontal sprite sheet containing EXACTLY THREE isolated hearts in one row: BLUE full-health heart on the left, RED full-health heart in the middle, GRAY depleted-heart slot on the right. These are three color/state versions of ONE design, with EXACTLY the same outer silhouette, pixel alignment, size and center. Genuinely transparent background (alpha), no checkerboard drawn into the image, no panels, no text, no labels, no watermark.
They must unmistakably be HEARTS: two distinct equal upper lobes with a deep V-shaped center notch and a single tapered bottom point. NOT diamonds, shields, arrowheads, gems, badges or medical anatomical hearts. Use strict uniformly sized square pixels, clean staircase edges, no antialiasing, no soft gradients, no glow, no blur, no perspective, no 3D extrusion, no horizontal seam through the middle.
Game palette: saturated cyan-blue approximately #00B4FF and coral-red approximately #FF465A. Gray depleted slot uses a dark charcoal interior #2A3140 and a visible muted gray outline #758197, SAME HEART SHAPE as the filled states, no cross or broken-heart slash.
Composition: three equal square cells, generous equal transparent padding between cells and around the canvas, centered in a single baseline. Hearts occupy roughly 65% of each cell. Enlarge the logical pixel art cleanly with nearest-neighbor appearance for inspection; must remain legible when later exported around 20-24 physical pixels high.
Variant C — CLEAN FLAT PIXEL. Design on a 20 by 20 logical grid. A crisp, wider-than-tall symmetrical heart with strongly defined two stepped lobes and an unmistakable central notch. Thin light-tinted outline and a completely flat saturated inner fill, no gloss and no highlights, no outer black border. Refined, quiet, very high readability on a dark navy game-history list. The depleted slot preserves exactly that silhouette using a muted gray outline and dark gray fill.

## C edge-cleanup prompt

Use case: precise-object-edit. Input image is the EDIT TARGET: a horizontal PNG sheet of three pixel-art hearts, cyan-blue, coral-red, and gray depleted slot.
Change ONLY the faulty edge cleanup: remove ALL white specks, white halo fragments, colored stray pixels and irregular fuzzy roughness outside the intended square stepped heart outlines. Preserve the three heart positions, their sizes, equal silhouette, deep central notch, flat blue/red/charcoal fill, and the thin light-blue/light-pink/muted-gray outlines. Keep all three hearts exactly aligned. Keep a genuinely transparent alpha background.
Every silhouette edge must be a clean horizontal or vertical staircase made from a single coherent square pixel grid, not hand-cut or furry. No blur, antialiasing, feathering, glow, gradients, texture, extra highlighting, labels or additional hearts. The final three hearts should look like cleanly authored retro game HUD pixel sprites, not screenshots with a bad background removal. Preserve all intended light-colored outline pixels; remove only stray material outside the intended outline.
