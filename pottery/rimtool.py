# rimtool — one-click rim orientation, local web tool (8 Nov). Serves a viewer page; the user clicks the rim lip,
# the server finds the lip, fits the rim circle, and returns axis + radius; Accept writes <name>-oriented.ply + .json.
# usage: python rimtool.py <folder with sherd meshes> [port]        then open http://localhost:8765
import sys, os, glob, json, threading, numpy as np, open3d as o3d
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from scipy.spatial import cKDTree
from scipy.sparse import csr_matrix
import rimclick as rc

ROOT = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else "."; PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 8765
HERE = os.path.dirname(os.path.abspath(__file__)); OUT = os.path.join(ROOT, "_oriented"); os.makedirs(OUT, exist_ok=True)
files = sorted(f for f in glob.glob(os.path.join(ROOT, "**", "*.stl"), recursive=True) + glob.glob(os.path.join(ROOT, "**", "*.ply"), recursive=True)
               if "texture" not in os.path.basename(f).lower() and os.sep + "_oriented" + os.sep not in f)
cache = {}; lock = threading.Lock()

def prep(i):
    with lock:
        if i in cache: return cache[i]
    V, F, N, c, area = rc.prepare(files[i], target=60000); kmin, kmax, dmax, ok = rc.shape_operators(c, N); tree = cKDTree(c)
    _, nn = tree.query(c, k=13); rows = np.repeat(np.arange(len(c)), 12); G = csr_matrix((np.ones(len(rows), bool), (rows, nn[:, 1:].ravel())), shape=(len(c), len(c)))
    d = dict(V=V, F=F, N=N, c=c, area=area, kmax=kmax, dmax=dmax, ok=ok, tree=tree, graph=G)
    with lock: cache[i] = d
    if i + 1 < len(files) and (i + 1) not in cache: threading.Thread(target=prep, args=(i + 1,), daemon=True).start()   # warm the next piece
    return d

