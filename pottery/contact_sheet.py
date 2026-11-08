# contact_sheet — tiny shaded thumbnails of every sherd export, two views each, tiled into sheets of 20.
# Pure numpy rasteriser (no Blender), decimates to 20k faces first. usage: contact_sheet.py <export_root> <out_dir>
import sys, os, glob, numpy as np, open3d as o3d
from PIL import Image, ImageDraw
def load(path, target=20000):
    m = o3d.io.read_triangle_mesh(path); m.remove_duplicated_vertices(); m.remove_degenerate_triangles()
    if len(m.triangles) > target: m = m.simplify_quadric_decimation(target_number_of_triangles=target)
    m.compute_triangle_normals(); return np.asarray(m.vertices), np.asarray(m.triangles), np.asarray(m.triangle_normals)
def render(V, F, N, R, size=220):
    # project with rotation R (3x3), orthographic, painter's sort by depth, flat shading
    P = (V - V.mean(0)) @ R.T; s = (size * 0.9) / max(np.ptp(P[:, :2], axis=0).max(), 1e-6); P = P * s; P[:, :2] += size / 2
    n = N @ R.T; depth = P[F][:, :, 2].mean(1); order = np.argsort(depth)
    light = np.array([0.3, 0.5, 0.8]); light /= np.linalg.norm(light); shade = np.clip(np.abs(n @ light), 0, 1) * 0.75 + 0.2
    img = Image.new("RGB", (size, size), (245, 245, 245)); d = ImageDraw.Draw(img)
    for i in order:
        g = int(shade[i] * 230); tri = [tuple(P[F[i, k], :2]) for k in range(3)]; d.polygon(tri, fill=(g, int(g * 0.95), int(g * 0.9)))
    return img
def rot(ax, deg):
    a = np.radians(deg); c, s = np.cos(a), np.sin(a)
    return {"x": np.array([[1, 0, 0], [0, c, -s], [0, s, c]]), "y": np.array([[c, 0, s], [0, 1, 0], [-s, 0, c]])}[ax]
def main(root, out):
    os.makedirs(out, exist_ok=True); files = sorted(glob.glob(os.path.join(root, "**", "*.stl"), recursive=True) + glob.glob(os.path.join(root, "**", "*.ply"), recursive=True))
    files = [f for f in files if "texture" not in os.path.basename(f).lower()]; print(len(files), "meshes"); tiles = []
    for f in files:
        try: V, F, N = load(f)
        except Exception as e: print("skip", f, e); continue
        ext = np.ptp(V, axis=0); a = render(V, F, N, np.eye(3)); b = render(V, F, N, rot("x", 90) @ rot("y", 60))
        tile = Image.new("RGB", (440, 250), (255, 255, 255)); tile.paste(a, (0, 0)); tile.paste(b, (220, 0))
        ImageDraw.Draw(tile).text((4, 228), f"{os.path.splitext(os.path.basename(f))[0]}  {ext[0]:.0f}x{ext[1]:.0f}x{ext[2]:.0f} mm  {len(F)}f", fill=(0, 0, 0)); tiles.append(tile)
    per = 20; cols = 4
    for s in range(0, len(tiles), per):
        chunk = tiles[s:s + per]; rows = (len(chunk) + cols - 1) // cols; sheet = Image.new("RGB", (cols * 440, rows * 250), (255, 255, 255))
        for i, t in enumerate(chunk): sheet.paste(t, ((i % cols) * 440, (i // cols) * 250))
        p = os.path.join(out, f"sheet-{s // per + 1:02d}.png"); sheet.save(p); print("wrote", p)
if __name__ == "__main__": main(sys.argv[1], sys.argv[2])
