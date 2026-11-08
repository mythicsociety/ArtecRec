# rimclick — one-click rim orientation, maths prototype (8 Nov). Given a sherd mesh and ONE point on the rim lip
# (the user's click), find the whole rim lip, fit a circle to it, and orient the sherd: axis = circle normal, up = away
# from the wall. Prints radius, residual and inlier count; writes a coloured PLY (lip inliers green) and an oriented PLY.
# usage: rimclick.py in.stl seed_x seed_y seed_z [out_prefix]      (seed = clicked point in source coordinates)
import sys, numpy as np, open3d as o3d, trimesh
from scipy.spatial import cKDTree

def prepare(path, target=60000):
    m0 = o3d.io.read_triangle_mesh(path); m0.remove_duplicated_vertices(); m0.remove_degenerate_triangles()
    m = m0.simplify_quadric_decimation(target_number_of_triangles=target); m = m.filter_smooth_taubin(number_of_iterations=10)
    m.compute_triangle_normals(); V = np.asarray(m.vertices); F = np.asarray(m.triangles); N = np.asarray(m.triangle_normals)
    c = V[F].mean(1); e1 = V[F[:, 1]] - V[F[:, 0]]; e2 = V[F[:, 2]] - V[F[:, 0]]; area = 0.5 * np.linalg.norm(np.cross(e1, e2), axis=1)
    return V, F, N, c, area

def shape_operators(c, N, k=40):
    """per-face principal curvatures (kmin, kmax) and the max-curvature direction, from the k nearest same-side faces.
    Vectorised: local frame per face, shape operator W from dn = W q by weighted normal equations, 2x2 eigen in closed form."""
    tree = cKDTree(c); _, idx = tree.query(c, k=k + 1); idx = idx[:, 1:]                     # (F, k)
    same = (np.einsum('fkj,fj->fk', N[idx], N) > 0.3).astype(float)                           # ignore the far wall of the shell
    ax = np.where(np.abs(N[:, 0:1]) < 0.9, np.array([[1.0, 0, 0]]), np.array([[0, 1.0, 0]])); u = np.cross(N, ax); u /= np.linalg.norm(u, axis=1)[:, None]; v = np.cross(N, u)
    q = c[idx] - c[:, None, :]; dn = N[idx] - N[:, None, :]
    x = np.einsum('fkj,fj->fk', q, u); y = np.einsum('fkj,fj->fk', q, v); du = np.einsum('fkj,fj->fk', dn, u); dv = np.einsum('fkj,fj->fk', dn, v)
    sxx = (same * x * x).sum(1); sxy = (same * x * y).sum(1); syy = (same * y * y).sum(1); det = sxx * syy - sxy * sxy
    ok = (same.sum(1) >= 12) & (det > 1e-6 * (sxx + syy) ** 2)
    det = np.where(ok, det, 1.0); ixx = syy / det; ixy = -sxy / det; iyy = sxx / det            # inverse of [[sxx,sxy],[sxy,syy]]
    bxu = (same * x * du).sum(1); byu = (same * y * du).sum(1); bxv = (same * x * dv).sum(1); byv = (same * y * dv).sum(1)
    w00 = ixx * bxu + ixy * byu; w01 = ixy * bxu + iyy * byu; w10 = ixx * bxv + ixy * byv; w11 = ixy * bxv + iyy * byv
    a = w00; b = 0.5 * (w01 + w10); d = w11; tr = a + d; disc = np.sqrt(np.maximum(0.25 * (a - d) ** 2 + b * b, 0))
    kmin = 0.5 * tr - disc; kmax = 0.5 * tr + disc
    ex = np.where(np.abs(b) > 1e-12, b, 1.0); ey = np.where(np.abs(b) > 1e-12, kmax - a, 0.0); nrm = np.hypot(ex, ey) + 1e-12   # eigenvector of kmax in (u,v)
    dmax = (ex / nrm)[:, None] * u + (ey / nrm)[:, None] * v
    kmin[~ok] = 0; kmax[~ok] = 0; return kmin, kmax, dmax, ok

def fit_circle_3d(P, w=None):
    """plane (PCA) + algebraic circle fit in the plane; returns centre(3), normal(3), radius, per-point residual"""
    w = np.ones(len(P)) if w is None else w; m = (P * w[:, None]).sum(0) / w.sum(); X = P - m
    C = (X * w[:, None]).T @ X; ev, evec = np.linalg.eigh(C); n = evec[:, 0]; u = evec[:, 2]; v = evec[:, 1]
    x = X @ u; y = X @ v; A = np.c_[2 * x, 2 * y, np.ones(len(x))]; b = x * x + y * y
    sol = np.linalg.lstsq(A * w[:, None], b * w, rcond=None)[0]; cx, cy = sol[0], sol[1]; r = np.sqrt(max(sol[2] + cx * cx + cy * cy, 1e-9))
    centre = m + cx * u + cy * v; d_in = np.abs(np.hypot(x - cx, y - cy) - r); d_out = np.abs(X @ n); return centre, n, r, np.hypot(d_in, d_out)

