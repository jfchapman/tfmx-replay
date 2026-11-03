"""Smoke test: run every song in a folder through tfmxrender -n (open, list subsongs, and a fast
run of each subsong to find its length). Reports failures, crashes and subsongs with no end.

Usage: python smoke.py <folder> [tfmxrender path]
"""
import collections
import glob
import os
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))


def main():
    root = sys.argv[1]
    tool = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', 'build', 'x64', 'Release', 'tfmxrender.exe')
    songs = sorted(p for p in glob.glob(os.path.join(root, '**', '*'), recursive=True)
                   if os.path.basename(p).lower().startswith('mdat.') or p.lower().endswith('.mdat'))
    stats = collections.Counter()
    variants = collections.Counter()
    problems = []
    start = time.time()
    for p in songs:
        try:
            r = subprocess.run([tool, '-n', p], capture_output=True, text=True, timeout=120)
        except subprocess.TimeoutExpired:
            problems.append((p, 'timeout'))
            continue
        if r.returncode:
            problems.append((p, 'exit %d: %s' % (r.returncode, (r.stderr or r.stdout).strip()[:100])))
            continue
        stats['songs'] += 1
        for line in r.stdout.splitlines():
            if line.startswith('variant:'):
                variants[line.split(':', 1)[1].strip()] += 1
            elif ': song' in line:
                stats['subsongs'] += 1
                if 'no end found' in line:
                    stats['no end'] += 1
                    problems.append((p, line.strip()))
    print('%d songs (%d subsongs) in %.0f s; %d failed; %d subsongs with no end found' % (
        stats['songs'], stats['subsongs'], time.time() - start, len(songs) - stats['songs'], stats['no end']))
    print('variants: ' + ', '.join('%s %d' % kv for kv in variants.most_common()))
    for p, what in problems:
        print('  %s: %s' % (os.path.relpath(p, root), what))


if __name__ == '__main__':
    main()
