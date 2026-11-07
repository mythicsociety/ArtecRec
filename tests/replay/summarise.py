"""Summarises a replay-bench CSV (rows may have 16, 17 or 19 columns from older builds; the last column is the result).
Usage: python summarise.py D:\\ArtecReplay\\results\\bench-overlap2.csv"""
import csv, sys, collections
rows = [r for r in csv.reader(open(sys.argv[1], encoding='utf-8')) if r and r[0] != 'project' and not r[0].startswith('#')]
seen = set(); uniq = []
for r in rows:
    k = (r[0], r[1])
    if k in seen: continue   # a project benchmarked twice: the first run counts
    seen.add(k); uniq.append(r)
def res(r): return r[-1]
def base(r): return res(r).split('+')[0].replace('?TRUTH', '')
print('rows', len(rows), 'unique passes', len(uniq))
print('verdicts:', dict(collections.Counter(base(r) for r in uniq)))
place = [r for r in uniq if base(r) != 'NO_OVERLAP_SAVED']
clean = [r for r in place if '?TRUTH' not in res(r)]
print('placeable', len(place), ' with a sound saved reference', len(clean))
print('  of those correct (within 5 mm):', sum(1 for r in clean if base(r) in ('JOINED_OK', 'ALIGNED_OK')), ' wrong:', sum(1 for r in clean if base(r) in ('JOINED_WRONG', 'ALIGNED_WRONG')), ' not placed:', sum(1 for r in clean if base(r) == 'NOT_PLACED'))
print('live joins:', sum(1 for r in place if r[5] == '1'), 'of', len(place), ' (tries/seconds for the joined ones:', [(r[6], r[7]) for r in place if r[5] == '1'], ')')
mo = [r for r in place if 'MODEL_OK' in res(r)]; mf = [r for r in place if 'MODEL_OFF' in res(r)]
print('finished-model check: on the model', len(mo), ' off', len(mf), ' (no model:', len(place) - len(mo) - len(mf), ')')
print('anchors moved:', sum(1 for r in uniq if 'ANCHOR_MOVED' in res(r)))
sec = [r for r in uniq if len(r) >= 19 and r[16] not in ('-1.0', '') and float(r[16]) >= 0]
print('second opinion (live-joined passes only):', len(sec))
for r in sec: print('   %-30s scan %s  live err %6s mm   second err %5s mm   apart %5s mm   %s' % (r[0][:30], r[1], r[9], r[16], r[17], res(r)))
