"""Aggregate StackWalk events from tracerpt CSV for one thread; name frames by module + nearest export.
usage: python -I stacks.py <s.csv> <tid> <dir-with-ntdll.dll,ntoskrnl.exe,kernelbase.dll copies>
"""
import csv, sys, collections, bisect, struct, os
path, tid_main, symdir = sys.argv[1], int(sys.argv[2]), sys.argv[3]
images = collections.defaultdict(list); tid2pid = {}; stacks = []
with open(path, encoding='utf-8-sig', errors='replace', newline='') as f:
    for row in csv.reader(f):
        if len(row) < 20: continue
        ev, typ = row[0].strip(), row[1].strip(); ud = [x.strip() for x in row[19:]]
        try:
            if ev == 'Image' and typ in ('DCStart', 'Load', 'DCEnd'):
                base = int(ud[0], 16); size = int(ud[1], 16); pid = int(ud[2], 16) if ud[2].startswith('0x') else int(ud[2])
                images[pid].append((base, base + size, ud[-1].strip('"').split(chr(92))[-1].lower()))
            elif ev == 'Thread' and typ in ('DCStart', 'Start'): tid2pid[int(ud[1], 16)] = int(ud[0], 16)
            elif ev == 'StackWalk' and typ == 'Stack':
                pid = int(ud[1], 16); tid = int(ud[2])
                if tid == tid_main: stacks.append((int(ud[0]), pid, [int(x, 16) for x in ud[3:] if x.startswith('0x')]))
        except Exception: pass
for p in images: images[p] = sorted(set(images[p]))
def find(ip, pid):
    lst = images[0] if ip >= 0xFFFF800000000000 else images.get(pid, [])
    i = bisect.bisect_right(lst, (ip, float('inf'), '')) - 1
    return lst[i] if i >= 0 and lst[i][0] <= ip < lst[i][1] else None
def exports(pe):
    d = open(pe, 'rb').read(); e = struct.unpack_from('<I', d, 0x3c)[0]; opt = e + 24
    nsec = struct.unpack_from('<H', d, e + 6)[0]; optsz = struct.unpack_from('<H', d, e + 20)[0]
    exp_rva, exp_sz = struct.unpack_from('<II', d, opt + 112); secs = []; so = opt + optsz
    for k in range(nsec):
        vs, va, rs, ro = struct.unpack_from('<IIII', d, so + k * 40 + 8); secs.append((va, vs, ro, rs))
    def r2o(rva):
        for va, vs, ro, rs in secs:
            if va <= rva < va + max(vs, rs): return rva - va + ro
    eo = r2o(exp_rva); nf, nn = struct.unpack_from('<II', d, eo + 20); af, an, ao = struct.unpack_from('<III', d, eo + 28); out = []
    for k in range(nn):
        nrva = struct.unpack_from('<I', d, r2o(an) + 4 * k)[0]; ordn = struct.unpack_from('<H', d, r2o(ao) + 2 * k)[0]
        frva = struct.unpack_from('<I', d, r2o(af) + 4 * ordn)[0]; o = r2o(nrva); nm = d[o:d.index(b'\0', o)].decode(errors='replace')
        if not (exp_rva <= frva < exp_rva + exp_sz): out.append((frva, nm))
    out.sort(); return out
ex = {}
for fn in os.listdir(symdir):
    if fn.lower().endswith(('.dll', '.exe')): ex[fn.lower().replace('-laptop', '')] = exports(os.path.join(symdir, fn))
def name(ip, pid):
    im = find(ip, pid)
    if not im: return 'kernel?' if ip >= 0xFFFF800000000000 else '?'
    m = im[2]; off = ip - im[0]
    if m in ex:
        lst = ex[m]; i = bisect.bisect_right(lst, (off, '\xff')) - 1
        if i >= 0 and off - lst[i][0] < 0x4000: return f'{m}!{lst[i][1]}+0x{off - lst[i][0]:x}'
    return f'{m}+0x{off:x}'
# merge kernel+user stack pieces with the same timestamp
by_ts = collections.defaultdict(list)
for ts, pid, fr in stacks: by_ts[ts].append((pid, fr))
merged = []
for ts, parts in by_ts.items():
    pid = parts[0][0]; frames = []
    for p, fr in sorted(parts, key=lambda x: (0 if x[1] and x[1][0] >= 0xFFFF800000000000 else 1)): frames += fr
    merged.append((pid, frames))
print('stack samples for thread', tid_main, ':', len(merged))
SYS = ('ntoskrnl', 'ntdll', 'kernelbase', 'kernel32', 'win32u', 'kernel?')
first_user = collections.Counter(); callers = collections.Counter(); full = collections.Counter()
for pid, fr in merged:
    names = [name(ip, pid) for ip in fr]
    full[' <- '.join(n.split('+')[0] for n in names[:12])] += 1
    fu = [n for n in names if not n.startswith(SYS)]
    if fu:
        first_user[fu[0]] += 1
        callers[' <- '.join(fu[:4])] += 1
print('\n=== first non-system frame (who called into ntdll/kernel)')
for s, c in first_user.most_common(15): print(f'  {c:6d} {100*c/len(merged):5.1f}%  {s}')
print('\n=== top caller chains (first 4 non-system frames)')
for s, c in callers.most_common(12): print(f'  {c:6d} {100*c/len(merged):5.1f}%  {s}')
print('\n=== top full stacks (module!export, 12 frames)')
for s, c in full.most_common(8): print(f'  {c:6d}  {s}')
