"""Show one voice of a reference trace and a tfmxtrace side by side, tick by tick.

Usage: python show.py <trace stem> <voice> <first tick> [last tick]
  stem: the file name in build/traces without .ref.txt / .ours.txt, e.g. t1-mdat.turrican_intro-0
"""
import os
import sys

from compare import parse

HERE = os.path.dirname(os.path.abspath(__file__))


def cell(writes, voice):
    out = []
    for v, reg, val in writes:
        if v != voice:
            continue
        if reg == 'NOTE':
            out.append('NOTE %02X m%02X v%X' % (val >> 16, (val >> 8) & 0x7F, (val >> 4) & 0xF))
        elif reg.startswith('DMA'):
            out.append(reg[4:].lower())
        else:
            out.append('%s %X' % (reg, val))
    return ' '.join(out)


def main():
    stem, voice, first = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    last = int(sys.argv[4]) if len(sys.argv) > 4 else first + 12
    base = os.path.join(HERE, '..', 'build', 'traces', stem)
    ref, ours = parse(base + '.ref.txt'), parse(base + '.ours.txt')
    for t in range(first, last + 1):
        a = cell(ref[t + 1], voice) if t + 1 < len(ref) else ''
        b = cell(ours[t + 1], voice) if t + 1 < len(ours) else ''
        print('%5d %s %-48s | %s' % (t, ' ' if a == b else '*', a, b))


if __name__ == '__main__':
    main()
