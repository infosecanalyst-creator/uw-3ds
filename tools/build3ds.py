#!/usr/bin/env python3
"""build3ds.py: builds Underworld Exhumed's UW1 or UW2 port for the New 3DS with devkitARM.

Reads the game's exhume.toml for its source tree and flags (the same rules as Exhume's
portcfg.py: every .C of the game but dos-only ones, plus replay.c; every .c of the game's
src/port and the runtime's port/ but stand-in headers and other platform backends), swaps the
SDL3 backend for ours (3ds/platform), adds the 3DS front end (3ds/front), and links with
libctru/citro2d into a .3dsx.

    python3 tools/build3ds.py uw1 [--jobs N] [--clean]
"""
import os, sys, glob, json, shutil, argparse, subprocess, tomllib
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
TOP = os.path.normpath(os.path.join(HERE, '..'))
UP = os.path.join(TOP, 'upstream')
DKP = os.environ.get('DEVKITPRO', '/opt/devkitpro')
ARM = os.path.join(DKP, 'devkitARM', 'bin')
CC = os.path.join(ARM, 'arm-none-eabi-gcc')
CTRU = os.path.join(DKP, 'libctru')
PORTLIBS = os.path.join(DKP, 'portlibs', '3ds')
TOOLS = os.path.join(DKP, 'tools', 'bin')

ARCH = ['-march=armv6k', '-mtune=mpcore', '-mfloat-abi=hard', '-mtp=soft', '-mword-relocations']
LTO = [] if os.environ.get('NO_LTO') else ['-flto=auto']
LTO_DEFAULT = r'^upstream/(uw1|uw2)/src/(?!port/mem)|^upstream/exhume/'
COMMON = ARCH + ['-D__3DS__', '-DPORT_FRAME_SINGLE', '-ffunction-sections', '-fno-strict-aliasing',
                 '-fsigned-char', '-I', os.path.join(CTRU, 'include'), '-I', os.path.join(PORTLIBS, 'include')]


def walk_c(top, skip_dirs, plat, backend_keep):
    out = []
    for d, _, fs in os.walk(top):
        if any(d == s or d.startswith(s + os.sep) for s in skip_dirs): continue
        if d.startswith(plat + os.sep) and os.path.relpath(d, plat).split(os.sep)[0] != backend_keep: continue
        out += [os.path.join(d, f) for f in sorted(fs) if f.endswith('.c')]
    return out


def config(game):
    groot = os.path.join(UP, game)
    toml = os.path.join(groot, 'exhume.toml')
    if os.path.exists(toml):
        raw = tomllib.load(open(toml, 'rb'))
    else:
        # UW2 predates exhume.toml: its own tools/portbuild.py has the same rules, with the
        # translated modules, their machine and the graphics C optimised, and the FM chips'
        # emulator for the runtime's sound/audio.c
        raw = {'project': {'exclude': ['port']},
               'port': {'optimise': ['3d', 'gfx', 'x86'],
                        'vendor': [{'define': 'AUDIO_HAVE_OPL', 'for': 'sound/audio.c'}]}}
    pj, pt = raw.get('project', {}), raw.get('port', {})
    src = os.path.join(groot, 'src')
    include = os.path.join(groot, pj['include']) if pj.get('include') else os.path.join(src, 'include')
    runtime = os.path.join(UP, 'exhume', 'runtime')
    rtp = os.path.join(runtime, 'port')
    pdir = os.path.join(groot, pt.get('dir', 'src/port'))
    stand_ins = os.path.join(rtp, 'include')
    exclude = [os.path.join(src, x) for x in pj.get('exclude', [])]
    # the game's C (sources.all_sources, game_sources)
    game_c = []
    for p in sorted(set(glob.glob(os.path.join(src, '**', '*.C'), recursive=True) +
                        glob.glob(os.path.join(src, '**', '*.c'), recursive=True))):
        if any(os.path.commonpath([p, d]) == d for d in [include] + exclude): continue
        if pt.get('dos_only', 'port: dos-only') in open(p, encoding='latin1').read(4000): continue
        game_c.append(p)
    for d in pt.get('shared', []):
        game_c += sorted(glob.glob(os.path.join(groot, d, '*.[cC]')))
    game_c.append(os.path.join(runtime, 'replay', 'replay.c'))
    # the port's C, with no desktop backend (ours is added below)
    port_c = []
    for top in (pdir, rtp):
        port_c += walk_c(top, [stand_ins, os.path.join(top, 'include')], os.path.join(top, 'platform'), '3ds')
    defines = list(pt.get('defines', ['-D_POSIX_C_SOURCE=200809L']))
    quote = [include, os.path.join(runtime, 'include')]
    inc_dirs = [pdir, rtp, os.path.join(rtp, 'platform'), stand_ins, os.path.join(rtp, 'sound'), os.path.join(rtp, 'stubs')]
    optimise = [os.path.join(t, x) + os.sep for t in (pdir, rtp) for x in pt.get('optimise', [])]
    vendor_def = {v['define']: v['for'] for v in pt.get('vendor', [])}
    sound_ext = [os.path.join(groot, x) for x in raw.get('sound', {}).get('extensions', [])
                 if not os.path.join(groot, x).startswith(pdir + os.sep)]
    return dict(groot=groot, game_c=game_c, port_c=sorted(port_c) + sound_ext, defines=defines,
                compat=os.path.join(rtp, 'compat.h'), pdir=pdir, stand_ins=stand_ins, quote=quote,
                inc_dirs=inc_dirs, optimise=optimise, game_std=pt.get('game_std', 'gnu89'),
                port_std=pt.get('port_std', 'gnu11'), vendor_def=vendor_def, exe=pt.get('exe', 'port'))


