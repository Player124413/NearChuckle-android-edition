#!/usr/bin/env python3
# cgbuild.py <dumpdir> <outroot> [cgc] : compile FARCRY_CG_DUMP sources into shader cache files
# (<outroot>/Shaders/Cache/...), with the options, header and edits the engine applies.
# Needs NVIDIA's cgc 3.1 (Cg Toolkit, Intel-only: runs under Rosetta); path from $CGC or the argument.
# Zip <outroot>/Shaders into the GL shader cache pak.
import os, re, subprocess, sys
dump, out = sys.argv[1], sys.argv[2]
cgc = sys.argv[3] if len(sys.argv) > 3 else os.environ.get('CGC', 'cgc')
OPTS = {  # what cgGLSetOptimalOptions gave the machine that built GL_Shaders_20260517.pak
  'arbvp1': 'NumTemps=32,MaxInstructions=4096,MaxAddressRegs=1,MaxLocalParams=2048',
  'arbfp1': 'ARB_draw_buffers,NumTemps=256,NumInstructionSlots=16384,NumTexInstructionSlots=16384,'
            'NumMathInstructionSlots=16384,MaxTexIndirections=16384,MaxLocalParams=2048,MaxDrawBuffers=4',
}
VER = '//CGVER3.4\n'
ok = fail = 0
for f in sorted(os.listdir(dump)):
    if not f.endswith('.cg'): continue
    src = open(os.path.join(dump, f), encoding='latin-1').read()
    m = re.match(r'//CGDUMP profile=(\S+) fog=(\S+) target=(.+)\n', src)
    if not m or m.group(1) not in OPTS:
        print('skip', f); fail += 1; continue
    prof, fog, target = m.groups()
    r = subprocess.run(['arch', '-x86_64', cgc, '-q', '-no_uniform_blocks', '-profile', prof, '-entry', 'main',
                        '-po', OPTS[prof], '-DCGC=1', os.path.join(dump, f)], capture_output=True, text=True, encoding='latin-1')
    code = r.stdout
    if r.returncode or '!!ARB' not in code:
        print('FAIL', f, r.stderr.strip()[:400]); fail += 1; continue
    code = code.replace('program.local', '  program.env')  # as the engine's memcpy over the same 13 chars
    if fog != '-':
        i = code.index('\n', code.index('!!ARBfp1.0')) + 1
        code = code[:i] + 'OPTION %s;\n' % fog + code[i:]
    dst = os.path.join(out, target)
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    open(dst, 'w', encoding='latin-1', newline='').write(VER + code)
    ok += 1
print('compiled %d, failed %d' % (ok, fail))
