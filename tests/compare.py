"""Compare TFMXreplay's Paula writes with a reference trace from an original replayer.

Usage: python compare.py <reference.txt> <tfmxtrace.txt> [--voices 0123] [--show N]

Both files have the tfmxtrace format: a "start" section, then "tick N" sections of
" voice REG value" lines. Per tick and voice, the comparison uses what Paula ends up with:
the DMA on/off events in order, and the last LC, LEN, PER and VOL written. A register is only
compared once both sides have written it (the reference starts from a game's state). The two
traces are aligned by the tick offset that matches best, since the start-up latency differs by
build and isn't audible.
"""
import argparse

REGS = ('LC', 'LEN', 'PER', 'VOL')


def parse(path):
    ticks = []
    cur = None
    for line in open(path):
        line = line.rstrip()
        if line == 'start' or line.startswith('tick '):
            cur = []
            ticks.append(cur)
        elif line.startswith(' ') and cur is not None:
            v, reg, val = line.split()
            val = int(val, 16)
            if reg == 'LC':
                # Paula only sees address bits 1-23 (a negative begin wraps; DMA is word-aligned).
                val &= 0xFFFFFE
            cur.append((int(v), reg, val))
    return ticks


def states(ticks, voices):
    """Per tick: {voice: (dma transitions, playing, {reg: value or None})}, registers carried
    forward. Only real transitions count: some builds re-enable DMA on a voice that's playing."""
    regs = {v: {r: None for r in REGS} for v in voices}
    playing = {v: False for v in voices}
    out = []
    for writes in ticks:
        dma = {v: [] for v in voices}
        for v, reg, val in writes:
            if v not in voices:
                continue
            if reg in REGS:
                regs[v][reg] = val
            elif reg not in ('DMA_ON', 'DMA_OFF'):
                continue  # annotations such as NOTE
            elif (reg == 'DMA_ON') != playing[v]:
                playing[v] = reg == 'DMA_ON'
                dma[v].append('on' if playing[v] else 'off')
        out.append({v: (tuple(dma[v]), playing[v], dict(regs[v])) for v in voices})
    return out


IGNORE = set()


def diff(a, b, voices):
    """Differences between two tick states, as strings. Volume and period only matter while the
    reference voice plays (silent voices keep whatever the game left there)."""
    out = []
    for v in voices:
        da, pa, ra = a[v]
        db, pb, rb = b[v]
        if da != db:
            out.append('v%d DMA %s vs %s' % (v, '/'.join(da) or '-', '/'.join(db) or '-'))
        for r in REGS:
            if r in IGNORE:
                continue
            if r in ('VOL', 'PER') and not pa:
                continue
            if ra[r] is not None and rb[r] is not None and ra[r] != rb[r]:
                out.append('v%d %s %X vs %X' % (v, r, ra[r], rb[r]))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('reference')
    ap.add_argument('ours')
    ap.add_argument('--voices', default='0123')
    ap.add_argument('--show', type=int, default=12)
    ap.add_argument('--search', type=int, default=4, help='largest tick offset tried when aligning '
                    '(more for a capture that starts before or during the song)')
    ap.add_argument('--ignore', default='', help='registers to leave out, e.g. VOL (when the game '
                    'left the master volume low)')
    args = ap.parse_args()
    IGNORE.update(x for x in args.ignore.split(',') if x)
    voices = [int(c) for c in args.voices]
    ref = states(parse(args.reference), voices)
    ours = states(parse(args.ours), voices)
    best = None
    for off in range(-args.search, args.search + 1):
        pairs = [(ref[i], ours[i + off]) for i in range(len(ref)) if 0 <= i + off < len(ours)]
        if len(pairs) < min(len(ref), len(ours)) // 2:
            continue  # too little overlap to mean anything
        bad = sum(1 for a, b in pairs if diff(a, b, voices))
        if best is None or bad < best[1]:
            best = (off, bad, len(pairs))
    off, bad, n = best
    print('offset %+d: %d of %d ticks differ' % (off, bad, n))
    shown = 0
    for i in range(len(ref)):
        j = i + off
        if not 0 <= j < len(ours):
            continue
        d = diff(ref[i], ours[j], voices)
        if d and shown < args.show:
            print('  ref tick %d / ours %d: %s' % (i - 1, j - 1, '; '.join(d[:6])))
            shown += 1


if __name__ == '__main__':
    main()