def fit(d, pts):
    """pts: 3+ clicked points on the rim top. Method = 'stand the sherd on its rim': the rim top is perpendicular to the
    vessel axis, so the axis direction is the surface normal at the clicked rim-top points (made perpendicular to the
    rim line through the clicks). The axis position (= rim radius) then comes from the wall: seen along the axis, the
    wall normals all point at the axis, a well-conditioned 2-D intersection even for a short rim arc. The circle through
    the clicks alone is reported for comparison and used as a fallback when the wall is flat (nothing to intersect)."""
    V, F, N, c, area, kmax, ok, tree = (d[k] for k in ("V", "F", "N", "c", "area", "kmax", "ok", "tree"))
    pts = np.asarray(pts, float)
    if len(pts) < 3: return {"error": "Click three points on the rim top, spread as far apart as you can."}
    m = pts.mean(0); _, sv, vt = np.linalg.svd(pts - m); t = vt[0]; chord = max(np.linalg.norm(pts[i] - pts[j]) for i in range(len(pts)) for j in range(i))
    if chord < 15: return {"error": "The clicks are too close together. Spread them along the rim."}
    ctr0, nrm0, r0, _ = rc.fit_circle_3d(pts); body = c.mean(0)
    # 1) axis direction = rim-top stance: mean surface normal around the clicks (faces within 3 mm, facing like the hit face)
    nn = []
    for q in pts:
        ids = np.asarray(tree.query_ball_point(q, 3.0)); _, i0 = tree.query(q); ids = ids[(N[ids] @ N[i0]) > 0.5] if len(ids) else np.array([i0]); nn.append((N[ids] * area[ids][:, None]).sum(0))
    d0 = np.sum(nn, axis=0); d0 /= np.linalg.norm(d0) + 1e-12; up = d0 - (d0 @ t) * t; up /= np.linalg.norm(up) + 1e-12
    stance_tilt = float(np.degrees(np.arccos(np.clip(abs(d0 @ up), 0, 1))))               # how much the normals leaned along the rim line
    # 2) axis position: in the rim plane, the wall normals point at the axis. Faces within 40 mm of the rim line, excluding rim-top faces.
    order = np.argsort((pts - m) @ t); P = pts[order]; dpoly = np.full(len(c), np.inf)
    for a, b in zip(P[:-1], P[1:]):
        ab = b - a; tt = np.clip(((c - a) @ ab) / max(ab @ ab, 1e-9), 0, 1); dpoly = np.minimum(dpoly, np.linalg.norm(c - (a + tt[:, None] * ab), axis=1))
    # genuine wall only: gently curved faces (radius > 33 mm) at least 10 mm away from the rim line, so the rim bead's own
    # rounded sides (whose normals converge on the bead core, not the axis) cannot pull the answer
    Pp = np.eye(3) - np.outer(up, up); S = np.flatnonzero(ok & (dpoly < 45.0) & (dpoly > 10.0) & (np.abs(kmax) < 0.03)); n2 = N[S] @ Pp.T; n2n = np.linalg.norm(n2, axis=1); wall = n2n > 0.6
    S = S[wall]; n2 = n2[wall] / n2n[wall][:, None]; c2 = c[S] @ Pp.T; w = area[S]
    e1 = np.cross(up, t); e1 /= np.linalg.norm(e1) + 1e-12; e2 = t                        # 2-D frame in the rim plane
    def solve(w):
        Q = Pp[None] - n2[:, :, None] * n2[:, None, :]; M = (w[:, None, None] * Q).sum(0) + 1e-9 * np.eye(3); b = (w[:, None] * (Q @ c2[:, :, None])[:, :, 0]).sum(0); return np.linalg.lstsq(M, b, rcond=None)[0], M
    px = None; cond2 = 0.0
    if len(S) > 100:
        px, M = solve(w)
        for it in range(4):
            cr = np.cross(n2, up); dist = np.abs(((c2 - px) * cr).sum(1)); s = max(np.median(dist) * 1.5, 0.5); w = area[S] / (1.0 + (dist / s) ** 2); px, M = solve(w)
        M2 = np.array([[e1 @ M @ e1, e1 @ M @ e2], [e2 @ M @ e1, e2 @ M @ e2]]); ev = np.linalg.eigvalsh(M2); cond2 = float(ev[0] / max(ev[1], 1e-12))
        cr = np.cross(n2, up); dist = np.abs(((c2 - px) * cr).sum(1)); inl = dist < 3.0; resid_mm = float(np.median(dist))
    use_wall = px is not None and cond2 > 0.08 and len(S) > 300
    if use_wall:
        axis_pt = px + up * float(m @ up); r_axis = float(np.linalg.norm((m - axis_pt) @ Pp.T)); src = "wall"
    else:
        # keep the click-circle radius, but put the axis in the stance plane: inward = perpendicular to the rim line and to 'up', towards the body
        inward = np.cross(up, t); inward /= np.linalg.norm(inward) + 1e-12
        if ((body - m) @ inward) < 0: inward = -inward
        axis_pt = m + r0 * inward; r_axis = float(r0); src = "clicks"; inl = np.zeros(len(S), bool); resid_mm = float("nan")
    # up sign: the body hangs below the rim
    if ((m - body) @ up) < 0: up = -up
    ctr = axis_pt + up * float((m - axis_pt) @ up)
    # outer rim radius from rim-top faces (within 8 mm of the rim line)
    top = np.flatnonzero(dpoly < 8.0); q = c[top] - ctr; rad = np.linalg.norm(q - (q @ up)[:, None] * up[None], axis=1); rim_r = float(np.percentile(rad, 95)) if len(top) > 20 else r_axis
    u = np.cross(up, [1, 0, 0]); u = np.cross(up, [0, 1, 0]) if np.linalg.norm(u) < 0.3 else u; u /= np.linalg.norm(u); v = np.cross(up, u)
    qq = c[top] - ctr; ang = np.sort(np.degrees(np.arctan2(qq @ v, qq @ u))); gaps = np.diff(np.r_[ang, ang[0] + 360]); arc = float(360 - gaps.max()) if len(ang) > 2 else 0.0
    frac = arc / 360.0; sag_ok = sv[1] > 1.5                                               # clicks visibly curved (sagitta > 1.5 mm)
    print(f"fit: clicks r0 {r0:.1f} chord {chord:.0f} sag {sv[1]:.1f}; stance tilt {stance_tilt:.0f}; wall faces {len(S)} cond {cond2:.3f} -> src {src} r_axis {r_axis:.1f} rim_r {rim_r:.1f} resid {resid_mm:.2f} arc {arc:.0f}", flush=True)
    quality = "good" if (src == "wall" and resid_mm < 1.5 and arc > 20) else ("check" if (src == "wall" or sag_ok) else "poor")
    note = []
    if src == "clicks": note.append("The wall is too flat to fix the diameter; it comes from your 3 clicks only" + (" and the clicks are nearly in a straight line, so treat the diameter as unknown." if not sag_ok else "."))
    elif abs(rim_r - r0) > 0.3 * max(r0, rim_r): note.append(f"Circle through the clicks alone would give {2*r0:.0f} mm.")
    if stance_tilt > 20: note.append("The rim top leans strongly along the rim line; check the axis against the wall.")
    lip = S[inl].tolist() if src == "wall" else top.tolist()
    return {"centre": ctr.tolist(), "up": up.tolist(), "radius": rim_r, "diameter": 2 * rim_r, "residual": resid_mm if resid_mm == resid_mm else 0.0, "arcDeg": arc,
            "clickRadius": float(r0), "source": src, "lip": lip, "quality": quality, "note": " ".join(note)}

