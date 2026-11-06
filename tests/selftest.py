"""Self-contained tests: needs only Python and a build of the tools, with no song files.

Usage: python selftest.py [--tool tfmxtrace] [--render tfmxrender] [--only name] [--update]

Builds tiny TFMX songs in memory, runs each through tfmxtrace on every replayer variant, and
compares the Paula writes with tests/expected. Those files are the library's own output, saved
from a build that passed the comparisons with the original replayers, so a failure means the
behaviour has changed, not that it has stopped matching a game. If a change is meant to alter
the output, check the difference, then run with --update.

It also checks variant detection, and renders a song with tfmxrender through the public API.
"""
import argparse
import difflib
import os
import struct
import subprocess
import sys
import tempfile

from common import HERE, find_tool

EXPECTED = os.path.join(HERE, 'expected')
VARIANTS = ['pro', 'turrican2', 't3small', 'hexuma', 'gemx', 'masterblazer', '1.5', 'xout', 'rocknroll']
IDLE = 0xFF00
KEEP = 0x8000  # the track keeps its pattern running


# --- building songs -----------------------------------------------------------------------

def long(b0, b1=0, b2=0, b3=0):
    return bytes([b0 & 0xFF, b1 & 0xFF, b2 & 0xFF, b3 & 0xFF])


def word(value):
    return (value >> 8) & 0xFF, value & 0xFF


def track(*tracks):
    """Pattern numbers, or whole track words such as KEEP."""
    return [t << 8 if t < 0x80 else t for t in tracks] + [IDLE] * (8 - len(tracks))


def command(cmd, w3=0, w4=0):
    return [0xEFFE, cmd, w3, w4, 0, 0, 0, 0]


def song(lines, patterns, macros, tempo=0, magic=b'TFMX-SONG ', packed=False, subsongs=None):
    """lines: trackstep lines of 8 words. subsongs: (start, end, tempo) for each, or just one for the lot."""
    header = bytearray(0x200)
    header[:len(magic)] = magic
    if magic != b'TFMX ':
        header[0x10:0x10 + 15] = b'TFMXreplay test'
    for i, (start, end, speed) in enumerate(subsongs or [(0, len(lines) - 1, tempo)]):
        struct.pack_into('>H', header, 0x100 + 2 * i, start)
        struct.pack_into('>H', header, 0x140 + 2 * i, end)
        struct.pack_into('>H', header, 0x180 + 2 * i, speed)
    base = 0x200 if packed else 0x800
    body = bytearray()
    for line in lines:
        body += struct.pack('>8H', *line)
    pattern_at, macro_at = [], []
    for data, offsets in ((patterns, pattern_at), (macros, macro_at)):
        for d in data:
            offsets.append(base + len(body))
            body += d
    pattern_table = struct.pack('>128I', *(pattern_at + [0] * (128 - len(pattern_at))))
    macro_table = struct.pack('>128I', *(macro_at + [0] * (128 - len(macro_at))))
    if packed:
        struct.pack_into('>III', header, 0x1D0, base, base + len(body), base + len(body) + 0x200)
        return bytes(header + body + pattern_table + macro_table)
    return bytes(header + bytes(0x200) + pattern_table + macro_table + body)  # tables at 0x400 and 0x600


def samples():
    """Silence, a square wave at 4, a saw at $44, and a ramp for SID at $404, past its buffers."""
    data = bytearray(0x600)
    data[4:0x44] = bytes([0x40] * 32 + [0xC0] * 32)
    data[0x44:0x84] = bytes((4 * i - 128) & 0xFF for i in range(64))
    data[0x404:0x504] = bytes(i & 0xFF for i in range(256))
    return bytes(data)


def sound(b1=0, volume=0x30, note=0x18, begin=4, length=32):
    return (long(0x00, b1) + long(0x02, 0, *word(begin)) + long(0x03, 0, *word(length)) + long(0x0E, 0, 0, volume)
            + long(0x09, note) + long(0x01))


def wait(ticks=0):
    return long(0x04, 0, *word(ticks))


def volume(value):
    return long(0x0E, 0, 0, value)


STOP = long(0x07)
HOLD = long(0xF3, 0x7F) + long(0xF0)


def note(macro, voice=0, velocity=0xF, number=0x18, finetune=0):
    return long(number, macro, velocity << 4 | voice, finetune)


