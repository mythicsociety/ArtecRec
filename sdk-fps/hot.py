import csv, sys, collections, bisect, struct
path, tid_main = sys.argv[1], int(sys.argv[2]); pid_t = None
images = collections.defaultdict(list); tid2pid = {}; pid_name = {}; samples = []
with open(path, encoding='utf-8-sig', errors='replace', newline='') as f:
    for row in csv.reader(f):
        if len(row) < 20: continue
        ev, typ = row[0].strip(), row[1].strip(); ud = [x.strip() for x in row[19:]]
        try:
            if ev == 'Image' and typ in ('DCStart', 'Load'):
                base = int(ud[0], 16); size = int(ud[1], 16); pid = int(ud[2], 16) if ud[2].startswith('0x') else int(ud[2])
                images[pid].append((base, base + size, ud[-1].strip('"').split(chr(92))[-1].lower()))
            elif ev == 'Thread' and typ in ('DCStart', 'Start'): tid2pid[int(ud[1], 16)] = int(ud[0], 16)
            elif ev == 'PerfInfo' and typ == 'SampleProf': samples.append((int(ud[0], 16), int(ud[1])))
        except Exception: pass
pid_t = tid2pid[tid_main]
for p in images: images[p].sort()
def find(ip, pid):
    lst = images[0] if ip >= 0xFFFF800000000000 else images[pid]
    i = bisect.bisect_right(lst, (ip, float('inf'), '')) - 1
    return lst[i] if i >= 0 and lst[i][0] <= ip < lst[i][1] else None
def exports(pe):
    d = open(pe, 'rb').read(); e_lfanew = struct.unpack_from('<I', d, 0x3c)[0]; opt = e_lfanew + 24
    nsec = struct.unpack_from('<H', d, e_lfanew + 6)[0]; optsz = struct.unpack_from('<H', d, e_lfanew + 20)[0]
    exp_rva, exp_sz = struct.unpack_from('<II', d, opt + 112)
    secs = []; so = opt + optsz
    for k in range(nsec):
        vs, va, rs, ro = struct.unpack_from('<IIII', d, so + k * 40 + 8); secs.append((va, vs, ro, rs))
    def r2o(rva):
        for va, vs, ro, rs in secs:
            if va <= rva < va + max(vs, rs): return rva - va + ro
        return None
    eo = r2o(exp_rva); nf, nn = struct.unpack_from('<II', d, eo + 20); af, an, ao = struct.unpack_from('<III', d, eo + 28)
    out = []
    for k in range(nn):
        nrva = struct.unpack_from('<I', d, r2o(an) + 4 * k)[0]; ordn = struct.unpack_from('<H', d, r2o(ao) + 2 * k)[0]
        frva = struct.unpack_from('<I', d, r2o(af) + 4 * ordn)[0]; o = r2o(nrva); nm = d[o:d.index(b'\0', o)].decode(errors='replace')
        if not (exp_rva <= frva < exp_rva + exp_sz): out.append((frva, nm))
    out.sort(); return out
mods = {'ntdll.dll': sys.argv[3], 'ntoskrnl.exe': sys.argv[4]}
ex = {m: exports(p) for m, p in mods.items()}
def sym(m, off):
    lst = ex[m]; i = bisect.bisect_right(lst, (off, '\xff')) - 1
    return f'{lst[i][1]}+0x{off - lst[i][0]:x}' if i >= 0 else f'+0x{off:x}'
mt = [ip for ip, t in samples if t == tid_main]
print('main thread samples', len(mt))
hist = collections.defaultdict(collections.Counter)
for ip in mt:
    im = find(ip, pid_t)
    if im and im[2] in mods: hist[im[2]][sym(im[2], ip - im[0])] += 1
for m in mods:
    tot = sum(hist[m].values()); print(f'\n=== {m}: {tot} samples ({100*tot/len(mt):.0f}% of main thread) — nearest export + offset')
    for s, c in hist[m].most_common(12): print(f'  {c:6d} {100*c/len(mt):5.1f}%  {s}')
