# TFMXreplay

A library for playing TFMX music: the format Chris Hülsbeck wrote for Amiga games such as
*Turrican*, *Turrican 2* and *Apidya*, and which Rudolf Stember (Factor 5), Tim Follin, Lajos Suvak
and others also used, from 1989 to the mid-1990s.

- Plain C API (`include/tfmxreplay.h`), static or shared library, no dependencies.
- Builds with CMake or a plain Makefile; tested with MSVC (x64 and 32-bit).
- Two-file songs (`mdat.*` + `smpl.*`) and single-file TFHD songs.
- Subsongs, with lengths and loop points, seeking, and the song's text. Finds the main subsong and
  subsongs that repeat another's music, and tells songs that loop from ones that end in silence.
- Output options: sample rate, stereo separation, interpolation (none, linear, cubic),
  Amiga output filters (A500, A1200 or none, plus the LED filter), anti-click volume ramping,
  loop count, fade-out, gain, 7V mixer quality.
- The different replayer generations ("variants"): TFMX Pro and 7V, TFMX 1.5, and the earlier
  builds used by Masterblazer, X-Out and Rock'n'Roll, and GemX's (also Monkey Island's). Detected
  from the song, or chosen. *Turrican 2*'s two builds share a variant of their own, which has to
  be chosen.

The format is described in [docs/TFMX-format.md](docs/TFMX-format.md).

## Building

With CMake:

```
cmake -S . -B build
cmake --build build --config Release
```

Options: `-DTFMX_BUILD_SHARED=ON` for a shared library (DLL / .so / .dylib), `-DTFMX_BUILD_TOOLS=OFF`
to skip the tools. For a 32-bit Windows build, add `-A Win32` with a Visual Studio generator.
`cmake --install build` installs the header, library and a CMake package, so another project can
use `find_package(TFMXreplay)` and link `TFMXreplay::tfmxreplay`.

Without CMake, with GNU make and gcc or clang: `make` (static library and tools) or `make shared`.