def orient_and_save(i, d, r):
    ctr = np.array(r["centre"]); y = np.array(r["up"]); x = np.cross([0, 0, 1], y); x = np.cross([1, 0, 0], y) if np.linalg.norm(x) < 0.3 else x; x /= np.linalg.norm(x); z = np.cross(x, y)
    R = np.vstack([x, y, z]); m = o3d.io.read_triangle_mesh(files[i]); V = (np.asarray(m.vertices) - ctr) @ R.T   # full-resolution mesh, rim centre at origin, axis = +Y
    m.vertices = o3d.utility.Vector3dVector(V); base = os.path.splitext(os.path.basename(files[i]))[0]
    o3d.io.write_triangle_mesh(os.path.join(OUT, base + "-oriented.ply"), m, write_ascii=False)
    meta = {"source": files[i], "rimCentre": r["centre"], "axisUp": r["up"], "rimDiameterMm": r["diameter"], "residualMm": r["residual"], "arcDeg": r["arcDeg"], "quality": r["quality"], "rotation": R.tolist(), "method": "rimclick one-click (8 Nov 2026)"}
    json.dump(meta, open(os.path.join(OUT, base + "-oriented.json"), "w"), indent=1); return base + "-oriented.ply"

def build_page(i):
    try:
        import subprocess; base = os.path.splitext(os.path.basename(files[i]))[0]
        subprocess.run([sys.executable, os.path.join(HERE, "sherdpage.py"), os.path.join(OUT, base + "-oriented.ply"), os.path.join(OUT, "pages")], check=True, capture_output=True, timeout=1800)
    except Exception as e: print("page build failed:", e, flush=True)

def mesh_bytes(d):
    V = d["V"].astype(np.float32); F = d["F"].astype(np.uint32); hdr = np.array([len(V), len(F)], np.uint32).tobytes(); return hdr + V.tobytes() + F.tobytes()

class H(BaseHTTPRequestHandler):
    def log_message(self, *a): pass
    def send(self, code, body, ctype="application/json"):
        self.send_response(code); self.send_header("Content-Type", ctype); self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_GET(self):
        p = self.path.split("?")[0]
        if p == "/": return self.send(200, open(os.path.join(HERE, "rimtool.html"), "rb").read(), "text/html; charset=utf-8")
        if p == "/list":
            done = {os.path.splitext(f)[0].replace("-oriented", "") for f in os.listdir(OUT) if f.endswith("-oriented.json")}
            pages = {f[:-5] for f in os.listdir(os.path.join(OUT, "pages")) if f.endswith(".html")} if os.path.isdir(os.path.join(OUT, "pages")) else set()
            return self.send(200, json.dumps([{"i": i, "name": os.path.splitext(os.path.basename(f))[0], "done": os.path.splitext(os.path.basename(f))[0] in done, "page": os.path.splitext(os.path.basename(f))[0] in pages} for i, f in enumerate(files)]).encode())
        if p.startswith("/pages/"):
            import urllib.parse, mimetypes; f = os.path.join(OUT, "pages", urllib.parse.unquote(p[7:]).replace("/", os.sep))
            if not os.path.isfile(f): return self.send(404, b"not built yet", "text/plain")
            return self.send(200, open(f, "rb").read(), mimetypes.guess_type(f)[0] or "application/octet-stream")
        if p.startswith("/mesh/"):
            i = int(p[6:]); d = prep(i); return self.send(200, mesh_bytes(d), "application/octet-stream")
        return self.send(404, b"not found", "text/plain")
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0)); body = json.loads(self.rfile.read(n) or b"{}"); i = int(body["i"]); d = prep(i)
        if self.path == "/fit": return self.send(200, json.dumps(fit(d, body["points"])).encode())
        if self.path == "/accept":
            saved = orient_and_save(i, d, body["result"]); threading.Thread(target=build_page, args=(i,), daemon=True).start(); return self.send(200, json.dumps({"saved": saved}).encode())
        if self.path == "/skip":
            base = os.path.splitext(os.path.basename(files[i]))[0]; json.dump({"source": files[i], "skipped": body.get("reason", "no rim")}, open(os.path.join(OUT, base + "-oriented.json"), "w")); return self.send(200, b"{}")
        return self.send(404, b"{}")

if __name__ == "__main__":
    print(f"{len(files)} meshes under {ROOT}; results in {OUT}\nopen http://localhost:{PORT}"); ThreadingHTTPServer(("127.0.0.1", PORT), H).serve_forever()
