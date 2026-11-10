"""Change check: render every subsong of every song in a folder with two builds of tfmxrender and
list the subsongs whose audio differs, with how much they differ.

Use it after a change to the library, with a build from before the change (a release, or the
previous commit built in a separate folder) as the old tool. It shows which songs a change reaches;
it can't say whether the new output is right. Songs that differ only by a level shift show a
large difference overall but little above 30 Hz.

Usage: python everysong.py <folder> <old tfmxrender> [new tfmxrender] [options]
  -r <rate>      sample rate of the renders (default 22050)
  -j <n>         songs rendered at once (default: the number of processors)
  --keep <dir>   keep the differing renders there, as <song> <subsong> old/new.wav
  -- ...         everything after this goes to both tools (e.g. -- -f none -i linear)
"""
import array
import glob
import hashlib
import math
import os
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

from common import find_tool, usage


def read_wav(path):
    """Left and right channels of a 16-bit stereo WAV, as two arrays."""
    data = open(path, 'rb').read()
    pos = 12
    while pos + 8 <= len(data):
        size = int.from_bytes(data[pos + 4:pos + 8], 'little')
        if data[pos:pos + 4] == b'data':
            samples = array.array('h', data[pos + 8:pos + 8 + size])
            if sys.byteorder == 'big':
                samples.byteswap()
            return samples[0::2], samples[1::2]
        pos += 8 + size + (size & 1)
    return array.array('h'), array.array('h')


def high_pass(x, rate):
    """One pole at 30 Hz."""
    k = math.exp(-2 * math.pi * 30 / rate)
    out = [0.0] * len(x)
    prev_in = prev_out = 0.0
    for i, v in enumerate(x):
        prev_out = v - prev_in + k * prev_out
        prev_in = v
        out[i] = prev_out
    return out


def difference(old_path, new_path, rate):
    """(difference against the old render in dB, the same above 30 Hz, length change in seconds)."""
    old, new = read_wav(old_path), read_wav(new_path)
    n = min(len(old[0]), len(new[0]))
    err = sig = err_hp = sig_hp = 0.0
    for a, b in zip(old, new):
        a, b = a[:n], b[:n]
        for x, y in zip(a, b):
            err += (y - x) ** 2
            sig += x * x
        for x, y in zip(high_pass(a, rate), high_pass(b, rate)):
            err_hp += (y - x) ** 2
            sig_hp += x * x
    db = lambda e, s: 10 * math.log10((e + 1e-9) / (s + 1e-9))
    return db(err, sig), db(err_hp, sig_hp), (len(new[0]) - len(old[0])) / rate


def label(file_name):
    """The subsong index from a render's file name (tfmxrender -a numbers them song.wav-<n>.wav)."""
    stem = file_name[:-4] if file_name.lower().endswith('.wav') else file_name
    return stem.rsplit('-', 1)[-1]


def render(tool, mdat, folder, rate, extra):
    """Every subsong of the song into folder: {file name: md5}, or None if the tool failed."""
    try:
        r = subprocess.run([tool, '-a', '-r', str(rate), *extra, '-o', os.path.join(folder, 'song.wav'), mdat],
                           capture_output=True, timeout=600)
    except subprocess.TimeoutExpired:
        return None
    if r.returncode:
        return None
    return {os.path.basename(f): hashlib.md5(open(f, 'rb').read()).hexdigest()
            for f in glob.glob(os.path.join(folder, '*.wav'))}


def main():
    usage(__doc__, 2)
    args = sys.argv[1:]
    extra = []
    if '--' in args:
        extra = args[args.index('--') + 1:]
        args = args[:args.index('--')]
    rate, jobs, keep = 22050, os.cpu_count() or 4, None
    positional = []
    while args:
        a = args.pop(0)
        if a == '-r':
            rate = int(args.pop(0))
        elif a == '-j':
            jobs = int(args.pop(0))
        elif a == '--keep':
            keep = args.pop(0)
        else:
            positional.append(a)
    root, old_tool = positional[0], positional[1]
    new_tool = positional[2] if len(positional) > 2 else find_tool('tfmxrender')
    if keep:
        os.makedirs(keep, exist_ok=True)
    songs = sorted(p for p in glob.glob(os.path.join(root, '**', '*'), recursive=True)
                   if os.path.basename(p).lower().startswith('mdat.') or p.lower().endswith('.mdat'))

    def check(mdat):
        name = os.path.relpath(mdat, root)
        old_dir, new_dir = tempfile.mkdtemp(), tempfile.mkdtemp()
        try:
            old = render(old_tool, mdat, old_dir, rate, extra)
            new = render(new_tool, mdat, new_dir, rate, extra)
            if old is None or new is None:
                return name, None, (old is None, new is None)
            changed = []
            for f in sorted(set(old) | set(new)):
                if old.get(f) == new.get(f):
                    continue
                if f not in old or f not in new:
                    changed.append((f, None))
                    continue
                o, w = os.path.join(old_dir, f), os.path.join(new_dir, f)
                changed.append((f, difference(o, w, rate)))
                if keep:
                    base = os.path.join(keep, '%s %s' % (name.replace(os.sep, ' - '), label(f)))
                    shutil.copy(o, base + ' old.wav')
                    shutil.copy(w, base + ' new.wav')
            return name, changed, len(old)
        finally:
            shutil.rmtree(old_dir, ignore_errors=True)
            shutil.rmtree(new_dir, ignore_errors=True)

    subsongs = 0
    rows, failed = [], []
    with ThreadPoolExecutor(jobs) as pool:
        for name, changed, info in pool.map(check, songs):
            if changed is None:
                failed.append((name, info))
                continue
            subsongs += info
            rows += [(name, f, d) for f, d in changed]
    files = len({name for name, _, _ in rows})
    print('%d songs, %d subsongs: %d subsongs differ, in %d songs; %d songs failed to render' % (
        len(songs) - len(failed), subsongs, len(rows), files, len(failed)))
    # Biggest differences first; subsongs only one build produced at the top.
    rows.sort(key=lambda r: -1e9 if r[2] is None else -r[2][0])
    for name, f, d in rows:
        if d is None:
            print('  only one build has subsong %s: %s' % (label(f), name))
        else:
            print('  %7.1f dB, above 30 Hz %7.1f dB, length %+.2f s  %s, subsong %s' % (d[0], d[1], d[2], name, label(f)))
    for name, (old_failed, new_failed) in failed:
        print('  failed with the %s build: %s' % ('both' if old_failed and new_failed else 'old' if old_failed else 'new', name))
    return 1 if rows or failed else 0


if __name__ == '__main__':
    sys.exit(main())
