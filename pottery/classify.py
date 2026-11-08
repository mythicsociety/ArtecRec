# classify — separate smooth wall faces from rough fracture faces on a sherd mesh, then fit the axis on walls only.
# Roughness: for each face, the angle spread of normals in its neighbourhood (k-nearest face centroids). Walls are
# smooth (small spread); fractures and the rim cap are rough. Writes a coloured PLY for inspection.
import sys, numpy as np, trimesh
from scipy.spatial import cKDTree
def load(path):
    m = trimesh.load(path, force='mesh'); m.remove_unreferenced_vertices(); return m
def roughness(m, k=120):
    c = m.triangles_center; n = m.face_normals; tree = cKDTree(c); dist, idx = tree.query(c, k=k)
    nn = n[idx]                                     # (F, k, 3)
    mean = nn.mean(axis=1); mean /= np.linalg.norm(mean, axis=1)[:, None] + 1e-12
    cosang = np.clip((nn * mean[:, None, :]).sum(axis=2), -1, 1); ang = np.arccos(cosang)   # rad
    rough = np.degrees(ang.mean(axis=1))            # fine-scale grain + real curvature, deg
    # robust curvature: angle to the central face's own normal vs distance, in the outer half of the neighbourhood
    cos0 = np.clip((nn * n[:, None, :]).sum(axis=2), -1, 1); a0 = np.arccos(cos0)
    far = dist > np.median(dist, axis=1, keepdims=True); kappa = np.where(far, a0 / np.maximum(dist, 1e-6), np.nan)
    kap = np.nanmedian(kappa, axis=1); rad_curv = 1.0 / np.maximum(kap, 1e-6)
    return rough, rad_curv                          # deg; mm
def axis_from_normals(c, n, w):
    W = w / w.sum(); P = np.eye(3)[None] - n[:, :, None] * n[:, None, :]
    M = (W[:, None, None] * P).sum(0); b = (W[:, None] * (P @ c[:, :, None])[:, :, 0]).sum(0)
    p = np.linalg.lstsq(M, b, rcond=None)[0]; S = (W[:, None, None] * n[:, :, None] * n[:, None, :]).sum(0)
    ev, evec = np.linalg.eigh(S); return p, evec[:, 0], ev
def nl_dist(p, d, c, n):
    cr = np.cross(n, d[None]); cl = np.linalg.norm(cr, axis=1) + 1e-12; return np.abs(((c - p) * cr).sum(1)) / cl
def main(path, out):
    m = load(path); print(f"{path}: {len(m.faces)} faces, watertight {m.is_watertight}, extent {np.round(m.extents, 1)} mm")
    r, rc = roughness(m); thr = np.percentile(r, 60); wall = (r < thr) & (rc > 20.0)   # smooth AND gently curved: no fracture, no rim lip
    print(f"roughness deg: median {np.median(r):.1f}, 25/75% {np.percentile(r,25):.1f}/{np.percentile(r,75):.1f}; curvature radius 25/50/75% {np.percentile(rc,25):.1f}/{np.median(rc):.1f}/{np.percentile(rc,75):.1f} mm; wall faces: {wall.mean()*100:.0f}%")
    # two-sided wall split by normal direction clusters is left for later; fit the axis on all smooth faces, trimmed
    c = m.triangles_center; n = m.face_normals; a = m.area_faces; keep = wall.copy()
    for it in range(4):
        p, d, ev = axis_from_normals(c[keep], n[keep], a[keep]); dist = nl_dist(p, d, c, n)
        thr2 = np.percentile(dist[wall], 60); keep = wall & (dist <= thr2)
        print(f"iter {it}: dir {np.round(d,3)} point {np.round(p,1)} | median normal-line dist (walls) {np.median(dist[wall]):.2f} mm | eig {np.round(ev,3)}")
    rad = np.linalg.norm((c[keep] - p) - ((c[keep] - p) @ d)[:, None] * d[None], axis=1)
    print(f"radius of kept wall faces: median {np.median(rad):.1f} mm, 10-90% {np.percentile(rad,10):.1f}-{np.percentile(rad,90):.1f}")
    col = np.zeros((len(m.faces), 4), np.uint8); col[:] = (200, 60, 60, 255); col[wall] = (90, 160, 220, 255); col[keep] = (60, 180, 90, 255)
    vc = np.zeros((len(m.vertices), 4), np.float64); cnt = np.zeros(len(m.vertices)); np.add.at(vc, m.faces.ravel(), np.repeat(col, 3, axis=0)); np.add.at(cnt, m.faces.ravel(), 1)
    m.visual = trimesh.visual.ColorVisuals(m, vertex_colors=(vc / np.maximum(cnt, 1)[:, None]).astype(np.uint8)); m.export(out); print("wrote", out)
if __name__ == '__main__': main(sys.argv[1], sys.argv[2])