def main(path, seed, out):
    V, F, N, c, area = prepare(path); print(f"{path}: {len(F)} faces (decimated+smoothed)")
    kmin, kmax, dmax, ok = shape_operators(c, N)
    tree = cKDTree(c); _, i0 = tree.query(seed); print(f"click -> face {i0}, kmax {kmax[i0]:.4f} (radius {1/max(abs(kmax[i0]),1e-9):.1f} mm), kmin {kmin[i0]:.4f}")
    # 1) lip candidates: convex, sharply curved like the clicked face (within a factor), in the whole mesh
    k0 = kmax[i0]; cand = ok & (kmax > 0.5 * k0) & (kmax < 2.0 * k0) & (kmax > 0.03)   # radius under ~33 mm: a lip, not a wall
    print(f"lip-like faces: {cand.sum()} of {len(c)}")
    # 2) grow a connected lip region from the click through lip-like faces (8 mm steps)
    sel = np.zeros(len(c), bool); sel[i0] = True; frontier = [i0]
    while frontier:
        nxt = []
        for i in frontier:
            for j in tree.query_ball_point(c[i], 8.0):
                if cand[j] and not sel[j]: sel[j] = True; nxt.append(j)
        frontier = nxt
    print(f"connected lip region: {sel.sum()} faces, extent {np.round(c[sel].max(0) - c[sel].min(0), 1)} mm")
    # 3) crest points: within the region, the lip crest is where the surface normal is most 'outward along the max-curvature
    #    direction' - simpler: use all region face centres offset to the crest by their curvature centre? Use the centres directly,
    #    robust circle fit with iterative reweighting (the lip is a thin band; its centreline is the rim circle up to lip radius).
    P = c[sel]; w = area[sel].copy()
    for it in range(6):
        ctr, nrm, r, res = fit_circle_3d(P, w); s = max(np.median(res) * 1.5, 0.5); w = area[sel] * (1.0 / (1.0 + (res / s) ** 2))
    inl = res < 3 * max(np.median(res), 0.5)
    print(f"rim circle: radius {r:.1f} mm (diameter {2*r:.0f}), centre {np.round(ctr,1)}, normal {np.round(nrm,3)}; residual median {np.median(res):.2f} mm, inliers {inl.sum()}/{len(P)}; arc covered ~{np.degrees(2*np.arcsin(min(1, (np.ptp(P[inl] @ np.cross(nrm, [1,0,0] if abs(nrm[0])<0.9 else [0,1,0])))/(2*r)))):.0f} deg (chord estimate)")
    # 4) up: the wall hangs off the rim; the axis points from the sherd body towards the rim
    body = c.mean(0); up = nrm if ((ctr - body) @ nrm) > 0 else -nrm
    # 5) orient: axis -> +Y, centre -> origin
    y = up; x = np.cross([0, 0, 1], y); x = np.cross([1, 0, 0], y) if np.linalg.norm(x) < 0.3 else x; x /= np.linalg.norm(x); z = np.cross(x, y)
    R = np.vstack([x, y, z]); Vo = (V - ctr) @ R.T
    tm = trimesh.Trimesh(Vo, F, process=False); tm.export(out + "-oriented.ply")
    col = np.tile(np.array([200, 200, 200], np.uint8), (len(F), 1)); col[cand] = (90, 160, 220); idx_sel = np.flatnonzero(sel); col[idx_sel[inl]] = (60, 180, 90); col[i0] = (255, 0, 255)
    vc = np.zeros((len(V), 3)); cnt = np.zeros(len(V)); np.add.at(vc, F.ravel(), np.repeat(col, 3, axis=0)); np.add.at(cnt, F.ravel(), 1)
    tc = trimesh.Trimesh(V, F, process=False); tc.visual = trimesh.visual.ColorVisuals(tc, vertex_colors=np.c_[(vc / np.maximum(cnt, 1)[:, None]).astype(np.uint8), np.full(len(V), 255, np.uint8)]); tc.export(out + "-lip.ply")
    # wall radius check in the oriented frame: distance of all faces from the Y axis
    co = (c - ctr) @ R.T; rw = np.hypot(co[:, 0], co[:, 2]); print(f"oriented: all-face radius 10/50/90% {np.percentile(rw,10):.0f}/{np.median(rw):.0f}/{np.percentile(rw,90):.0f} mm; height span {np.ptp(co[:,1]):.0f} mm; wrote {out}-oriented.ply, {out}-lip.ply")

if __name__ == '__main__':
    main(sys.argv[1], np.array([float(sys.argv[2]), float(sys.argv[3]), float(sys.argv[4])]), sys.argv[5] if len(sys.argv) > 5 else 'rimclick')
