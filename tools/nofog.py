#!/usr/bin/env python3
# nofog.py <cachedir> <outroot>: write the missing $NoFog builds of a shader cache's $Fog programs.
# <cachedir> holds CGPShaders/ and CGVShaders/ (a pak's Shaders/Cache). A fragment program's NoFog build
# is its Fog build without the OPTION ARB_fog_* line (true of every pair the community pak has); a vertex
# program's Fog build also writes result.fogcoord, which a NoFog pass simply leaves unused.
import os, sys

cache, out = sys.argv[1], sys.argv[2]
counts = {}
for sub in ('CGPShaders', 'CGVShaders'):
    names = set(os.listdir(os.path.join(cache, sub)))
    os.makedirs(os.path.join(out, 'Shaders/Cache', sub), exist_ok=True)
    n = 0
    for name in sorted(names):
        nofog = name.replace('$Fog', '$NoFog', 1)
        if '$Fog' not in name or nofog in names:
            continue
        src = open(os.path.join(cache, sub, name), 'rb').read().decode('latin-1')
        if sub == 'CGPShaders':
            src = ''.join(l for l in src.splitlines(True) if not l.startswith('OPTION ARB_fog'))
        open(os.path.join(out, 'Shaders/Cache', sub, nofog), 'wb').write(src.encode('latin-1'))
        n += 1
    counts[sub] = n
print(counts)