def obj_name(out, path):
    rel = os.path.relpath(path, TOP).replace(os.sep, '_')
    return os.path.join(out, os.path.splitext(rel)[0] + '.o')


def run(cmd, cwd):
    r = subprocess.run(cmd, capture_output=True, text=True, cwd=cwd)
    return r.returncode, r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('game', choices=['uw1', 'uw2'])
    ap.add_argument('--jobs', type=int, default=os.cpu_count() or 4)
    ap.add_argument('--clean', action='store_true')
    ap.add_argument('--compile-only', action='store_true')
    a = ap.parse_args()
    C = config(a.game)
    out = os.path.join(TOP, 'build', a.game)
    if a.clean and os.path.isdir(out): shutil.rmtree(out)
    os.makedirs(out, exist_ok=True)
    gameflags = ['-x', 'c', f"-std={C['game_std']}", '-O2', '-w', '-fpermissive', '-fno-toplevel-reorder'] + COMMON + C['defines'] + [
        '-include', C['compat'], '-I', C['pdir'], '-I', C['stand_ins']] + [f for d in C['quote'] for f in ('-iquote', d)]
    ours = os.path.join(TOP, '3ds')
    portflags = ['-x', 'c', f"-std={C['port_std']}"] + COMMON + C['defines'] + [
        '-Wall', '-Wno-comment', '-Wno-unused-function', '-Wno-unknown-pragmas', '-Wno-pragma-pack'] + \
        ['-I', os.path.join(ours, 'front'), '-I', os.path.join(ours, 'platform')] + \
        [f for d in C['inc_dirs'] for f in ('-I', d)] + [f for d in C['quote'] for f in ('-iquote', d)] + \
        [f'-DGAME_{a.game.upper()}=1']
    nuked = os.path.join(TOP, 'vendor', 'nuked-opl3')
    jobs = []
    for p in C['game_c']:
        jobs.append((p, [CC] + gameflags + ['-c', '-o', obj_name(out, p), p]))
    ours_c = sorted(glob.glob(os.path.join(ours, 'platform', '*.c')) + glob.glob(os.path.join(ours, 'front', '*.c')) + glob.glob(os.path.join(ours, 'game', '*.c')))
    for p in C['port_c'] + ours_c:
        extra = []
        for define, fors in C['vendor_def'].items():
            fors = [fors] if isinstance(fors, str) else fors
            if any(p.endswith(os.sep + f.replace('/', os.sep)) for f in fors) and define == 'AUDIO_HAVE_OPL':
                extra += ['-DAUDIO_HAVE_OPL', '-I', nuked]
        opt = ['-O2']
        jobs.append((p, [CC] + portflags + opt + extra + ['-c', '-o', obj_name(out, p), p]))
    jobs.append((os.path.join(nuked, 'opl3.c'), [CC, '-x', 'c', '-std=c99', '-O2', '-w'] + COMMON +
                 ['-c', '-o', os.path.join(out, 'opl3.o'), os.path.join(nuked, 'opl3.c')]))
    # link-time optimisation where it pays and works: LTO_FILES, a regex of the sources it is
    # applied to (some translated modules' code does not survive it)
    import re as _re
    lto_re = _re.compile(os.environ.get('LTO_FILES', LTO_DEFAULT)) if LTO else None
    jobs = [(p, cmd[:1] + (LTO if lto_re and lto_re.search(os.path.relpath(p, TOP)) else []) + cmd[1:]) for p, cmd in jobs]
    with ThreadPoolExecutor(a.jobs) as ex:
        res = list(ex.map(lambda j: (j[0], j[1][-2]) + run(j[1], TOP), jobs))
    bad = [(p, e) for p, o, rc, e in res if rc]
    for p, e in bad:
        errs = [l for l in e.split('\n') if 'error' in l]
        print(f'{os.path.relpath(p, TOP)}: does not compile\n  ' + '\n  '.join(errs[:12]))
    warn = [(p, e) for p, o, rc, e in res if not rc and 'warning' in e and p.startswith(ours)]
    for p, e in warn:
        print(f'{os.path.relpath(p, TOP)}: warnings\n  ' + '\n  '.join(l for l in e.split('\n') if 'warning' in l)[:3000])
    print(f'compiled {len(res) - len(bad)} of {len(res)} sources')
    if bad or a.compile_only: return 1 if bad else 0
    objs = [o for p, o, rc, e in res]
    elf = os.path.join(out, f'{a.game}-3ds.elf')
    specs = os.path.join(DKP, 'devkitARM', 'arm-none-eabi', 'lib', '3dsx.specs')
    link = [CC, '-specs=3dsx.specs', '-g', '-O2'] + ARCH + LTO + ['-Wl,--gc-sections', '-Wl,-Map,' + elf + '.map', '-o', elf] + objs + \
        ['-L', os.path.join(CTRU, 'lib'), '-L', os.path.join(PORTLIBS, 'lib'), '-lcitro2d', '-lcitro3d', '-lctru', '-lm']
    rc, e = run(link, TOP)
    if rc:
        und = sorted(set(l.split('undefined reference to ')[-1].strip("`'") for l in e.split('\n') if 'undefined reference' in l))
        print(f'link failed: {len(und)} undefined: ' + ' '.join(und[:200]))
        print(e[-4000:])
        return 1
    print(f'linked {os.path.relpath(elf, TOP)} ({os.path.getsize(elf)} bytes)')
    title = {'uw1': 'Ultima Underworld', 'uw2': 'Ultima Underworld II'}[a.game]
    sub = {'uw1': 'The Stygian Abyss', 'uw2': 'Labyrinth of Worlds'}[a.game]
    icon = os.path.join(ours, 'meta', f'{a.game}-icon.png')
    smdh = os.path.join(out, f'{a.game}-3ds.smdh')
    smdh_cmd = [os.path.join(TOOLS, 'smdhtool'), '--create', title, sub, 'Underworld Exhumed 3DS port', icon, smdh]
    if not os.path.exists(icon): smdh_cmd[5] = os.path.join(CTRU, 'default_icon.png')
    rc, e = run(smdh_cmd, TOP)
    if rc: print('smdhtool:', e)
    x = os.path.join(out, f'{a.game}-3ds.3dsx')
    romfs = os.path.join(ours, 'romfs')
    cmd = [os.path.join(TOOLS, '3dsxtool'), elf, x, '--smdh=' + smdh]
    if os.path.isdir(romfs): cmd.append('--romfs=' + romfs)
    rc, e = run(cmd, TOP)
    if rc: print('3dsxtool:', e); return 1
    print(f'wrote {os.path.relpath(x, TOP)} ({os.path.getsize(x)} bytes)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
