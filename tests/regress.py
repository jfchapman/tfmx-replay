"""Re-run every whole-song comparison that has a cached reference trace (build/traces).

Usage: python regress.py <modland TFMX folder> [profile ...]
Finds each song's mdat under the folder by name, runs check_song.py with the profile's variant,
and prints one line per song. Micro test songs (not found under the folder) are skipped.
"""
import glob
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TRACES = os.path.join(HERE, '..', 'build', 'traces')

VARIANTS = {
    't1': '1.5', 't1-cia': '1.5', 't1-game': '1.5',
    'xout': 'xout', 'flyharder': 'xout', 'abandoned': 'xout',
    'rocknroll': 'rocknroll',
    'mb-loader': 'masterblazer', 'mb-title': 'masterblazer', 'mb-select': 'masterblazer',
    'zout-title': 'masterblazer',
    'apidya': 'pro', 'apidya7v': 'pro', 'bckid7v': 'pro', 'bckid7v-hi': 'pro',
    't3': 'pro', 't3-menu': 'pro', 't3-small': 't3small', 't3-game': 'pro',
    'carllewis7v': 'pro', 'mlaw7v': 'pro', 'mlaw': 'pro', 'hexuma': 'hexuma', 't2': 'turrican2', 't2-small': 'turrican2', 'bckid': 'pro', 'blade': 'pro',
    'gemx': 'gemx', 'monkey': 'gemx', 'monkey-cia': 'gemx',
}
# 7V: hardware voice 3 plays the mix of voices 4-7, which Paula writes can't compare.
VOICES = {'apidya7v': '012', 't2': '012', 'bckid7v': '012', 'bckid7v-hi': '012', 't3': '012', 'carllewis7v': '012',
          'mlaw7v': '012'}
# Known differences that come from the savestate, not the replayer.
IGNORE = {('mb-select', 'mdat.masterblazer title'): 'VOL'}

# WinUAE captures of the running games (tfmx-trace/*-capture*.txt, see capture.py): the most
# direct reference, and the one that counts where the harness's timing differs (7V 1A waits).
# (capture, replayer PCs, smpl address, mdat name, subsong, variant, voices)
CAPTURES = [
    ('hexuma-capture.txt', 'C30000', 'C50000', '20798', 'mdat.hexuma-title', 0, 'hexuma', '012'),
    ('bundesliga-capture2.txt', '31000', '3A900', '3BAF0', 'mdat.bundesliga manager-title', 0, 'pro', '012'),
    ('anstoss-capture1.txt', '42900', '4B700', '2559E', 'mdat.anstoss-titel', 0, 'pro', '012'),
    ('anstoss-capture2.txt', '42900', '4B700', '2559E', 'mdat.anstoss-titel', 0, 'pro', '012'),
    ('apidya-capture1.txt', '5C00', 'E000', '343F2', 'mdat.apidya (title)', 0, 'pro', '012'),
    ('apidya-capture2.txt', '5C00', 'E000', '343F2', 'mdat.apidya (title)', 0, 'pro', '012'),
    ('apidya-capture3.txt', '5C00', 'E000', '343F2', 'mdat.apidya (title)', 0, 'pro', '012'),
]
CAPTURE_DIR = os.path.join(HERE, '..', '..', 'tfmx-trace')
TFMXTRACE = os.path.join(HERE, '..', 'build', 'x64', 'Release', 'tfmxtrace.exe')


def check_captures(songs):
    for cap, pc0, pc1, smpl, name, sub, variant, voices in CAPTURES:
        path = os.path.join(CAPTURE_DIR, cap)
        mdat = songs.get(name.lower())
        if not os.path.exists(path) or not mdat:
            continue
        ref = os.path.join(TRACES, 'cap-' + cap.replace('.txt', '') + '.ref.cap')
        ours = os.path.join(TRACES, 'cap-' + cap.replace('.txt', '') + '.ours.cap')
        with open(ref, 'w') as f:
            subprocess.run([sys.executable, os.path.join(HERE, 'capture.py'), path, pc0, pc1, '--smpl', smpl],
                           stdout=f, check=True)
        smpl_file = mdat.replace('mdat.', 'smpl.')
        with open(ours, 'w') as f:
            subprocess.run([TFMXTRACE, mdat, smpl_file, str(sub), '3000', variant], stdout=f, check=True)
        r = subprocess.run([sys.executable, os.path.join(HERE, 'compare.py'), ref, ours, '--voices', voices,
                            '--search', '2500', '--show', '1'], capture_output=True, text=True)
        lines = r.stdout.strip().splitlines()
        print('%-12s %-36s %s  %s' % ('capture', cap, lines[0] if lines else '?',
                                     lines[1].strip() if len(lines) > 1 else ''))


def main():
    root = sys.argv[1]
    only = set(sys.argv[2:])
    songs = {os.path.basename(p).lower(): p for p in glob.glob(os.path.join(root, '**', 'mdat.*'), recursive=True)}
    for ref in sorted(glob.glob(os.path.join(TRACES, '*.ref.txt'))):
        stem = os.path.basename(ref)[:-8]
        m = re.match(r'(.+?)-(mdat\..+)-(\d+)$', stem)
        if not m:
            continue
        prof, name, sub = m.group(1), m.group(2).replace('_', ' '), m.group(3)
        if only and prof not in only:
            continue
        mdat = songs.get(name.lower())
        if not mdat or prof not in VARIANTS:
            continue
        cmd = [sys.executable, os.path.join(HERE, 'check_song.py'), prof, mdat, sub, '1500',
               '--variant', VARIANTS[prof], '--show', '1']
        if prof in VOICES:
            cmd += ['--voices', VOICES[prof]]
        if (prof, name) in IGNORE:
            cmd += ['--ignore', IGNORE[(prof, name)]]
        r = subprocess.run(cmd, capture_output=True, text=True)
        lines = (r.stdout + r.stderr).strip().splitlines()
        print('%-12s %-36s %s  %s' % (prof, name + ('' if sub == '0' else ' #' + sub), lines[0] if lines else '?',
                                     lines[1].strip() if len(lines) > 1 else ''))
    if not only or 'capture' in only:
        check_captures(songs)


if __name__ == '__main__':
    main()
