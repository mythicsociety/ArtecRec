# axisfit — first look at automatic sherd orientation (8 Nov). Binary STL in, axis estimate out.
# Idea: on a surface of revolution every surface normal line passes through the axis. Fit the line that all
# normal lines come closest to (least squares on the normal lines), with trimming of the worst faces so that
# fracture faces and noise do not steer it. Also report PCA for comparison.
import sys, struct, numpy as np
def read_stl(path):
    d = open(path, 'rb').read(); n = struct.unpack('<I', d[80:84])[0]
    a = np.frombuffer(d[84:84 + 50 * n], dtype=np.dtype([('n', '<f4', 3), ('v', '<f4', (3, 3)), ('a', '<u2')]))
    return a['v'].astype(np.float64), a['n'].astype(np.float64)
def face_geometry(V):
    c = V.mean(axis=1); e1 = V[:, 1] - V[:, 0]; e2 = V[:, 2] - V[:, 0]; nrm = np.cross(e1, e2); area = 0.5 * np.linalg.norm(nrm, axis=1)
    ok = area > 1e-9; nrm[ok] /= (2 * area[ok])[:, None]; return c, nrm, area, ok
def fit_axis_from_normals(c, n, w):
    # minimise sum_i w_i * dist^2(line, normal line i). For a point p on the axis and direction d:
    # the squared distance between two lines is not linear; use the classic linearisation: find the point X closest
    # to all normal lines (weighted least squares), repeat for points offset along +-normals? Simpler and robust:
    # the axis is the line minimising sum w_i |(X - c_i) x n_i|^2 over points X on it. For a line X = p + t d this is a
    # quadratic in (p, d); we solve via the point-closest-to-all-lines, then the direction from the 3x3 moment of the
    # normals (the axis direction is the direction most perpendicular to all normals: smallest eigenvector of sum w n n^T).
    W = w / w.sum()
    M = (W[:, None, None] * (np.eye(3)[None] - n[:, :, None] * n[:, None, :])).sum(axis=0)
    b = (W[:, None] * ((np.eye(3)[None] - n[:, :, None] * n[:, None, :]) @ c[:, :, None])[:, :, 0]).sum(axis=0)
    p = np.linalg.lstsq(M, b, rcond=None)[0]
    S = (W[:, None, None] * n[:, :, None] * n[:, None, :]).sum(axis=0)
    evals, evecs = np.linalg.eigh(S); d = evecs[:, 0]
    return p, d / np.linalg.norm(d), evals
def line_point_dist(p, d, q):
    v = q - p; return np.linalg.norm(v - (v @ d)[:, None] * d[None], axis=1)
def normal_line_to_axis_dist(p, d, c, n):
    # shortest distance between the axis line and each normal line
    cr = np.cross(n, d[None]); cl = np.linalg.norm(cr, axis=1); out = np.empty(len(c))
    par = cl < 1e-9; out[par] = line_point_dist(p, d, c[par]) if par.any() else 0
    g = ~par; out[g] = np.abs(((c[g] - p) * cr[g]).sum(axis=1)) / cl[g]; return out
def main(path):
    V, N0 = read_stl(path); c, n, area, ok = face_geometry(V); c, n, area = c[ok], n[ok], area[ok]
    bb = V.reshape(-1, 3); lo, hi = bb.min(0), bb.max(0); print(f"{path}: {len(c)} faces, extent {hi - lo} mm")
    # PCA for comparison
    X = c - c.mean(0); ev, evec = np.linalg.eigh((X * area[:, None]).T @ X / area.sum()); print("PCA sqrt-eigen (mm):", np.sqrt(np.maximum(ev, 0)))
    # iterative trimmed fit: start with all faces, drop the 30% worst by normal-line distance, refit
    w = area.copy(); keep = np.ones(len(c), bool)
    for it in range(6):
        p, d, evals = fit_axis_from_normals(c[keep], n[keep], w[keep])
        dist = normal_line_to_axis_dist(p, d, c, n)
        thr = np.percentile(dist[keep], 70); keep = dist <= thr
        print(f"iter {it}: axis point {np.round(p, 1)} dir {np.round(d, 3)} | normal-line dist median {np.median(dist):.2f} mm, kept {keep.mean() * 100:.0f}%, eig {np.round(evals, 3)}")
    # radius statistics of kept faces around the axis
    r = line_point_dist(p, d, c[keep]); h = (c[keep] - p) @ d
    print(f"radius of kept faces: median {np.median(r):.1f} mm, 10-90% {np.percentile(r, 10):.1f}-{np.percentile(r, 90):.1f}; height span {h.min():.1f}..{h.max():.1f} mm")
    # the fit is only meaningful if the normals are not all nearly parallel (flat sherd) and radii agree
    print("ambiguity: smallest/middle eigen ratio", evals[0] / max(evals[1], 1e-12))
if __name__ == '__main__': main(sys.argv[1])
