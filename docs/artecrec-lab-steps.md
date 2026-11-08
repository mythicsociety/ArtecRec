# From ArtecRec passes to a finished model — lab steps (8 Nov 2026)

ArtecRec in **Field mode** only records. Each pass is saved as it was scanned: not cleaned, not placed against the
other passes. All of that is done in the lab, in Artec Studio. Do the steps in this order.

1. **Open the project** in Studio. Check the Workspace lists all the passes (scans).
2. **Fine registration.** Tools → Registration → Fine registration. Keep "Geometry and texture", subsampling 1.
   Apply to all scans. About one minute per 1,000 frames.
3. **Align.** Align panel (left bar) → select all scans → **Auto-alignment**. This places the passes against each
   other. Without it, each pass sits where the scanner started it, often a metre away from the others.
   Texture alignment: off.
4. **Global registration.** Tools → Registration → Global registration, Geometry. Apply. A few minutes.
5. **Sharp fusion.** Tools → Fusion → Sharp fusion, resolution 0.3 mm. Hole filling by radius, 2 mm. 10–20 minutes.
6. **Save the project as** a new name (for example `Trail31_lab`), so the field version stays untouched.
7. **Fast mesh simplification.** Tools → Fast mesh simplification, target 3,000,000 triangles. About 7 minutes.
   Without it, texturing a 24-million-triangle fusion ran for hours; with it, 11 minutes (Trail31, 8 Nov).
8. **Texture.** Texture panel → model = the new fusion → source = all scans → destination Export →
   **Inpaint missing texture** ticked → 4096 to test, 8192 for the final. Apply.

## What the white squares were
Not holes in the stone. The scanner takes a photo only every few frames. Where two photos do not quite overlap, the
surface has no colour, and Studio draws it white. With texture switched off the surface is complete.
Inpainting (step 8) paints those patches from their surroundings.

## What the ledges were
Passes a few millimetres to a metre apart, because the field passes were never placed (step 3 missing).
Global registration alone cannot fix that; Auto-alignment can.

Checked on Trail30 (stone 3) and Trail31 (stone 4, full recipe to a clean textured model by 11:45), 8 Nov: `docs/artecrec-field-readiness.md`.
