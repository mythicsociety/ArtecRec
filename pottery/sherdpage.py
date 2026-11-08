# sherdpage — from an oriented sherd (rimtool output: axis = +Y, rim centre at origin) build the 3-D record:
#   * thickness map (wall thickness at every vertex, mm) as vertex colours on the sherd
#   * the radial profile (cut by the best plane through the axis), kept as 3-D geometry and as 2-D (r, h) points
#   * the revolved reconstruction: solid where the sherd is (its arc), ghost elsewhere
#   * a GLB per part and one viewer page (three.js) with toggles, a section cut and measurements
# usage: sherdpage.py <name>-oriented.ply [out_dir]      (reads <name>-oriented.json next to it if present)
import sys, os, json, numpy as np, trimesh, open3d as o3d

def load(path):
    m = trimesh.load(path, force="mesh"); m.remove_unreferenced_vertices(); m.merge_vertices(); return m

def thickness(m, step=0.0):
    """wall thickness per vertex: distance from the vertex along -normal to the opposite wall (first hit), mm"""
    scene = o3d.t.geometry.RaycastingScene(); scene.add_triangles(o3d.t.geometry.TriangleMesh.from_legacy(o3d.geometry.TriangleMesh(o3d.utility.Vector3dVector(m.vertices), o3d.utility.Vector3iVector(m.faces))))
    n = m.vertex_normals; origins = m.vertices - n * 0.05; rays = np.c_[origins, -n].astype(np.float32)
    hit = scene.cast_rays(o3d.core.Tensor(rays)); t = hit["t_hit"].numpy(); t[~np.isfinite(t)] = np.nan
    t = np.where(t > 40, np.nan, t)                                  # > 40 mm is not a wall thickness (ray left through a break)
    return t

def decimate(m, target):
    if len(m.faces) <= target: return m
    o = o3d.geometry.TriangleMesh(o3d.utility.Vector3dVector(m.vertices), o3d.utility.Vector3iVector(m.faces)); o = o.simplify_quadric_decimation(target_number_of_triangles=target)
    return trimesh.Trimesh(np.asarray(o.vertices), np.asarray(o.triangles), process=True)

def best_profile(m, n_az=72):
    """cut by planes through the Y axis at n_az azimuths; keep the cut with the largest height span (most of the wall)"""
    best = None
    for k in range(n_az):
        a = 2 * np.pi * k / n_az; normal = np.array([-np.sin(a), 0, np.cos(a)]); radial = np.array([np.cos(a), 0, np.sin(a)])
        sec = trimesh.intersections.mesh_plane(m, normal, np.zeros(3))          # (S, 2, 3) segments
        if len(sec) < 10: continue
        P = sec.reshape(-1, 3); r = P @ radial; keep = r > 0; P = P[keep]         # the half-plane on the +radial side only
        if len(P) < 10: continue
        span = np.ptp(P[:, 1]); score = span + 0.2 * np.ptp(P @ radial)
        if best is None or score > best[0]: best = (score, a, sec[keep.reshape(-1, 2).all(1)], radial)
    if best is None: return None
    score, a, sec, radial = best; P = sec.reshape(-1, 3); rh = np.c_[P @ radial, P[:, 1]]
    return {"azimuth": float(np.degrees(a)), "segments": sec, "rh": rh, "radial": radial}

def order_loop(rh_segments):
    """chain 2-D segments into polylines (the section of a shell is one closed loop, or several if the cut crosses breaks)"""
    segs = [(tuple(np.round(s[0], 3)), tuple(np.round(s[1], 3))) for s in rh_segments]; adj = {}
    for a, b in segs: adj.setdefault(a, []).append(b); adj.setdefault(b, []).append(a)
    seen = set(); loops = []
    for start in adj:
        if start in seen: continue
        loop = [start]; seen.add(start); cur = start; prev = None
        while True:
            nxt = [q for q in adj[cur] if q != prev and q not in seen]
            if not nxt: break
            prev, cur = cur, nxt[0]; loop.append(cur); seen.add(cur)
        loops.append(np.array(loop))
    loops.sort(key=len, reverse=True); return loops

def revolve(loop_rh, arc_from, arc_to, n_seg=96, closed=True):
    """revolve a 2-D (r,h) loop around Y over [arc_from, arc_to] radians -> trimesh"""
    th = np.linspace(arc_from, arc_to, n_seg + 1); R, H = loop_rh[:, 0], loop_rh[:, 1]
    V = np.stack([np.outer(R, np.cos(th)), np.repeat(H[:, None], len(th), 1), np.outer(R, np.sin(th))], -1).reshape(-1, 3)   # (L*(n+1), 3)
    L, T = len(R), len(th); idx = lambda i, j: i * T + j; F = []
    rng = range(L) if closed else range(L - 1)
    for i in rng:
        i2 = (i + 1) % L
        for j in range(T - 1): F += [[idx(i, j), idx(i2, j), idx(i, j + 1)], [idx(i2, j), idx(i2, j + 1), idx(i, j + 1)]]
    m = trimesh.Trimesh(V, np.array(F), process=True); trimesh.repair.fix_normals(m); return m