def rows(n):
    return long(0xF3, n - 1)


def one(pattern, macros, tempo=0):
    return song([track(0)], [pattern], macros, tempo=tempo)


# --- the songs ----------------------------------------------------------------------------

def retrigger():
    # A held note, a deferred cut, an immediate cut, then a note with finetune on another voice.
    plain = sound() + STOP
    deferred = long(0x00, 1) + long(0x02, 0, 0, 4) + long(0x03, 0, 0, 32) + long(0x0D, 0, 0, 0x14) + long(0x08) + long(0x01) + STOP
    keeps = long(0x13) + long(0x02, 0, 0, 0x44) + long(0x03, 0, 0, 16) + volume(0x28) + long(0x08, 2) + long(0x01) + STOP
    pattern = (note(0) + rows(4) + note(1, velocity=5) + rows(4) + note(2) + rows(4) + note(0, 1, finetune=0x20)
               + rows(4) + HOLD)
    return one(pattern, [plain, deferred, keeps]), 32


def tick_enders():
    # Volume markers either side of each command show whether it ended the tick.
    macro = sound(1) + wait(1)
    for i, command_ in enumerate((wait(), long(0x08), long(0x09, 0x18), long(0x17, 0, 0x01, 0xAC), long(0x1F),
                                  long(0x13), long(0x0D, 0, 0, 0x10))):
        macro += volume(0x10 + 2 * i) + command_ + volume(0x11 + 2 * i) + wait()
    return one(note(0) + HOLD, [macro + STOP]), 30


def effects():
    vibrato = sound() + long(0x0C, 6, 0, 0x10) + STOP
    uneven = sound() + long(0x0C, 5, 0, 0xFC) + STOP
    slide = sound() + wait(2) + long(0x0B, 1, 0, 0x10) + long(0x09, 0x20) + STOP
    envelope = sound() + long(0x0F, 4, 1, 0x10) + STOP
    pattern = (note(0) + note(1, 1) + note(2, 2) + note(3, 3) + rows(10) + long(0xC0 + 0x20, 2, 0, 8)
               + long(0xC0 + 0x10, 1, 1, 8) + long(0xF7, 3, 0x20, 0x30) + long(0xF6, 8, 3, 8) + HOLD)
    return one(pattern, [vibrato, uneven, slide, envelope]), 60


def sample_commands():
    sweep = sound() + long(0x11, 3, 0, 2) + wait(8) + long(0x18, 0, 0, 16) + wait(3) + long(0x12, 0, 0xFF, 0xF8) + wait(3) + long(0x19) + STOP
    once = sound() + long(0x11, 0, 0, 4) + wait(3) + long(0x03, 0, 0, 0) + wait(3) + long(0x0A) + STOP
    dma_wait = sound(length=8) + long(0x1A, 0, 0, 0) + volume(0x10) + long(0x1A, 0, 0, 2) + volume(0x20) + long(0x1A, 0, 0, 1) + STOP
    return one(note(0) + note(1, 1) + note(2, 2) + HOLD, [sweep, once, dma_wait]), 40


def loops():
    macro_loop = sound() + wait() + volume(0x10) + wait() + volume(0x11) + wait() + long(0x05, 2, 0, 7) + volume(0x30) + STOP
    # A 14 with a timeout inside a 10 loop: the two share one counter.
    shared = sound() + volume(0x12) + long(0x14, 0, 0, 3) + volume(0x13) + wait() + long(0x10, 2, 0, 6) + volume(0x31) + STOP
    pattern = note(0) + note(1, 1) + rows(2) + long(0xF1, 2, 0, 2) + rows(1) + long(0xF5, 0, 1) + long(0xF0)
    stop = long(0xF3, 3) + long(0xF0)
    lines = [track(0), track(1), command(1, 1, 2), command(1, 0, 1), command(0)]
    return song(lines, [pattern, stop], [macro_loop, shared]), 80


def key_up():
    macro = sound() + volume(0x10) + long(0x14) + volume(0x30) + STOP
    return one(note(0) + rows(2) + long(0xF5) + HOLD, [macro], tempo=2), 20


