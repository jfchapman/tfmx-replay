"""Trace one song through an original replayer and through TFMXreplay, then compare.

Usage: python check_song.py <profile> <mdat> [song number] [ticks] [--variant V] [--voices 0123]

Needs the trace harness (TFMX_TRACE, default ../../tfmx-trace), which runs the game's own
replayer from a WinUAE savestate, and a build of tfmxtrace (TFMX_TRACE_TOOL, default
../build/x64/Release/tfmxtrace.exe). The smpl file is found from the mdat's name.
"""
import argparse
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HARNESS = os.environ.get('TFMX_TRACE', os.path.join(HERE, '..', '..', 'tfmx-trace'))
TOOL = os.environ.get('TFMX_TRACE_TOOL', os.path.join(HERE, '..', 'build', 'x64', 'Release', 'tfmxtrace.exe'))
OUT = os.path.join(HERE, '..', 'build', 'traces')


def smpl_of(mdat):
    d, b = os.path.split(mdat)
    if b.lower().startswith('mdat.'):
        return os.path.join(d, ('SMPL' if b[0] == 'M' else 'smpl') + b[4:])
    if b.lower().endswith('.mdat'):
        return os.path.join(d, b[:-4] + ('SMPL' if b[-4] == 'M' else 'smpl'))
    raise SystemExit('can\'t find the smpl file for ' + mdat)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('profile')
    ap.add_argument('mdat')
    ap.add_argument('song', nargs='?', type=int, default=0)
    ap.add_argument('ticks', nargs='?', type=int, default=500)
    ap.add_argument('--variant', default='')
    ap.add_argument('--voices', default='0123')
    ap.add_argument('--show', type=int, default=12)
    ap.add_argument('--ignore', default='')
    args = ap.parse_args()
    args.mdat = os.path.abspath(args.mdat)
    smpl = smpl_of(args.mdat)
    os.makedirs(OUT, exist_ok=True)
    stem = '%s-%s-%d' % (args.profile, os.path.basename(args.mdat).replace(' ', '_'), args.song)
    ref = os.path.join(OUT, stem + '.ref.txt')
    ours = os.path.join(OUT, stem + '.ours.txt')
    # Reference traces are slow to make, so they're cached until the song or the harness changes.
    stale = (not os.path.exists(ref) or os.path.getsize(ref) == 0 or
             os.path.getmtime(ref) < max(os.path.getmtime(os.path.join(HARNESS, f)) for f in ('harness.py', 'player.py')) or
             os.path.getmtime(ref) < os.path.getmtime(args.mdat))
    if stale:
        try:
            with open(ref, 'w') as f:
                subprocess.run([sys.executable, 'reference.py', args.profile, args.mdat, smpl, str(args.song),
                                str(args.ticks)], cwd=HARNESS, stdout=f, check=True)
        except BaseException:
            os.remove(ref)
            raise
    with open(ours, 'w') as f:
        cmd = [TOOL, args.mdat, smpl, str(args.song), str(args.ticks)] + ([args.variant] if args.variant else [])
        subprocess.run(cmd, stdout=f, check=True)
    subprocess.run([sys.executable, os.path.join(HERE, 'compare.py'), ref, ours, '--voices', args.voices,
                    '--show', str(args.show), '--ignore', args.ignore], check=True)


if __name__ == '__main__':
    main()
