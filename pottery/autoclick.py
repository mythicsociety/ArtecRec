# autoclick — stand-in for the user's three rim clicks, for demos and tests only: picks three points along the rim bead
# crest (sharpest convex band) and runs the same fit + save as the Accept button. The owner re-does these by hand.
# usage: autoclick.py <mesh name suffix> [more names...]
import sys, os, json, numpy as np; names = sys.argv[1:]; sys.argv = [sys.argv[0], "D:/Pottery/exports"]; import rimtool as rt
for name in names:
    i = [k for k, f in enumerate(rt.files) if f.replace("\\", "/").endswith(name)][0]; d = rt.prep(i); c, kmax, ok, N = d["c"], d["kmax"], d["ok"], d["N"]
    bead = ok & (kmax > 0.07) & (kmax < 0.25)
    if bead.sum() < 50: print(name, "no bead found"); continue
    P = c[bead]; t = np.linalg.svd(P - P.mean(0))[2][0]; proj = (P - P.mean(0)) @ t
    # three points: 10th, 50th, 90th percentile along the bead, each the local median position of nearby bead faces
    pts = []
    for q in (10, 50, 90):
        v = np.percentile(proj, q); near = np.abs(proj - v) < 3.0; pts.append(np.median(P[near], axis=0) if near.sum() > 3 else P[np.argmin(np.abs(proj - v))])
    r = rt.fit(d, pts)
    if "error" in r: print(name, "fit error:", r["error"]); continue
    saved = rt.orient_and_save(i, d, r); print(f"{name}: diameter {r['diameter']:.0f} mm, arc {r['arcDeg']:.0f}, quality {r['quality']}, source {r['source']} -> {saved}; {r['note']}")
    rt.build_page(i); print("  page built")
