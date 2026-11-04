import re, os
root = r"C:\Program Files\Artec\Artec 3D Scanning SDK\bin-x64"
pat = re.compile(r'(?i)\.(json|ini|xml|cfg|conf)\b|getenv|ARTEC_|packet|config|settings|capture\.|camera\.|fly\.|property_tree|ptree|Devices|AppData|ProgramData|HKEY|Software\\\\')
for fn in ("artec-sdk-capturing.dll", "artec-sdk-flyCamera.dll"):
    d = open(os.path.join(root, fn), 'rb').read(); out = set()
    for m in re.finditer(rb'[\x20-\x7e]{5,120}', d):
        s = m.group().decode()
        if pat.search(s) and '@@' not in s and 'boost::' not in s: out.add(s)
    for m in re.finditer(rb'(?:[\x20-\x7e]\x00){5,100}', d):
        s = m.group().decode('utf-16le', errors='replace')
        if pat.search(s): out.add('W:' + s)
    print('=====', fn, len(out))
    for s in sorted(out)[:80]: print('  ', s[:140])
