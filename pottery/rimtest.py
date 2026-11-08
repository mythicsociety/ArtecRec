# rimtest — run the rim-click fit on one mesh with an automatic seed, and draw what it did (band green, seed magenta,
# axis yellow, rim circle red) as a two-view PNG. usage: rimtest.py <mesh name or suffix> <out.png> [sx sy sz]
import sys, os, numpy as np; path = sys.argv[1]; out = sys.argv[2]; seed_arg = [float(x) for x in sys.argv[3:6]] if len(sys.argv) >= 6 else None
sys.argv = [sys.argv[0], "D:/Pottery/exports"]; import rimtool as rt
from PIL import Image, ImageDraw
i = [k for k, f in enumerate(rt.files) if f.replace("\\", "/").endswith(path.replace("\\", "/"))][0]
d = rt.prep(i); kmax, ok, c, N = d["kmax"], d["ok"], d["c"], d["N"]
if seed_arg is None:
    cand = ok & (kmax > 0.09) & (kmax < 0.25); P = c[cand]; seed = P[np.argmin(np.linalg.norm(P - np.median(P, axis=0), axis=1))]   # bead crest: 4-11 mm radius
else: seed = np.array(seed_arg)
_, i0 = d["tree"].query(seed); print(os.path.basename(rt.files[i]), "seed", np.round(seed, 1), f"kmax {kmax[i0]:.3f} (r {1/max(kmax[i0],1e-9):.1f} mm)")
r = rt.fit(d, seed); lip = set(r.pop("lip", [])); print({k: (round(v, 2) if isinstance(v, float) else v) for k, v in r.items()})
def rot(ax, deg):
    a = np.radians(deg); co, s = np.cos(a), np.sin(a)
    return {"x": np.array([[1, 0, 0], [0, co, -s], [0, s, co]]), "y": np.array([[co, 0, s], [0, 1, 0], [-s, 0, co]])}[ax]
def render(R, size=600):
    V, F = d["V"], d["F"]; ctr0 = V.mean(0); P = (V - ctr0) @ R.T; s = (size * 0.8) / max(np.ptp(P[:, :2], axis=0).max(), 1e-6); P = P * s; P[:, :2] += size / 2
    n = N @ R.T; depth = P[F][:, :, 2].mean(1); order = np.argsort(depth); light = np.array([0.3, 0.5, 0.8]); light /= np.linalg.norm(light); shade = np.clip(np.abs(n @ light), 0, 1) * 0.7 + 0.25
    img = Image.new("RGB", (size, size), (40, 40, 40)); dr = ImageDraw.Draw(img)
    for f in order:
        g = shade[f]; col = (int(70 * g), int(200 * g), int(90 * g)) if f in lip else (int(210 * g), int(205 * g), int(195 * g)); dr.polygon([tuple(P[F[f, k], :2]) for k in range(3)], fill=col)
    def pt(x): q = ((np.asarray(x) - ctr0) @ R.T) * s; return (q[0] + size / 2, q[1] + size / 2)
    sp = pt(seed); dr.ellipse([sp[0] - 5, sp[1] - 5, sp[0] + 5, sp[1] + 5], fill=(255, 0, 255))
    if "centre" in r:
        C = np.array(r["centre"]); U = np.array(r["up"]); L = max(r["radius"] * 1.5, 40); dr.line([pt(C - U * L), pt(C + U * L)], fill=(255, 215, 0), width=3)
        u = np.cross(U, [1, 0, 0]); u = np.cross(U, [0, 1, 0]) if np.linalg.norm(u) < 0.3 else u; u /= np.linalg.norm(u); v = np.cross(U, u)
        ring = [pt(C + r["radius"] * (np.cos(t) * u + np.sin(t) * v)) for t in np.linspace(0, 2 * np.pi, 120)]; dr.line(ring + [ring[0]], fill=(255, 60, 50), width=2)
    return img
a = render(np.eye(3)); b = render(rot("x", 90) @ rot("y", 60)); sheet = Image.new("RGB", (1200, 600)); sheet.paste(a, (0, 0)); sheet.paste(b, (600, 0)); sheet.save(out); print("wrote", out)
