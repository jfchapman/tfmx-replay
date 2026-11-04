"""Checks the reported loop points: renders two passes of every looped subsong, and compares the
audio after the loop with the audio it should be repeating from loop_start. A subsong fails if some
other alignment fits better, which means the loop point is wrong. Some songs don't repeat exactly
(a fade-in, sample positions carried over the loop): the notes are the same, so they're only counted.

Usage: python loops.py <folder> [tfmxrender path]
"""
import array
import concurrent.futures
import glob
import math
import operator
import os
import re
import subprocess
import sys
import tempfile
import wave

HERE = os.path.dirname(os.path.abspath(__file__))
RATE = 11025
SKIP = 1         # seconds to skip after the loop point, where old notes still ring
SECONDS = 3      # how much to compare
SEARCH = 1200    # frames either way to try
SLOP = 6         # frames of error allowed (about half a millisecond)
GOOD = 0.15      # difference under this fraction of the signal counts as an exact repeat
LINE = re.compile(r'^\s*(\d+): song\s+\d+, \S+ \(([\d.]+) s\), loop from ([\d.]+) s$')


def power(a):
    return sum(map(operator.mul, a, a))


def check(tool, mdat, index, end, start, tmp):
    if end - start < SKIP + 2:
        return None
    path = os.path.join(tmp, 'x%d.wav' % index)
    r = subprocess.run([tool, '-s', str(index), '-l', '1', '-r', str(RATE), '-i', 'none', '-f', 'none', '-m', '0',
                        '-o', path, mdat], capture_output=True, text=True, timeout=600)
    if r.returncode:
        return None
    with wave.open(path) as w:
        data = array.array('h', w.readframes(w.getnframes()))[0::2]
    os.remove(path)
    n = int(min(end - start - SKIP, SECONDS) * RATE)
    first = int((start + SKIP) * RATE)
    second = int((end + SKIP) * RATE)
    if second + n + SEARCH >= len(data):
        return None
    a = data[first:first + n:3]
    base = power(a)
    if base < len(a) * 4:
        return None
    best = (1e18, 0)
    for shift in range(-SEARCH, SEARCH + 1, 2):
        d = list(map(operator.sub, a, data[second + shift:second + shift + n:3]))
        best = min(best, (power(d), abs(shift)))
    return math.sqrt(best[0] / base), best[1]


def one(args):
    tool, mdat = args
    r = subprocess.run([tool, '-n', mdat], capture_output=True, text=True, timeout=600)
    out = []
    with tempfile.TemporaryDirectory() as tmp:
        for line in r.stdout.splitlines():
            m = LINE.match(line)
            if m:
                out.append((mdat, int(m[1]), float(m[2]), float(m[3]), check(tool, mdat, int(m[1]), float(m[2]), float(m[3]), tmp)))
    return out


def main():
    root = sys.argv[1]
    tool = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', 'build', 'x64', 'Release', 'tfmxrender.exe')
    songs = sorted(glob.glob(os.path.join(glob.escape(root), '**', 'mdat.*'), recursive=True))
    with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        results = [x for r in pool.map(one, [(tool, s) for s in songs]) for x in r]
    checked = [r for r in results if r[4] is not None]
    wrong = [r for r in checked if r[4][1] > SLOP and r[4][0] < GOOD]
    inexact = sum(r[4][0] >= GOOD for r in checked)
    print('%d looped subsongs, %d checked: %d exact repeats, %d similar, %d wrong' % (
        len(results), len(checked), len(checked) - inexact - len(wrong), inexact, len(wrong)))
    for mdat, index, end, start, (diff, shift) in wrong:
        print('  %s #%d (loop %.2f to %.2f): fits %d frames away' % (os.path.relpath(mdat, root), index, start, end, shift))
    sys.exit(1 if wrong else 0)


main()