def macro_flow():
    target = volume(0x21) + wait() + volume(0x22) + long(0x16) + volume(0x23) + STOP
    gosub = sound() + long(0x15, 1) + volume(0x24) + wait() + long(0x06, 1, 0, 2) + STOP
    split = (sound() + long(0x1C, 0x10, 0, 8) + volume(0x25) + wait() + volume(0x26) + long(0x1D, 0x30, 0, 12)
             + volume(0x27) + wait() + volume(0x28) + long(0x21, 3, 3, 0) + STOP)
    other = sound(note=0x20) + long(0x1F) + STOP
    return one(note(0) + note(2, 1) + HOLD, [gosub, target, split, other]), 30


def pattern_flow():
    first = note(0) + rows(1) + long(0xF2, 1, 0, 1) + long(0xFF)
    second = (long(0xFF) + note(0, 1) + rows(1) + long(0xF8, 2, 0, 0) + long(0xFD, 0, 0, 1) + long(0xFC, 1, 2, 3)
              + note(0, 2) + rows(2) + note(0, 2) + rows(1) + long(0xFB, 3, 2, 0) + rows(3) + long(0xF0))
    sub = note(0, 3) + rows(1) + long(0xF9)
    played = note(0, 3, number=0x20) + long(0xF4)
    return song([track(0), track(KEEP, 0)], [first, second, sub, played], [sound() + STOP]), 30


def fades():
    lines = [command(4, 2, 0x10), track(0), command(4, 0, 0x30), track(0, 1), command(4, 1, 0x40), track(0)]
    fade = long(0xFA, 1, 0, 0x08) + rows(6) + long(0xF0)
    return song(lines, [note(0) + rows(8) + long(0xF0), fade], [sound() + long(0x0D, 0, 0, 0xE8) + STOP]), 60


def tempo_change():
    lines = [track(0), command(2, 1), track(0), command(2, 0, 125), track(0), command(0)]
    return song(lines, [note(0) + rows(3) + long(0xF0)], [sound() + STOP], tempo=125), 50


def song_end():
    return one(note(0) + rows(2) + long(0xF4), [sound() + STOP]), 140


def sid():
    macro = (long(0x29, 1) + long(0x22, 0, *word(0x404)) + long(0x23, 0x20, 0, 0x0F) + long(0x24, 0, 0x01, 0x00)
             + long(0x25, 4, 0x10, 0x00) + long(0x26, 0, 0, 0x40) + long(0x27, 3, 0, 0x10) + long(0x28, 2, 0x10, 0x08)
             + sound() + wait(8) + long(0x29, 0) + long(0x19) + STOP)
    return one(note(0, 1) + HOLD, [macro]), 16


def seven_voice():
    lines = [command(3, 0xFF00, 0x00EE), track(0), command(3, 0xFF00, 0x0010), track(0)]
    pattern = note(0, 0) + note(0, 4) + note(1, 5, number=0x1C) + note(2, 6) + note(1, 7) + rows(8) + long(0xF5, 0, 4) + rows(4) + long(0xF0)
    vibrato = sound(length=64) + long(0x0C, 6, 0, 0x10) + long(0x14) + long(0x13) + STOP
    return song(lines, [pattern], [sound(length=64) + STOP, vibrato, sound(length=16, volume=0x40) + long(0x0F, 2, 2, 0x10) + STOP]), 40


SONGS = {
    'retrigger': retrigger, 'tick_enders': tick_enders, 'effects': effects, 'sample_commands': sample_commands,
    'loops': loops, 'key_up': key_up, 'macro_flow': macro_flow, 'pattern_flow': pattern_flow, 'fades': fades,
    'tempo_change': tempo_change, 'song_end': song_end, 'sid': sid, 'seven_voice': seven_voice,
}

# Songs with the features that decide the detected variant.
PLAIN = note(0) + long(0xF0)
WAITS = long(0x98, 0, 0xF0, 3) + long(0xF0)
DETECTION = {
    'rocknroll': lambda: one_with(PLAIN, magic=b'TFMX '),
    'masterblazer': lambda: one_with(WAITS, magic=b'tfmxsong'),
    '7v': lambda: song([command(3, 0xFF00, 0), track(0)], [WAITS], [sound() + STOP]),
    'gemx': lambda: song([track(0)], [WAITS], [long(0x00, 0, 0, 0x10) + STOP for _ in range(10)]),
    'packed': lambda: one_with(WAITS, packed=True),
    'xout': lambda: one_with(PLAIN),
    '1.5': lambda: one_with(WAITS),
}