For the web, `wasm\build.cmd` builds a single `build\wasm\tfmxreplay.wasm` with
[Emscripten](https://emscripten.org/) (run it from the repository's root, with `em++` on the path).
It needs no JavaScript from Emscripten: instantiate it, call its `_initialize` export, and give it
a do-nothing `env.emscripten_notify_memory_growth` import. `wasm/tfmxreplay_wasm.cpp` adds
`tfmx_wasm_open` and `tfmx_wasm_configure`, which take the settings as plain numbers.

## Using the library

```c
#include "tfmxreplay.h"

tfmx_settings settings;
tfmx_default_settings(&settings);
settings.sample_rate = 48000;

tfmx_error error;
tfmx_module *module = tfmx_open(mdat, mdat_size, smpl, smpl_size, &settings, &error);
if (!module) {
    fprintf(stderr, "%s\n", tfmx_error_string(error));
    return;
}
for (int i = 0; i < tfmx_subsong_count(module); i++)
    printf("subsong %d: %.1f s\n", i, tfmx_subsong_duration(module, i));

tfmx_select_subsong(module, 0);
int16_t buffer[2 * 1024];
size_t frames;
while ((frames = tfmx_render_s16(module, buffer, 1024)) > 0) {
    /* 'frames' stereo frames, interleaved left/right */
}
tfmx_close(module);
```

The header documents every function. Points worth knowing:
- The library copies the song data, so the caller's buffers can be freed after `tfmx_open`.
- Most TFMX songs loop forever. By default a subsong plays once, up to its loop point or end
  (`loop_count = 0`); set `loop_count` for extra loops, or -1 to loop forever.
- `tfmx_subsong_duration` runs the song quickly without mixing (a few milliseconds).
- Subsong indexes are 0 to `tfmx_subsong_count() - 1`; `tfmx_subsong_number` gives the song's
  own number (0-31) for display.

## Tools

`tfmxrender` renders songs to 16-bit WAV files, using only the public API (it's also the example
of using the library from C):

```
tfmxrender "mdat.turrican 2 level 1-desert"        # finds smpl.* itself
tfmxrender -a -o out "mdat.apidya (level 1)"       # every subsong: out-0.wav, out-1.wav, ...
tfmxrender -n "mdat.apidya (level 1)"              # list subsongs, lengths and text
```

Run it without arguments for all the options. The smpl file is found from the mdat's name:
`mdat.<name>` → `smpl.<name>`, `<name>.mdat` → `<name>.smpl`, or `<name>.tfx` → `<name>.sam`.

`tfmxtrace` prints the Paula register writes the replayer makes, tick by tick, for testing.

## Accuracy

TFMX never had a specification, and every game shipped its own build of the replayer. The
behaviour here was measured by running the original replayers from game memory in an emulator
and logging their Paula writes ("black-box" traces, never disassembly), then comparing
TFMXreplay's output with theirs tick by tick (`tests/`).

Current state:
- **TFMX Pro (4 voices):** all seven of *Apidya*'s 4-voice songs match the original replayer
  exactly, every register of every tick, over the 30 seconds compared.
- **TFMX 1.5 (Turrican 1):** every song compared (14, both of the game's builds) matches exactly.
- **X-Out family (X-Out, Fly Harder, Abandoned Places):** every song compared (17) matches exactly.
- **Rock'n'Roll:** all 10 songs match exactly.
- **Masterblazer:** the loader, intro and title music and Z-Out's title match exactly. *museum* and
  *lucasarts* differ only in one start-up volume write left over from the game. In *race*, two
  voices use macro 1B ("random"), which isn't implemented yet.
- **GemX and Monkey Island:** all 8 GemX subsongs match exactly. Monkey Island's 22 subsongs match
  apart from start-up leftovers, except that in three of them a wait on Paula ends a tick early
  now and then.
- **7V:** *Apidya*'s end music, the *Turrican 2* title and all three *B.C. Kid* title subsongs
  match exactly on the three hardware voices the traces can compare (the fourth plays the software
  mix of voices 4-7, which is compared separately, below). Seven more games' 7V songs (*Turrican 3*, *Hexuma*, *Anstoss* and others) also
  match exactly when played through B.C. Kid's 7V replayer. *Apidya*'s title
  matches exactly too, checked against captures of the real game running in WinUAE (which also
  verified the *Hexuma*, *Bundesliga Manager* and *Anstoss* titles).
- **The 7V mix:** an emulation of the original's software mixer (rate, volume table, clamping, timing),
  checked byte for byte against its buffers. *Carl Lewis Challenge* is identical over 1,200 ticks,
  *Apidya*'s title over 1,499 of 1,500, and its six-minute end song differs in 0.02% of its bytes,
  nearly all single bytes at loop points. *Turrican 2*'s title and *Turrican 3*'s intro are nearly
  identical, *Hexuma*'s title close. A WinUAE recording of *Turrican 2* agrees at gain 1.00.
- **Turrican 2 in game** (its small 4-voice build, `TFMX_VARIANT_TURRICAN2`): *level 1-desert*
  and *level 5-aliens* match exactly in 13 of their 15 subsongs. One differs in 4 ticks because of a
  note the game's own music left behind, and one in a portamento meeting a vibrato.
- **Carl Lewis Challenge, Mega-lo-Mania, Hexuma:** every subsong compared through the games' own
  replayers matches exactly: Carl Lewis's and Mega-lo-Mania's title music (7V), Mega-lo-Mania's
  three in-game songs, and Hexuma's six level songs (with `TFMX_VARIANT_HEXUMA`, whose replayer
  has no macro vibrato).
- **Turrican 3:** through the game's 7V replayer (its intro and music player) and its in-game
  replayer, 3 of the intro's 4 subsongs and 29 of the 32 level subsongs match exactly with the Pro
  variant. Its small loading-screen replayer is a cut-down build
  (`TFMX_VARIANT_TURRICAN3_SMALL`), which matches 4 of the 5 level songs; its loading music uses the
  SID macros, which still differ.
- **SID synthesis (macros 22-29):** implemented from measurements, not yet compared song by song.
- All 368 songs in the modland collection open (bar one 540-byte file with no playable
  subsong), and all but 7 of their 1,608 subsongs get a length.

## Licence

MIT: see [LICENSE](LICENSE). The format description in `docs/` was written from black-box
measurements and from public descriptions, in its own words; no code from other TFMX players
was used.
