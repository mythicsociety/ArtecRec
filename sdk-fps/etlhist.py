"""Bin NT Kernel Logger CPU samples (tracerpt CSV) by module, per process/thread.
usage: python -I etlhist.py <k.csv> <process-name-substring> [main-thread-id]
"""
import csv, sys, collections, bisect
path, pname = sys.argv[1], sys.argv[2].lower()
main_tid = int(sys.argv[3]) if len(sys.argv) > 3 else None
images = collections.defaultdict(list)   # pid -> [(base, end, name)]
tid2pid = {}
pid_name = {}
samples = []
with open(path, encoding='utf-8-sig', errors='replace', newline='') as f:
    for row in csv.reader(f):
        if len(row) < 20: continue
        ev, typ = row[0].strip(), row[1].strip()
        ud = [x.strip() for x in row[19:]]
        try:
            if ev == 'Image' and typ in ('DCStart', 'Load'):
                base = int(ud[0], 16); size = int(ud[1], 16); pid = int(ud[2], 16) if ud[2].startswith('0x') else int(ud[2])
                name = ud[-1].strip('"').split('\\')[-1].lower()
                images[pid].append((base, base + size, name))
            elif ev == 'Process' and typ in ('DCStart', 'Start'):
                pid = int(ud[1], 16); nm = [x for x in ud if x.strip('"').lower().endswith('.exe')]
                pid_name[pid] = nm[0].strip('"').lower() if nm else '?'
            elif ev == 'Thread' and typ in ('DCStart', 'Start'):
                pid = int(ud[0], 16); tid = int(ud[1], 16); tid2pid[tid] = pid
            elif ev == 'PerfInfo' and typ == 'SampleProf':
                ip = int(ud[0], 16); tid = int(ud[1]); samples.append((ip, tid))
        except Exception:
            pass
for pid in images: images[pid].sort()
kern = images.get(0, [])
def lookup(ip, pid):
    lst = kern if ip >= 0xFFFF800000000000 else images.get(pid, [])
    i = bisect.bisect_right(lst, (ip, float('inf'), '')) - 1
    if i >= 0 and lst[i][0] <= ip < lst[i][1]: return lst[i][2]
    return 'kernel?' if ip >= 0xFFFF800000000000 else 'user?'
target = [p for p, n in pid_name.items() if pname in n]
print('samples total', len(samples), '| target pids', target, '| threads known', len(tid2pid))
for pid in target:
    proc = [(ip, tid) for ip, tid in samples if tid2pid.get(tid) == pid]
    print(f'\n=== {pid_name[pid]} pid {pid}: {len(proc)} samples ({len(proc)/15:.0f}/s = {len(proc)/15/1000:.2f} cores)')
    k = sum(1 for ip, _ in proc if ip >= 0xFFFF800000000000); print(f'  kernel {k} ({100*k/max(1,len(proc)):.0f}%)  user {len(proc)-k}')
    h = collections.Counter(lookup(ip, pid) for ip, _ in proc)
    for m, c in h.most_common(14): print(f'  {c:6d} {100*c/len(proc):5.1f}%  {m}')
    byt = collections.Counter(tid for _, tid in proc)
    print('  top threads:', byt.most_common(5))
    if main_tid:
        mt = [(ip, tid) for ip, tid in proc if tid == main_tid]
        print(f'\n  --- main thread {main_tid}: {len(mt)} samples ({len(mt)/15/1000:.2f} cores)')
        k = sum(1 for ip, _ in mt if ip >= 0xFFFF800000000000); print(f'  kernel {k} ({100*k/max(1,len(mt)):.0f}%)  user {len(mt)-k}')
        h = collections.Counter(lookup(ip, pid) for ip, _ in mt)
        for m, c in h.most_common(14): print(f'  {c:6d} {100*c/max(1,len(mt)):5.1f}%  {m}')
        # kernel-side hot offsets inside the top kernel module (helps identify the function class)
        km = [m for m, _ in h.most_common() if m.endswith('.exe') or m.endswith('.sys')][:1]
        if km:
            lst = kern; offs = collections.Counter()
            for ip, _ in mt:
                if ip >= 0xFFFF800000000000:
                    i = bisect.bisect_right(lst, (ip, float('inf'), '')) - 1
                    if i >= 0 and lst[i][2] == km[0]: offs[(ip - lst[i][0]) >> 8 << 8] += 1
            print(f'  hottest 256-byte regions in {km[0]}:', [(hex(o), c) for o, c in offs.most_common(8)])