def colour_by(values, vmin, vmax):
    """blue (thin) -> yellow -> red (thick), grey where unknown; returns uint8 RGBA"""
    t = np.clip((values - vmin) / max(vmax - vmin, 1e-6), 0, 1); c = np.zeros((len(values), 4), np.uint8); c[:, 3] = 255
    stops = np.array([[40, 90, 200], [60, 170, 120], [230, 200, 60], [210, 60, 50]], float); pos = np.array([0, 0.33, 0.66, 1.0])
    for k in range(3): c[:, k] = np.interp(t, pos, stops[:, k]).astype(np.uint8)
    c[~np.isfinite(values)] = (150, 150, 150, 255); return c

def main(path, out_dir=None):
    base = os.path.basename(path).replace("-oriented.ply", ""); out_dir = out_dir or os.path.join(os.path.dirname(path), "pages"); os.makedirs(out_dir, exist_ok=True)
    meta_path = path.replace(".ply", ".json"); meta = json.load(open(meta_path)) if os.path.exists(meta_path) else {}
    m = load(path); print(f"{base}: {len(m.faces)} faces, extent {np.round(m.extents, 1)} mm")
    # thickness on a 150k-face copy (ray casting cost), transferred by nearest vertex
    md = decimate(m, 150000); th = thickness(md); good = np.isfinite(th)
    print(f"thickness: median {np.nanmedian(th):.1f} mm, 10-90% {np.nanpercentile(th,10):.1f}-{np.nanpercentile(th,90):.1f}, known on {good.mean()*100:.0f}% of vertices")
    tmin, tmax = float(np.nanpercentile(th, 2)), float(np.nanpercentile(th, 98)); md.visual = trimesh.visual.ColorVisuals(md, vertex_colors=colour_by(th, tmin, tmax))
    # sherd arc around the axis (where there is wall), from vertex azimuths
    az = np.degrees(np.arctan2(md.vertices[:, 2], md.vertices[:, 0])); hist, edges = np.histogram(az, bins=72, range=(-180, 180)); present = hist > max(3, 0.002 * len(az))
    arc_deg = float(present.sum() * 5)
    # profile
    prof = best_profile(md); rec_known = rec_ghost = None; profile_rh = []
    if prof is not None:
        loops = order_loop(np.c_[prof["rh"][0::2], prof["rh"][1::2]].reshape(-1, 2, 2)) if False else order_loop(prof["rh"].reshape(-1, 2, 2))
        loop = loops[0]; profile_rh = loop.tolist(); print(f"profile at azimuth {prof['azimuth']:.0f} deg: {len(loop)} points, r {loop[:,0].min():.1f}-{loop[:,0].max():.1f}, h {loop[:,1].min():.1f}-{loop[:,1].max():.1f}")
        # arc of the sherd about the profile's azimuth; known = where the sherd is, ghost = the rest
        a0 = np.radians(prof["azimuth"]); centres = np.radians((edges[:-1] + edges[1:]) / 2)
        # contiguous present-span containing a0
        k0 = int(((prof["azimuth"] + 180) % 360) // 5); lo = hi = k0
        while present[(lo - 1) % 72] and (k0 - lo) < 71: lo -= 1
        while present[(hi + 1) % 72] and (hi - k0) < 71: hi += 1
        a_lo, a_hi = np.radians(-180 + lo * 5), np.radians(-180 + (hi + 1) * 5)
        rec_known = revolve(loop, a_lo, a_hi, n_seg=max(8, int((a_hi - a_lo) / (2 * np.pi) * 96)))
        rec_ghost = revolve(loop, a_hi, a_lo + 2 * np.pi, n_seg=max(8, int((2 * np.pi - (a_hi - a_lo)) / (2 * np.pi) * 96)))
        rec_known.visual = trimesh.visual.ColorVisuals(rec_known, vertex_colors=np.tile([205, 170, 130, 255], (len(rec_known.vertices), 1)).astype(np.uint8))
        rec_ghost.visual = trimesh.visual.ColorVisuals(rec_ghost, vertex_colors=np.tile([120, 140, 170, 255], (len(rec_ghost.vertices), 1)).astype(np.uint8))
    # exports
    md.export(os.path.join(out_dir, base + "-thick.glb")); plain = md.copy(); plain.visual = trimesh.visual.ColorVisuals(plain, vertex_colors=np.tile([200, 195, 185, 255], (len(plain.vertices), 1)).astype(np.uint8)); plain.export(os.path.join(out_dir, base + "-sherd.glb"))
    if rec_known is not None: rec_known.export(os.path.join(out_dir, base + "-known.glb")); rec_ghost.export(os.path.join(out_dir, base + "-ghost.glb"))
    info = {"name": base, "rimDiameterMm": meta.get("rimDiameterMm"), "orientQuality": meta.get("quality"), "arcDeg": arc_deg, "thicknessMedian": float(np.nanmedian(th)), "thicknessMin": tmin, "thicknessMax": tmax,
            "heightMm": float(np.ptp(md.vertices[:, 1])), "profileAzimuth": prof["azimuth"] if prof else None, "profile": profile_rh, "hasReconstruction": rec_known is not None}
    json.dump(info, open(os.path.join(out_dir, base + "-info.json"), "w"))
    html = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "sherdpage.html"), encoding="utf-8").read().replace("__NAME__", base)
    open(os.path.join(out_dir, base + ".html"), "w", encoding="utf-8").write(html); print("wrote", os.path.join(out_dir, base + ".html"))

if __name__ == "__main__": main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
