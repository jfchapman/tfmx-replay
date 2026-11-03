"""Convert a WinUAE console memwatch capture into the tfmxtrace format, for compare.py.

Usage: python capture.py <capture.txt> <pc from> <pc to> [--smpl HEX] [--tick AUD3LC|INTREQ] > ref.txt
The capture is the debugger console text after
    w 0 dff096 2 W L / w 1 dff0a0 40 W L / w 2 dff09c 2 W L
Only writes made from PCs in [from, to) (the replayer) are kept. A tick starts at the voice 3
buffer swap (AUD3LC write, 7V songs) or at the replayer's first write after an INTREQ write
(4-voice builds, --tick INTREQ). --smpl gives the sample bank's address, so LC comes out relative to
it as tfmxtrace prints it.
"""
import argparse
import collections
import re

LINE = re.compile(r'Memwatch \d: break at 00DFF([0-9A-F]{3})\.\w\s+W\s+([0-9A-F]+) PC=([0-9A-F]+)')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('capture')
    ap.add_argument('pc_from', type=lambda s: int(s, 16))
    ap.add_argument('pc_to', type=lambda s: int(s, 16))
    ap.add_argument('--smpl', type=lambda s: int(s, 16), default=0)
    ap.add_argument('--tick', default='AUD3LC')
    a = ap.parse_args()

    writes = []
    for line in open(a.capture, errors='replace'):
        m = LINE.search(line)
        if m:
            writes.append((int(m.group(1), 16), int(m.group(2), 16), int(m.group(3), 16)))
    # The tick's buffer swap always writes AUD3LC from the same instruction; a game can also
    # re-initialise the mixer (Apidya's main loop does, with LEN 2) from another, which isn't a tick.
    swaps = collections.Counter(pc for reg, val, pc in writes if reg == 0x0D0 and a.pc_from <= pc < a.pc_to)
    swap_pc = swaps.most_common(1)[0][0] if swaps else None

    lch = [0] * 4
    out, started, pending = ['profile capture', 'start'], False, False
    tick = 0
    for reg, val, pc in writes:
        if reg == 0x09C:
            pending = True  # an interrupt acknowledge: the next replayer write may start a tick
            continue
        if not a.pc_from <= pc < a.pc_to:
            continue
        voice, off = (reg - 0x0A0) // 16, (reg - 0x0A0) % 16
        new_tick = ((a.tick == 'AUD3LC' and reg == 0x0D0 and pc == swap_pc) or (a.tick == 'INTREQ' and pending))
        pending = False
        if new_tick:
            out.append('tick %d' % tick)
            tick += 1
            started = True
        if reg == 0x096:
            kind = 'DMA_ON' if val & 0x8000 else 'DMA_OFF'
            for v in range(4):
                if val & (1 << v):
                    out.append(' %d %s 0' % (v, kind))
            continue
        if not 0 <= voice < 4:
            continue
        if off == 0:      # LCH (a long write logs its high word here, then the whole value at LCL)
            lch[voice] = val & 0xFFFF
            continue
        if off == 2:      # LCL
            full = val if val > 0xFFFF else lch[voice] << 16 | val
            out.append(' %d LC %X' % (voice, (full - a.smpl) & 0xFFFFFFFF))
        elif off == 4:
            out.append(' %d LEN %X' % (voice, val & 0xFFFF))
        elif off == 6:
            out.append(' %d PER %X' % (voice, val & 0xFFFF))
        elif off == 8:
            out.append(' %d VOL %X' % (voice, val & 0xFFFF))
    print('\n'.join(out))


if __name__ == '__main__':
    main()
