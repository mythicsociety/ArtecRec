# orient — sherd axis estimate, second attempt (8 Nov). Steps: decimate + smooth (kills the scan grain), curvature by
# sagitta over an 8 mm neighbourhood (walls: large radius; rim lip and fracture: small), walls only -> axis from normals,
# trimmed. Prints the estimate, writes a coloured PLY (red excluded, blue wall, green used) for inspection.
import sys, numpy as np, open3d as o3d, trimesh
from scipy.spatial import cKDTree
def axis_from_normals(c, n, w):
    W = w / w.sum(); P = np.eye(3)[None] - n[:, :, None] * n[:, None, :]
    M = (W[:, None, None] * P).sum(0); b = (W[:, None] * (P @ c[:, :, None])[:, :, 0]).sum(0)
    p = np.linalg.lstsq(M, b, rcond=None)[0]; S = (W[:, None, None] * n[:, :, None] * n[:, None, :]).sum(0)
    ev, evec = np.linalg.eigh(S); return p, evec[:, 0], ev
def nl_dist(p, d, c, n):
    cr = np.cross(n, d[None]); cl = np.linalg.norm(cr, axis=1) + 1e-12; return np.abs(((c - p) * cr).sum(1)) / cl
def main(path, out, target=40000, rho=16.0):
    m0 = o3d.io.read_triangle_mesh(path); m0.remove_duplicated_vertices(); m0.remove_degenerate_triangles()
    m = m0.simplify_quadric_decimation(target_number_of_triangles=target); m = m.filter_smooth_taubin(number_of_iterations=10); m.compute_triangle_normals(); m.compute_vertex_normals()
    V = np.asarray(m.vertices); F = np.asarray(m.triangles); N = np.asarray(m.triangle_normals); c = V[F].mean(1)
    e1 = V[F[:, 1]] - V[F[:, 0]]; e2 = V[F[:, 2]] - V[F[:, 0]]; area = 0.5 * np.linalg.norm(np.cross(e1, e2), axis=1)
    print(f"{path}: {len(F)} faces after decimation, extent {np.round(V.max(0) - V.min(0), 1)} mm")
    # sagitta curvature: distance of neighbours (within rho) from the face's tangent plane vs their in-plane distance
    tree = cKDTree(c); nb = tree.query_ball_point(c, rho); rad = np.full(len(F), np.inf); rough = np.zeros(len(F))
    for i, ids in enumerate(nb):
        if len(ids) < 12: continue
        ids = np.asarray(ids); same = (N[ids] @ N[i]) > 0.5          # same side of the shell only (the far wall has an opposite normal)
        ids = ids[same]
        if len(ids) < 12: continue
        q = c[ids] - c[i]; h = q @ N[i]; r2 = np.maximum((q * q).sum(1) - h * h, 1e-9)
        far = r2 > (0.5 * rho) ** 2
        if far.sum() < 6: continue
        kap = 2 * np.abs(h[far]) / r2[far]; rad[i] = 1.0 / max(np.median(kap), 1e-6)   # sagitta: h = r^2 / (2R)
        rough[i] = np.std(h[far] - np.median(h[far]))                                     # scatter about the local curve, mm
    print(f"curvature radius 25/50/75%: {np.percentile(rad,25):.0f}/{np.median(rad):.0f}/{np.percentile(rad,75):.0f} mm; roughness median {np.median(rough):.2f} mm")
    wall = (rad > 40) & (rough < np.percentile(rough, 85)); print(f"wall faces: {wall.mean()*100:.0f}%")
    keep = wall.copy()
    for it in range(5):
        p, d, ev = axis_from_normals(c[keep], N[keep], area[keep]); dist = nl_dist(p, d, c, N)
        keep = wall & (dist <= np.percentile(dist[wall], 70))
        print(f"iter {it}: dir {np.round(d,3)} point {np.round(p,1)} | normal-line dist median (wall) {np.median(dist[wall]):.2f} mm | eig {np.round(ev,3)}")
    rr = np.linalg.norm((c[keep] - p) - ((c[keep] - p) @ d)[:, None] * d[None], axis=1); hh = (c[keep] - p) @ d
    print(f"radius of used faces: median {np.median(rr):.1f} mm (10-90% {np.percentile(rr,10):.1f}-{np.percentile(rr,90):.1f}); height span {hh.max()-hh.min():.1f} mm; ambiguity eig0/eig1 {ev[0]/max(ev[1],1e-9):.2f}")
    # --- second method: principal curvature directions. On a pot wall the meridian (vertical) direction is nearly
    # straight and the parallel (horizontal) direction is curved with radius ~ the vessel radius. The axis is parallel
    # to the mean minimum-curvature direction, and the centres of curvature along the maximum direction lie near the axis.
    dmins, cents, wts = [], [], []
    for i in np.flatnonzero(wall):
        ids = np.asarray(nb[i]); ids = ids[(N[ids] @ N[i]) > 0.5]
        if len(ids) < 15: continue
        # local frame
        u = np.cross(N[i], [1, 0, 0]); u = np.cross(N[i], [0, 1, 0]) if np.linalg.norm(u) < 0.3 else u; u /= np.linalg.norm(u); v = np.cross(N[i], u)
        q = c[ids] - c[i]; x = q @ u; y = q @ v; dn = N[ids] - N[i]; dnu = dn @ u; dnv = dn @ v
        A = np.c_[x, y]; AtA = A.T @ A
        if np.linalg.cond(AtA) > 1e6: continue
        Wm = np.linalg.solve(AtA, A.T @ np.c_[dnu, dnv]).T      # shape operator estimate (2x2), dn = W q
        Wm = 0.5 * (Wm + Wm.T); ev, evec = np.linalg.eigh(Wm)     # eigen: curvatures (1/mm), directions in (u,v)
        kmin, kmax = ev[0], ev[1]
        if abs(kmax) < 1e-4 or abs(kmax) > 0.2: continue             # radius must be 5..10000 mm
        dmin3 = evec[0, 0] * u + evec[1, 0] * v; dmins.append(dmin3); cents.append(c[i] + N[i] / kmax); wts.append(area[i])
    dmins = np.array(dmins); cents = np.array(cents); wts = np.array(wts)
    if len(dmins) > 50:
        Smin = (wts[:, None, None] * dmins[:, :, None] * dmins[:, None, :]).sum(0); evs, evecs = np.linalg.eigh(Smin); dax = evecs[:, 2]   # dominant min-curvature direction
        # robust axis position: the line through the curvature centres, direction fixed to dax: medians of the projected offsets
        off = cents - (cents @ dax)[:, None] * dax[None]; pax = np.median(off, axis=0)
        resid = np.linalg.norm(off - pax, axis=1)
        print(f"curvature method: axis dir {np.round(dax,3)}, through {np.round(pax,1)}; centre scatter median {np.median(resid):.1f} mm; faces used {len(dmins)}; direction consistency {evs[2]/evs.sum():.2f}")
        rr2 = np.linalg.norm((c[wall] - pax) - ((c[wall] - pax) @ dax)[:, None] * dax[None], axis=1)
        print(f"curvature method: wall radius median {np.median(rr2):.1f} mm (10-90% {np.percentile(rr2,10):.1f}-{np.percentile(rr2,90):.1f})")
    col = np.tile(np.array([200, 60, 60], np.uint8), (len(F), 1)); col[wall] = (90, 160, 220); col[keep] = (60, 180, 90)
    tm = trimesh.Trimesh(V, F, process=False); vc = np.zeros((len(V), 3)); cnt = np.zeros(len(V)); np.add.at(vc, F.ravel(), np.repeat(col, 3, axis=0)); np.add.at(cnt, F.ravel(), 1)
    tm.visual = trimesh.visual.ColorVisuals(tm, vertex_colors=np.c_[(vc / np.maximum(cnt, 1)[:, None]).astype(np.uint8), np.full(len(V), 255, np.uint8)]); tm.export(out); print("wrote", out)
if __name__ == '__main__': main(sys.argv[1], sys.argv[2])
