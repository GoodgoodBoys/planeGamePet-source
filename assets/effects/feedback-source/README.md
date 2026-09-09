# Approved combat-feedback inputs

`damage-c-approved.png` preserves the user-approved C / Impact Scars sheet supplied on 2026-09-08. `damage-c-import-check.png` is the native runtime model comparison sheet produced during import.

Run `../prepare_damage_liveries.ps1` from Windows PowerShell to reproduce the four runtime liveries. It maps the approved healthy/damaged hull regions to the existing D2 models and transfers only the dark scar differences onto saturated interior paint. Original alpha, white border/highlights and exhaust are protected. The source D2 textures are never overwritten. Real renderer acceptance independently compares every protected pixel in the embedded liveries against those originals.

`../hit-word-c.png` is copied from the approved `assets/vfx-concepts/2026-09-08-impact-animations-v1/hit-master.png`. Its pop, clipped twin shine, settle and fade are generated in the client at native HUD pixel size.

Heart animation uses the existing A-group heart atlas. A fixed complementary jagged partition produces the two halves; later frames translate and fade those same pixels without redrawing or flattening the split edge. The fixed gray slot is rendered underneath. Timing and event ownership are in `desktop/combat_feedback.h`; the stage-05 hold lasts 0.30 seconds.