def one_with(pattern, **options):
    return song([track(0)], [pattern], [sound() + STOP], **options)


# --- running them -------------------------------------------------------------------------

def write_song(folder, name, mdat):
    paths = os.path.join(folder, 'mdat.' + name), os.path.join(folder, 'smpl.' + name)
    with open(paths[0], 'wb') as f:
        f.write(mdat)
    with open(paths[1], 'wb') as f:
        f.write(samples())
    return paths


def run(tool, *args):
    r = subprocess.run([tool] + [str(a) for a in args], capture_output=True, text=True, timeout=60)
    if r.returncode:
        raise RuntimeError('%s: exit %d: %s' % (os.path.basename(tool), r.returncode, r.stderr.strip()))
    return r.stdout


def trace_song(tool, folder, name):
    mdat, ticks = SONGS[name]()
    paths = write_song(folder, name, mdat)
    out = []
    for variant in VARIANTS:
        out.append('== %s\n' % variant + run(tool, *paths, 0, ticks, variant))
    if name == 'seven_voice':
        out.append('== mix\n' + run(tool, *paths, 0, ticks, 'pro', 'mix'))
    return ''.join(out)


def check_song(tool, folder, name, update):
    path = os.path.join(EXPECTED, name + '.txt')
    got = trace_song(tool, folder, name)
    if update:
        os.makedirs(EXPECTED, exist_ok=True)
        with open(path, 'w', newline='\n') as f:
            f.write(got)
        return 'updated'
    if not os.path.exists(path):
        return 'no expected output: run with --update'
    with open(path) as f:
        want = f.read()
    if got == want:
        return None
    diff = difflib.unified_diff(want.splitlines(), got.splitlines(), 'expected', 'got', lineterm='', n=2)
    return '\n'.join(list(diff)[:24])


def check_detection(tool, folder):
    """The variant each kind of song is detected as, by tfmx_variant's number as tfmxtrace prints it."""
    names = {}
    for kind, build in DETECTION.items():
        paths = write_song(folder, 'detect-' + kind, build())
        names[kind] = run(tool, *paths, 0, 1, 'auto').split()[1]
    return ' '.join('%s=%s' % kv for kv in names.items())


def check_detection_expected(tool, folder, update):
    path = os.path.join(EXPECTED, 'detection.txt')
    got = check_detection(tool, folder) + '\n'
    if update:
        with open(path, 'w', newline='\n') as f:
            f.write(got)
        return 'updated'
    with open(path) as f:
        want = f.read()
    return None if got == want else 'expected %s, got %s' % (want.strip(), got.strip())


def check_render(render, folder):
    """A render through the public API: a valid WAV of the right length, and not silent."""
    mdat, _ = song_end()
    paths = write_song(folder, 'render', mdat)
    wav = os.path.join(folder, 'render.wav')
    run(render, '-o', wav, paths[0])
    with open(wav, 'rb') as f:
        data = f.read()
    if data[:4] != b'RIFF' or data[8:12] != b'WAVE':
        return 'not a WAV file'
    audio = data[44:]
    seconds = len(audio) / 4 / 44100
    if not 2 < seconds < 3:
        return 'unexpected length, %.2f s' % seconds
    if not any(audio):
        return 'silent'
    return None


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--tool', help='path of tfmxtrace')
    ap.add_argument('--render', help='path of tfmxrender')
    ap.add_argument('--only', help='run this song only')
    ap.add_argument('--update', action='store_true', help='save the current output as expected')
    args = ap.parse_args()
    tool = args.tool or find_tool('tfmxtrace', 'TFMX_TRACE_TOOL')
    render = args.render or find_tool('tfmxrender', 'TFMX_RENDER_TOOL')
    failed = 0
    with tempfile.TemporaryDirectory() as folder:
        checks = [(n, lambda n=n: check_song(tool, folder, n, args.update)) for n in SONGS if not args.only or n == args.only]
        if not args.only:
            checks.append(('detection', lambda: check_detection_expected(tool, folder, args.update)))
            checks.append(('render', lambda: None if args.update else check_render(render, folder)))
        for name, check in checks:
            result = check()
            if result is None:
                print('ok      %s' % name)
            elif result == 'updated':
                print('updated %s' % name)
            else:
                failed += 1
                print('FAILED  %s\n%s' % (name, result))
        print('%d of %d passed' % (len(checks) - failed, len(checks)))
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
