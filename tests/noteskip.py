"""For each note event in our trace, did the reference skip that voice's update in the note's tick?

Usage: python noteskip.py <stem> [...]    (stems as for show.py; uses the offset compare.py finds)
Prints a table of (track, voice) -> skipped / updated counts. "Skipped" means the reference wrote
no VOL for the voice in that tick (older builds write VOL every tick otherwise).
"""
import collections
import os
import sys

from compare import parse, states, diff

HERE = os.path.dirname(os.path.abspath(__file__))


def best_offset(ref, ours):
    voices = [0, 1, 2, 3]
    a, b = states(ref, voices), states(ours, voices)
    best = None
    for off in range(-4, 5):
        bad = sum(1 for i in range(len(a)) if 0 <= i + off < len(b) and diff(a[i], b[i + off], voices))
        if best is None or bad < best[1]:
            best = (off, bad)
    return best[0]


def main():
    table = collections.Counter()
    for stem in sys.argv[1:]:
        base = os.path.join(HERE, '..', 'build', 'traces', stem)
        ref, ours = parse(base + '.ref.txt'), parse(base + '.ours.txt')
        off = best_offset(ref, ours)
        for j, w in enumerate(ours):
            for v, reg, val in w:
                if reg != 'NOTE':
                    continue
                i = j - off
                if not 0 <= i < len(ref):
                    continue
                wrote = any(vv == v and r == 'VOL' for vv, r, x in ref[i])
                table[(val & 15, v, 'updated' if wrote else 'skipped')] += 1
    for t in range(8):
        for v in range(4):
            s, u = table[(t, v, 'skipped')], table[(t, v, 'updated')]
            if s or u:
                print('track %d -> voice %d: skipped %3d  updated %3d' % (t, v, s, u))


if __name__ == '__main__':
    main()
