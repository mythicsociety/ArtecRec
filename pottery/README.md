# Pottery branch — sherd orientation for Sherd Studio

Sherd Studio (github.com/mythicsociety/pottery) needs the sherd mesh with the vessel axis vertical. Scans from Artec
Studio / ArtecRec are in scanner coordinates. This folder holds the tools that orient them.

## Rim click (semi-manual, the working tool)

    cd pottery
    python rimtool.py D:\Pottery\exports          # then open http://localhost:8765

One page, one piece at a time. Turn the sherd with the mouse, click **three points on the rim top**, as far apart as
possible. The tool shows the rim circle (red), the vessel axis (yellow, arrow = up) and the surface it used (green),
with the rim diameter, how much of the circle is present, and a fit quality. **Accept & save** writes
`<folder>\_oriented\<name>-oriented.ply` (full-resolution mesh, rim centre at the origin, axis = +Y, ready for
Sherd Studio's "Keep source orientation" import) and `<name>-oriented.json` (axis, diameter, quality, rotation matrix).
**Flip up/down** if the arrow points the wrong way (bases). **No rim (skip)** records body sherds. N / P keys move
along the list; done pieces are green.

How it works ("stand the sherd on its rim"): the rim top is perpendicular to the vessel axis, so the axis direction
is the surface normal at the clicked rim-top points, made perpendicular to the rim line. The axis position (the
radius) comes from the wall: seen along the axis, the wall normals point at the axis, which is a well-conditioned
intersection even for a short rim arc. If the wall is flat (nothing to intersect) the radius falls back to the circle
through the three clicks and the page says so; with nearly straight clicks the diameter is then unknown, as it would
be for an archaeologist with a rim chart.

Timing: about 15 s to prepare each piece (decimate, smooth, curvature); the next piece is prepared in the background.
Tested 8 Nov 2026 on the lamp pottery14(lamp4) (full rim, 68 mm, correct) and on Pottery1 (thick bead on a flat plate:
honest fallback). Python needs numpy, scipy, open3d, trimesh, pillow.

## Other files
- `rimclick.py` — mesh preparation (decimate + Taubin smooth), vectorised principal curvatures, 3-D circle fit; the
  first one-click prototype (lip growing) lives in its `main`, superseded.
- `rimtest.py` — offline test of the fit with a diagnostic two-view PNG (band green, seed magenta, axis, circle).
- `contact_sheet.py` — thumbnails of every mesh in a folder, 20 per sheet; used to survey the 187 sherd exports.
- `orient.py`, `classify.py`, `axisfit.py` — automatic-axis experiments (normals-line fit, wall/fracture classification,
  principal-curvature method). Finding: on these sherds the thick rim bead (5–12 mm) and the flat wall both defeat a
  free automatic fit (the bead's own core line is a strong local minimum; a flat wall is met by any perpendicular
  line). Kept for reference.

## Dataset
Laptop D:\Working_Folder_Pottery exports (241 STL/PLY, ~2.8 GB; 187 non-texture meshes) copied to the hub at
D:\Pottery\exports and to Kittayya D:\Pottery\exports (8 Nov 2026). Almost all are rim sherds with thick rounded
rims; one lamp; no specialist drawings exist yet for benchmarking.
