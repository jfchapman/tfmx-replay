"""Tests the playback API through the DLL: loop points, fade-out, seeking past a loop, and the song position.

Usage: python api.py <mdat of a looped song> [tfmxreplay.dll path]
"""
import array
import ctypes
import math
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
RATE = 22050


class Settings(ctypes.Structure):
    _fields_ = [('sample_rate', ctypes.c_int), ('stereo_separation', ctypes.c_int), ('interpolation', ctypes.c_int),
                ('volume_ramping', ctypes.c_int), ('loop_count', ctypes.c_int), ('variant', ctypes.c_int),
                ('gain', ctypes.c_float), ('amiga_model', ctypes.c_int), ('led_filter', ctypes.c_int),
                ('fade_seconds', ctypes.c_float), ('mix_setting', ctypes.c_int)]


class Info(ctypes.Structure):
    _fields_ = [('number', ctypes.c_int), ('duration', ctypes.c_double), ('loop_start', ctypes.c_double),
                ('loop_length', ctypes.c_double), ('looped', ctypes.c_int), ('audible', ctypes.c_int),
                ('repeat_of', ctypes.c_int)]


path = sys.argv[1]
dll_path = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, '..', 'build', 'x64-dll', 'Release', 'tfmxreplay.dll')
t = ctypes.CDLL(dll_path)
t.tfmx_open.restype = ctypes.c_void_p
t.tfmx_open.argtypes = [ctypes.c_char_p, ctypes.c_size_t, ctypes.c_char_p, ctypes.c_size_t, ctypes.POINTER(Settings), ctypes.c_void_p]
for name in ('tfmx_seek', 'tfmx_position', 'tfmx_song_position', 'tfmx_subsong_duration'):
    getattr(t, name).restype = ctypes.c_double
t.tfmx_seek.argtypes = [ctypes.c_void_p, ctypes.c_double]
t.tfmx_position.argtypes = [ctypes.c_void_p]
t.tfmx_song_position.argtypes = [ctypes.c_void_p]
t.tfmx_subsong_duration.argtypes = [ctypes.c_void_p, ctypes.c_int]
t.tfmx_render_s16.restype = ctypes.c_size_t
t.tfmx_render_s16.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t]
t.tfmx_get_subsong_info.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(Info)]
t.tfmx_default_subsong.argtypes = [ctypes.c_void_p]
t.tfmx_select_subsong.argtypes = [ctypes.c_void_p, ctypes.c_int]
t.tfmx_set_settings.argtypes = [ctypes.c_void_p, ctypes.POINTER(Settings)]
t.tfmx_default_settings.argtypes = [ctypes.POINTER(Settings)]

mdat = open(path, 'rb').read()
folder, name = os.path.split(path)
smpl = open(os.path.join(folder, 'smpl.' + name[5:]), 'rb').read()


def settings(**kw):
    s = Settings()
    t.tfmx_default_settings(ctypes.byref(s))
    s.sample_rate = RATE
    s.volume_ramping = 0
    for k, v in kw.items():
        setattr(s, k, v)
    return s


def open_song(**kw):
    s = settings(**kw)
    return t.tfmx_open(mdat, len(mdat), smpl, len(smpl), ctypes.byref(s), None)


def render(m, frames=None):
    out = array.array('h')
    buf = (ctypes.c_int16 * 8192)()
    while frames is None or len(out) < 2 * frames:
        want = 4096 if frames is None else min(4096, frames - len(out) // 2)
        n = t.tfmx_render_s16(m, buf, want)
        if not n:
            break
        out.extend(buf[:2 * n])
    return out


failures = 0


def check(what, ok, detail=''):
    global failures
    failures += not ok
    print('%-4s %s %s' % ('ok' if ok else 'FAIL', what, detail))


def rms(a):
    return math.sqrt(sum(x * x for x in a) / max(len(a), 1))


m = open_song()
index = t.tfmx_default_subsong(m)
info = Info()
for i in range(32):
    if t.tfmx_get_subsong_info(m, i, ctypes.byref(info)):
        check('a looped subsong exists', False)
        sys.exit(1)
    if info.looped:
        index = i
        break
print('subsong %d: %.3f s, loop from %.3f s' % (index, info.duration, info.loop_start))
t.tfmx_select_subsong(m, index)
once = render(m)
check('one play-through has the reported length', abs(len(once) / 2 / RATE - info.duration) < 0.002, '%.3f' % (len(once) / 2 / RATE))

# Two passes and a fade.
fade = 4.0
m = open_song(loop_count=1, fade_seconds=fade)
t.tfmx_select_subsong(m, index)
total = t.tfmx_subsong_duration(m, index)
expected = info.duration + info.loop_length + fade
check('duration includes the loop and fade', abs(total - expected) < 1e-6, '%.3f' % total)
whole = render(m)
check('the render has that length', abs(len(whole) / 2 / RATE - total) < 0.03, '%.3f' % (len(whole) / 2 / RATE))
check('the first pass is unchanged', whole[:len(once)] == once)
end = len(whole)
tail = whole[end - 2 * int(0.05 * RATE):]
before = whole[end - 2 * int(fade * RATE) - 2 * RATE:end - 2 * int(fade * RATE)]
check('it fades to nothing', rms(tail) < 0.05 * max(rms(before), 1), '%.0f after %.0f' % (rms(tail), rms(before)))

# Song position through a loop, and seeking past it.
m = open_song(loop_count=-1)
t.tfmx_select_subsong(m, index)
when = info.duration + 0.25 * info.loop_length
straight = render(m, int(when * RATE) + RATE)
t.tfmx_seek(m, when)
position = t.tfmx_position(m)
song = t.tfmx_song_position(m)
want = info.loop_start + 0.25 * info.loop_length
check('position keeps rising', abs(position - when) < 0.001, '%.3f' % position)
check('song position goes back to the loop', abs(song - want) < 0.001, '%.3f want %.3f' % (song, want))
sought = render(m, RATE // 2)
start = 2 * round(position * RATE)
check('seeking past a loop is sample exact', sought == straight[start:start + len(sought)])

# Seeking into the fade.
m = open_song(fade_seconds=fade)
t.tfmx_select_subsong(m, index)
t.tfmx_seek(m, info.duration + fade / 2)
rest = render(m)
check('seeking into the fade leaves the rest of it', abs(len(rest) / 2 / RATE - fade / 2) < 0.05, '%.3f' % (len(rest) / 2 / RATE))
print('FAILED' if failures else 'all passed')
sys.exit(1 if failures else 0)
