# The TFMX music format

TFMX is the music system Chris Hülsbeck wrote for Amiga games, used from 1989 to the mid-1990s by Hülsbeck, Rudolf Stember (Factor 5), Tim Follin, Lajos Suvak and others. It never had a public specification, and every game shipped its own build of the replayer, so "the format" is really a family of closely related behaviours.

This document describes the format as far as it's known: the file layout, what every command does, how a tick is timed, and where the replayer builds differ. It's detailed enough to write a player from, and [TFMXreplay](../README.md) was written from it. Sections 1–13 give the behaviour of the later (TFMX Pro) builds, with differences noted where they matter. Section 14 sets out how the other builds differ. Section 16 explains how the behaviour was measured, and section 17 lists what's still unknown.

**Conventions.** All values are big-endian. "Word" means 16 bits and "long" means 32 bits. `b0 b1 b2 b3` are the bytes of a long, and `b2b3` is a word made from two of them. `$` and `0x` both mean hexadecimal. Counts of files and songs refer to the 368 mdat/smpl pairs in the modland collection's `modules\TFMX` folder.

## Contents

1. [Concepts](#1-concepts)
2. [Files](#2-files)
3. [The mdat header](#3-the-mdat-header)
4. [Trackstep](#4-trackstep)
5. [Patterns](#5-patterns)
6. [Macros](#6-macros)
7. [Effects](#7-effects)
8. [Paula](#8-paula)
9. [Periods](#9-periods)
10. [Timing](#10-timing)
11. [7-voice mode](#11-7-voice-mode)
12. [The SID commands](#12-the-sid-commands)
13. [Song ends and loops](#13-song-ends-and-loops)
14. [Replayer builds](#14-replayer-builds)
15. [The songs in modland](#15-the-songs-in-modland)
16. [How this was measured](#16-how-this-was-measured)
17. [Open questions](#17-open-questions)
18. [Earlier descriptions](#18-earlier-descriptions)

---

## 1. Concepts

- **Subsong:** a range of trackstep lines with a starting tempo. One file holds up to 32. Games often keep all of a level's music, and sometimes its sound effects, in one file.
- **Trackstep:** the sequence. Each line starts a pattern on each of 8 tracks, or is a command.
- **Track and pattern:** a track runs a pattern, which is a list of notes and commands. Tracks and voices are separate: each note names its own voice, so one pattern can play chords across several voices, and several tracks can drive one voice.
- **Macro:** a small program that runs per voice, once a tick, and plays the role of an instrument. It picks samples, sets the pitch and volume, and starts effects. A note names a macro.
- **Voice:** one of 4 hardware voices on Paula, the Amiga's sound chip. In 7-voice mode (7V) there are 7, with voices 4–7 mixed in software onto hardware voice 3.
- **Tick:** one call of the replayer, about 50 times a second.
- **Row:** a track tick, once every *speed + 1* ticks. Pattern waits count in rows, so a row is much like a tracker row.

The replayer's only output is writes to Paula's registers: sample start (LC), length (LEN), period (PER), volume (VOL) and DMA on or off. A player has to reproduce those writes and how Paula responds to them (§8).

---

## 2. Files

### 2.1 Two-file form
- `mdat.<name>` holds the song data: header, trackstep, patterns and macros.
- `smpl.<name>` holds raw signed 8-bit sample data with no header. Macros address it by byte offset. The start of the file is normally silence, which one-shot samples rely on (macro 19).

All 368 modland songs use this form. The DOS-style names `<name>.mdat` / `<name>.smpl` and `<name>.tfx` / `<name>.sam` are also seen.

### 2.2 Single-file form, TFHD

| Offset | Size | Meaning |
|---|---|---|
| 0x00 | 4 | `"TFHD"` |
| 0x04 | long | Header size, which is also the offset of the mdat data |
| 0x08 | byte | Variant: 1 = TFMX 1.5, 2 = TFMX Pro, 3 = TFMX 7V |
| 0x0A | long | mdat size |
| 0x0E | long | smpl size |

The file is the header, then the mdat, then the smpl. It's a convenient container for passing a pair as one stream. The meaning of byte 0x09, and whether any header fields follow 0x12, isn't known.

---

## 3. The mdat header

The header occupies 0x000–0x1FF.

| Offset | Size | Meaning |
|---|---|---|
| 0x000 | 10 | Magic (see below) |
| 0x00A | word | Unused |
| 0x00C | long | Unused by the player. Early versions kept a PAL/NTSC flag here |
| 0x010 | 240 | Text: 6 lines of 40 characters (song title, composer, date and so on) |
| 0x100 | 32 words | Subsong start positions (trackstep line numbers) |
| 0x140 | 32 words | Subsong end positions |
| 0x180 | 32 words | Subsong tempo (§10.2) |
| 0x1C0 | 8 words | Track mute, one word per track, in some builds (§4.4). Non-zero in 7 files |
| 0x1D0 | long | File offset of the trackstep |
| 0x1D4 | long | File offset of the pattern pointer table |
| 0x1D8 | long | File offset of the macro pointer table |
| 0x1DC | 36 | Padding |

**Magic.** Four forms are in use:

| Magic | Files | Notes |
|---|---|---|
| `"TFMX-SONG "` | 353 | The usual form |
| `"tfmxsong"` | 5 | *Masterblazer* |
| `"TFMX "` then binary | 9 | The 1989 generation (*Rock'n'Roll*): `"TFMX "`, `00 00 01 00`, then zeros, with no text. This is genuine, not a ripper's artefact: the game's copy in memory is the same |
| `"TFMX-SONG\0"` | 1 | |

`"TFMX_SONG "` and `"TFMXSONG"` are also accepted by other players.

**Unpacked and packed layouts.** If all three longs at 0x1D0 are zero (138 of 368 files), the file is unpacked and fixed offsets apply: trackstep 0x800, pattern table 0x400, macro table 0x600. Otherwise the longs give the offsets. Some early builds, including *Turrican 1*'s, only understand the unpacked layout.

**Pointer tables.** 128 longs each, holding file offsets of the patterns and macros. The table length isn't stored. Unpacked files keep all 128 entries, including junk pointers for unused slots, so each entry has to be validated: it should be even, at least 0x200, and inside the file.

Trackstep lines run from the trackstep offset toward the data after it. Junk pointers mean the patterns can't be relied on to bound it, so check each subsong's own lines instead.

**Packed files are pruned.** The packer drops unused patterns and macros and renumbers the rest, so packed and unpacked files of the same song contain the same music but differ byte for byte. For example, pattern 0x20 in modland's *Turrican intro* is pattern 0x00 in the *Soundfactory* copy.

**Subsongs.** A slot is in use if its start or end entry is non-zero (slot 0 can legitimately be 0 to 0 in some files), both lie inside the trackstep, and end isn't before start. Some slots need more care:
- Slot 31 often holds an editor placeholder (start = end = 0x1FF, tempo 5).
- Editors fill unused slots with a line holding only EFFE0000.
- A slot can point past the real trackstep into other data, so check that its first line looks like a command or valid track words.

---

## 4. Trackstep

The trackstep is an array of 16-byte lines, and "position" N means line N. Each line holds either 8 track words, one per track, or a command.

### 4.1 Track words
The high byte is a pattern number or code, and the low byte is a signed transpose, in semitones, added to note numbers. The transpose is taken from every word, including idle ones.

| High byte | Effect |
|---|---|
| 0x00–0x7F | Start this pattern on this track from step 0, clearing the track's wait and loop state |
| 0x80 | Keep the track's current pattern running, with the new transpose |
| 0xFE | Stop the voice given in the low byte (once). The track is then idle |
| 0x90 and above | Track idle. 0xFF is the usual value |

### 4.2 Command lines
A line whose first word is 0xEFFE is a command. The second word selects the command, and the third and fourth words (*w3*, *w4*) are its parameters.

| Command | Files | Meaning |
|---|---|---|
| 0000 | 31 | Stop the song |
| 0001 | 230 | Loop: jump to position *w3*, *w4* times, then continue, so the section plays *w4* + 1 times. 0 means forever, as does FF00, the value *Turrican 1*'s songs use. Some builds ignore the count and always jump (§14.2) |
| 0002 | 20 | Tempo: *w3* = new speed. *w4* = new BPM, if (w4 & 0xF200) = 0 and (w4 & 0x1FF) > 15. A new speed applies from the next row read (§10.1) |
| 0003 | 17 | 7-voice mode, if bit 15 of *w4* is clear. The low byte of *w4* adjusts the tick rate (§11). In a song already in 7V, it restarts the mixer (§11.5) |
| 0004 | 38 | Master volume fade: low byte of *w3* = speed, low byte of *w4* = target (§7.5) |
| other | 1 (FF00) | Ignored |

One file named "(4ch)" uses 0003 with bit 15 set, which leaves it in 4-voice mode.

### 4.3 Advancing
When **any** track's pattern reaches *End* (F0, §5.3), the whole trackstep advances one position. After the subsong's end position it wraps to the subsong's start. The new line is loaded straight away, and **every track is processed again in the same tick**, including tracks that keep their pattern (0x80) and were already processed, so their waits count down twice.

In practice one "master" track ends each pattern with F0, and the others end with *Stop* (F4). F4 is used 18,862 times and F0 24,977 times.

### 4.4 Track mute
In the builds that have it, a non-zero header word at 0x1C0 + 2 × *track* mutes that track. The replayer reads the word again at every row, so a game can mute and unmute a track while the song plays. A muted track runs as usual (its pattern, waits, commands and transpose all go on), but its notes and portamento notes don't start a macro. Voices that are already sounding carry on.

Each build was tested by playing one song with and without its words through that build's own replayer.

| | Builds |
|---|---|
| Honour the words | Rudolf Stember's 4-voice builds (*Logical*, *B.C. Kid*, *Bifi 2*, *Software Manager*), *Masterblazer*'s loader and level select, *Rock'n'Roll* |
| Ignore them | *Turrican 1* (both), *Turrican 2* (both), *Turrican 3* (all three), *Apidya* (both), *GemX*, *Hexuma* (both), *Monkey Island*, *Mega-lo-Mania* (both), *Carl Lewis Challenge*, *Blade of Destiny*, *B.C. Kid*'s 7V build, *Z-Out* (both), *X-Out*, *Fly Harder*, *Abandoned Places* |
| Untested | *Masterblazer*'s title build, *Bundesliga Manager*, *Anstoss* |

Among the Pro-era builds, only Stember's 4-voice ones have it. TFMXreplay honours the words in its Pro, *Logical*, *Masterblazer* and *Rock'n'Roll* variants. No modland song in the Pro-era builds that ignore them sets any word (apart from the two below), so this changes no song.

Only 7 modland files set any word:
- *logical-levelmusic1* to *4* mute track 7, and play that way in the game.
- *softwaremanager-titel2* and *bifi2-ongame* set words that their games' own copies don't have. *Software Manager*'s title song in memory has all eight words at 0, starts 7.7 s earlier and is otherwise the same music. A recording of the game matches it, and not the muted file. TFMXreplay ignores the words in these two files (by checksum). *bifi2-ongame* hasn't been checked against its game.
- *circus attractions-title* is an *X-Out* song, so the word is ignored.

---

## 5. Patterns

A pattern is a series of longs `b0 b1 b2 b3`, run from step 0, on rows only. Commands run until one makes the track wait or stop.

### 5.1 Notes (b0 < 0xF0)

| b0 | Kind | b3 |
|---|---|---|
| 0x00–0x7F | Note, then fetch the next command immediately (same row) | Finetune (signed) |
| 0x80–0xBF | Note, then wait b3 more rows. Not in the *Rock'n'Roll* and *X-Out* builds, where these bytes hang the replayer | Wait count |
| 0xC0–0xEF | Portamento to this note (no retrigger) | Rate |

- The note number is (b0 + track transpose) & 0x3F.
- `b1` = macro number for normal notes, or the portamento speed in ticks per step for portamento notes.
- `b2` high nibble = velocity 0–F, which macro 0D uses as velocity × 3. Low nibble = voice: 0–3, or 0–7 in 7V. In a 4-voice song, voices wrap to 0–3 (*Monkey Island* sends key-ups to "voice 4").
- A normal note resets the voice's macro to step 0, sets "key held", remembers the previous note (for macro 1F) and starts the macro on the voice's next update. It doesn't touch the voice's effects, which only macro commands clear. A locked voice (FC) ignores new notes.
- A portamento note sets the destination period and rate, and keeps the current period as the start point. It doesn't restart the macro, and it becomes the voice's note, so a later 08 builds on it. Used 592 times.

### 5.2 Waits
"Wait n" means the track is next processed n + 1 rows later. This holds for note waits and for F3.

### 5.3 Commands (b0 ≥ 0xF0)

The last column is the number of files using the command.

| b0 | Name | Parameters and effect | Files |
|---|---|---|---|
| F0 | End | Advance the trackstep (§4.3) | 365 |
| F1 | Loop | Back to step `b2b3`, `b1` times, so the section plays `b1` + 1 times. 0 = forever | 223 |
| F2 | Jump | Continue in pattern `b1` at step `b2b3` | 41 |
| F3 | Wait | Wait `b1` + 1 rows | 345 |
| F4 | Stop | Stop this track until the trackstep restarts it. A later F0 is never reached | 357 |
| F5 | Key up | Release the voice in `b2`. Macros waiting for release continue, on the tick after the row in Pro builds | 209 |
| F6 | Vibrato | Voice `b2`: speed `b1 & 0xFE`, depth `b3` (as macro 0C) | 36 |
| F7 | Envelope | Voice `b2` (low nibble): step `b1` toward volume `b3`, every (`b2 >> 4`) + 2 ticks. Older builds ignore the nibble and step every 2 ticks (§14.2) | 105 |
| F8 | Gosub | Save the return point, then act as F2. One level only | 0 |
| F9 | Return | Back to the saved point | 0 |
| FA | Fade | Master volume: speed `b1`, target `b3` (§7.5). Ignored by some builds (§14.5) | 13 |
| FB | Play pattern | Start pattern `b1` on track `b2 & 7`. `b3` looks like a transpose, but it didn't show in traces (§17) | 11 |
| FC | Lock | If `b1` ≠ 0, lock voice `b2` against new notes for `b3` + 2 ticks, counting the FC's row. Used for game sound effects | 18 |
| FD | Cue | No audio effect. Safe to ignore | 7 |
| FE | Stop custom pattern | As F4, and clears the "custom pattern playing" flag | 1 |
| FF | No-op | | 12 |

---

## 6. Macros

A macro is a series of longs `cmd b1 b2 b3` run per voice. Most commands complete at once and the macro carries on to the next command in the same tick. Commands marked ⏸ end the macro's work for the current tick. §6.2 has the exceptions.

Each voice holds this state: current and previous note, velocity, macro pointer, step and wait, loop counter, gosub return, sample begin and length, current and destination period, volume, and the effect states (vibrato, portamento, envelope, AddBegin). It also has flags for key held, effects enabled and lock.

### 6.1 Command table

| Cmd | Name | Parameters and behaviour | Files |
|---|---|---|---|
| 00 | DMA off + reset | Clear all effects, then as 13. `b2b3` has a per-build meaning (§6.3) | 367 |
| 01 | DMA on | Start the sample from the current begin and length. `b1` sets the effects: 0 = from the next tick (§14.2 for builds that differ), 1–7F = run now, 80–FF = off | 367 |
| 02 | SetBegin | Sample begin = 24-bit offset `b1b2b3` into smpl. Stops AddBegin | 366 |
| 03 | SetLen | Sample length = `b2b3` words. 0 = 65,536 words | 366 |
| 04 | Wait ⏸ | The macro resumes `b2b3` + 1 ticks later, so 0 means "continue next tick" | 367 |
| 05 | Loop | Jump to step `b2b3`, `b1` times, so the section plays `b1` + 1 times. 0 = forever. See §6.4 | 171 |
| 06 | Continue | Jump to macro `b1`, step `b2b3`, clearing the loop counter | 79 |
| 07 | Stop ⏸ | Stop this voice's macro until the next note | 367 |
| 08 | AddNote ⏸ | Period from note (current note + signed `b1`) with detune = note finetune + signed `b2b3` (§9) | 362 |
| 09 | SetNote ⏸ | As 08, with note `b1` directly | 231 |
| 0A | Reset | Clear all effects | 30 |
| 0B | Portamento | Every `b1` ticks, slide the period by rate `b2b3` toward the destination (§7.3). Starts from the current period. On its own, with no note to slide to, it does nothing. Rate 0 is no slide: the period follows the notes (measured on 9 builds) | 139 |
| 0C | Vibrato | Speed `b1`, signed depth `b3` (§7.2). Puts the period back to the note's base, so 0C 00 turns the vibrato off and drops its offset | 254 |
| 0D | AddVolume | Volume = (velocity × 3 + `b3`) & 0xFF. Not capped at 64, as Paula's VOL register has rules of its own (§8.3) | 354 |
| 0E | SetVolume | Volume = `b3` | 263 |
| 0F | Envelope | Every `b2` + 1 ticks, step the volume by `b1` toward `b3` (§7.4) | 357 |
| 10 | Loop until key up | As 05, but falls through once the key is released | 33 |
| 11 | AddBegin | Add signed `b2b3` to the sample begin. With `b1` ≠ 0, in builds that have it, it then adds every tick and reverses every `b1` ticks: a sweeping sample start (§7.1) | 172 |
| 12 | AddLen | Length += signed `b2b3` words | 52 |
| 13 | DMA off | Stop the sample, keeping effects. Otherwise as 00 (§6.3) | 70 |
| 14 | Wait until key up ⏸ | Wait for key up, or for `b3` ticks. 0 = indefinitely. If the key is already up, the macro continues at once in the same tick. See §6.4 | 278 |
| 15 | Gosub | Save the return point, then as 06. One level, in every build | 20 |
| 16 | Return | Back to the saved point | 19 |
| 17 | SetPeriod | Period = `b2b3` directly | 23 |
| 18 | SampleLoop | Begin += `b2b3`, odd or not (measured on 12 builds; it only matters for a sample that starts at an odd address), length −= `b2b3` / 2 words. Sets the loop part of a sample | 327 |
| 19 | One-shot | Begin = 0, length = 1 word: the silent start of smpl. A sample plays once and then goes quiet. Stops an AddBegin sweep | 328 |
| 1A | Wait on DMA ⏸ | Hold the macro until `b2b3` + 1 Paula audio interrupts have happened (§6.5) | 22 |
| 1B | Random play | Only partly measured, and not implemented (§17) | 2 |
| 1C | Split key | Jump to step `b2b3` if the note is greater than `b1`, otherwise continue. Varies by build (§14.6) | 12 |
| 1D | Split volume | The same, comparing the voice's volume | 5 |
| 1E | AddVol + note | Unknown. The editor's name suggests `b1` = note, `b2` = 0xFE, `b3` = volume. Only *Masterblazer race* uses it | 1 |
| 1F | SetPrevNote ⏸ | As 08, using the previous note | 28 |
| 20 | Signal | Writes `b2b3` into a cue word in the replayer's variables, for the game to read. No audio effect | 2 |
| 21 | Play macro | Start macro `b1` on voice `b2` with the current note and velocity, and finetune `b3`. Only in some builds (§14.6) | 3 |
| 22–29 | SID | A per-tick wavetable oscillator with a slew filter (§12) | 9 |
| 30 | Same-macro jump | *Gem'Z*: if the voice's previous note started this macro, jump to step `b2b3` (§12) | 4 |
| 31 | Unknown | Safe to ignore | 1 |

Changes to begin and length land at Paula's next loop point (§8). A typical macro is therefore SetBegin, SetLen, DMA on, Wait, then SampleLoop or One-shot, and the first pass plays the attack while later passes play the loop.

### 6.2 What ends a tick
In all builds, 04, 08, 09, 1A and 14 (while waiting) end the macro's tick, as do 07 and the deferred form of 00/13 (§6.3). 17, 13 and 0D don't. 1F ends it in every build except *Turrican 1*'s, and 17 ends it only in *Rock'n'Roll*'s.

**The exception.** After a 00 or 13 with `b1` ≠ 0, the first 04, 08, 09, 1A or 1F doesn't end the tick. That lets a macro cut the old sound and start the new one in a single tick. A later note command in the same macro ends the tick as usual. This applies in the Pro and *Masterblazer* generations, not in *Turrican 1*, *X-Out* or *Rock'n'Roll* (§14.2).

### 6.3 DMA off (00 and 13)

**`b1`, in Pro builds.**
- **`b1` = 0:** DMA off immediately, inside the macro, and the macro continues. It can reach DMA on a tick later, because a note command in between ends the tick.
- **`b1` ≠ 0, any value:** a deferred cut. The tick ends with no DMA write. At the start of the next tick the voice is cut, and the macro continues, normally reaching DMA on in the same tick.

Either way, DMA on lands at the end of the following tick. `b1` only decides whether the old sample is cut now or plays on for one more tick.

With a deferred cut, a DMA on earlier in the same tick still starts the sample, as DMA starts come at the end of the tick and the cut at the start of the next. 00 resets vibrato, portamento and envelope at once, but an AddBegin sweep only stops with the cut, so it takes one more step.

**Other builds ignore `b1`'s deferral** and always cut at once (§14.2).

**`b2b3`** differs three ways:
- **Most builds** ignore it.
- ***GemX* and *Monkey Island*:** if `b3` ≠ 0, it sets the voice's volume, velocity × 3 + `b3` with `b2` = 0, or `b3` otherwise. It reaches Paula at once, even though other volume changes reach Paula a tick late in these builds. This matches the editor's name for the command, "flag / addset / vol". About 180 macros in the collection use it.
- ***Masterblazer* and *Z-Out* title:** `b2b3` goes straight to Paula's VOL and leaves the voice's volume alone.

### 6.4 The shared counter
Commands 05, 10 and 14 share **one counter per voice**, measured on *Masterblazer*, *Apidya* and *Turrican 1*. With L = −1 when idle:

- **05 and 10 with count n:** if L = 0, fall through and set L = −1. Otherwise L −= 1, and if that makes it negative, L = n − 1. Then jump. 10 with the key up falls through and leaves L alone.
- **14 with timeout T, key down:** if L = 0, continue and set L = −1. Otherwise L −= 1, and if negative, L = T − 1. Then wait: the command runs again next tick. With the key up, it continues and leaves L alone.

So a 10 or 05 loop whose body contains a 14 with a timeout never runs out, and a "forever" 05 reached with L = 0 falls through. *masterblazer loader* relies on both. 06, 15 and a new note clear the counter.

### 6.5 Wait on DMA (1A)
1A counts Paula's audio interrupts, which come at each DMA start and at each block end. The first comes right at DMA start, so 1A 0 waits one tick and 1A 1 one full sample loop.

- **Pro:** 01 disables and clears the voice's interrupt, even when DMA is already on, and DMA starting raises a new one. When 1A enables the interrupt, a request already pending is served at once and counts as the first of the n + 1. So DMA on, Wait, 1A 0 continues on the next tick, but DMA on, Wait, DMA on (already playing), Wait, 1A 0 waits for the block end (*apidya (level 1)*).
- **Older builds:** if the interrupt is already pending, 1A clears it and passes at once, ignoring the count. Otherwise it waits for interrupts from that point. Either way the tick ends. *GemX* and *Monkey Island* behave the same way.

*Turrican 2*'s 7V build seems to ignore 1A and always continues on the next tick, though none of its 7V songs use it.

Timing matters here. In 7V a DMA start comes late in the tick (§11.4), which a 1A wait near a tick boundary can show.

---

## 7. Effects

Effects run every tick, after the macro in Pro builds, in this order: AddBegin, vibrato, portamento, envelope, then the master fade. The update in which a new note's macro starts runs no effects (§14.2 for the exceptions).

### 7.1 AddBegin
Sample begin += delta, every tick. After *count* ticks the delta's sign flips and the count restarts. Paula only picks up the new begin at the next loop point, so a sweep gives a PWM-like sound.

Only some builds sweep (§14.5). Without it, 11 is a single add.

### 7.2 Vibrato
The offset starts at 0 and moves by *depth* each tick. A counter starts at speed / 2 (integer division) and reloads with *speed*, and the direction turns each time a decrement reaches exactly 0. That gives a triangle wave, lopsided for odd speeds: speed 5, depth −4 gives −8 … +12. With speed 1 the counter starts at 0 and goes negative, so it never turns and the period keeps moving one way.

- **Pro and *Masterblazer*:** period = base + ⌊base × offset / 2048⌋, rounded toward minus infinity. With depth 16 and speed 6, this gives 417, 421, 424, 428, 431, 434, 438 around 428.
- ***Turrican 1*, *X-Out* and *Rock'n'Roll*:** period = base + offset, unscaled, so the same numbers give a much wider vibrato.

*Rock'n'Roll* and the *X-Out* family mask the speed to even, so 5 acts as 4. Pattern F6 always masks it.

The first non-zero offset comes two ticks after the sound tick. Vibrato keeps running during a portamento, but doesn't write the period. In Pro, 0C puts the period back to the base with the tick's period write, and older builds write it at once.

### 7.3 Portamento
Every *speed* ticks, starting from the note's base period rather than the vibrato-shifted one Paula plays. While a portamento runs, 08, 09, 1F and 17 only set its destination.

- **Pro and *Masterblazer*:** exponential, in 16 bits, keeping 11 bits: period′ = ((period × ((256 ∓ rate) & 0xFFFF)) >> 8) & 0x7FF, truncated with no rounding. For example 428, 414, 401, 388 going up in pitch, and 428, 441, 454 going down.
- ***Turrican 1*, *X-Out* and *Rock'n'Roll*:** linear: period ∓= rate.

Arrival is checked on the whole product, before keeping 11 bits, and the period then snaps to the destination. With normal rates that's a plain clamp. A rate above 256 wraps: *turrican 2 level 5-aliens* subsong 9 uses $3776, which gives a jumping cycle of periods rather than arriving, and *turrican 3 level 2* subsong 7 uses $6237 from 428 toward 401, playing 428, 376, then 401.

In *Rock'n'Roll*, 0B starts from the last note's period, which is the previous slide's destination, even when that slide hadn't got there.

In tracks-first builds, a portamento note on the row straight after a note is cancelled: it's read in the same tick that note's macro runs 00. Real songs put them later.

### 7.4 Envelope
Every *time* + 1 ticks, the volume moves by *step* toward the target, then snaps to it and stops. Time 0 on macro 0F means no change at all over 300 ticks, so it's effectively off. That's used 27 times in 16 files.

### 7.5 Master fade
The master volume runs from 0 to 64. A fade moves it one step toward the target, once per voice per tick whether or not that voice's effects are running, and speed n means each voice steps every n ticks. So it's 4 steps a tick at speed 1 in a 4-voice song, and 7 in a 7V song, where the steps come from the seven real voices and not hardware voice 3. Speed 0 sets the volume at once, in that command.

Output volume = ⌊voice volume × master / 64⌋.

Trackstep fades (EFFE0004) and pattern fades (FA) don't both work in every build (§14.5). The 7V mixer ignores the master volume, so a fade only moves hardware voices 0–2.

---

## 8. Paula

The replayer only sets registers, so a player has to reproduce how Paula behaves.

### 8.1 Sample playback
- DMA on reads `length` words from `begin`.
- When that pass ends, Paula reloads **whatever begin and length the registers hold at that moment** and keeps looping.
- Changing begin or length while a sample plays doesn't restart it. The change takes effect at the next loop point.
- With length 1 (one-shot), the sample effectively stops after its first pass.
- Paula ignores bit 0 of LC, as DMA is word-aligned.
- A period change stretches or shortens the block in progress.
- The minimum period is 113.

### 8.2 The DMA batch
Pro builds batch their DMA writes. At the start of the macro phase there's at most one DMACON clear, for the voices with a deferred cut, with PER = 9 written to each so that the word in progress ends at once. At the end of the macro phase there's a single DMACON set for every voice whose macro ran DMA on this tick, followed by all four PERs. Volumes are written per voice as each macro finishes.

Replayers read INTREQR every tick and clear each voice's audio interrupt as part of DMA off, using the bits as "Paula has started a new block" flags. For example, the loop's LC and LEN (SampleLoop) are written on the tick after DMA on, once the bit shows the first block was latched.

### 8.3 Volume and output
- VOL has 7 bits: 64–127 are full volume, and higher values wrap, so 0x110 is 16.
- Voices are 8-bit signed, volume 0–64.
- Voices 0 and 3 are on the left, 1 and 2 on the right.

### 8.4 The output filter
Bit 1 of CIA-A's port controls the Amiga's LED filter. Every replayer build examined sets the bit, which turns the filter off, and none has code that clears it. The format has no command for it, so a TFMX song can't turn the filter on. *GemX*'s title has a bit-toggle in the game's own code, outside the replayer. The fixed low-pass filter in the machine's output is always there.

### 8.5 After DMA off
**On the hardware,** a voice whose DMA is switched off isn't silenced. Paula stops fetching sample words, but the voice's DAC keeps the last byte it was given, so the voice outputs a constant level, at its current volume, until its volume changes or DMA starts it again. The waveform stops, so nothing is heard while the level holds, but the voices' held levels add up to a DC offset on each output. The 7V mixer behaves the same way (§11.3). On an Amiga the output capacitors drain the offset over about a second. In a WinUAE recording with its filter off it stays.

**TFMXreplay** silences a hardware voice at DMA off (holding applies only to the 7V mixer). The difference shows in three places:
- **At DMA off,** the hardware level stays where it was, where TFMXreplay drops to 0.
- **At the next start,** the hardware steps from the held level to the new sample's first byte, where TFMXreplay steps from 0.
- **On a volume change while held,** the hardware level steps, and TFMXreplay has nothing to change.

So TFMXreplay's steps fall at different moments and have different sizes, and its output has no DC offset. The volume ramping setting softens its step at DMA off.

**Measured.** A version that holds the last byte was compared with WinUAE recordings of ten songs from three games, with each voice fitted on its own (§16.2). Of the 20 sides, it matched better on 13, about the same on 6, and worse on one: *Rock'n'Roll*'s *take off song*, right side, −11.7 dB against −10.3 dB without the hold. It also changes most of the collection: 1,230 of the 1,608 subsongs, by more than −20 dB above 30 Hz in 412 of them. Which byte is held depends on exactly when DMA off lands within the sample, so the time of the write inside the tick matters. That probably explains the one worse side, since *Rock'n'Roll*'s replayer switches each voice on and off at its own point in the tick (§16.2).

**What it means for listening.** In a blind test, a WinUAE recording of *lovely feelings*, TFMXreplay's render without the hold and its render with it were spliced together in random stretches of 3–9 seconds, with crossfades at the joins. No change of source could be heard, though the left side's DC steps showed in a waveform view. The difference is a measurable one, not an audible one, and the hold has been left out for now: holding would change nearly every song for no audible gain, and the one side where it does worse is unexplained.

---

## 9. Periods

The period table has 64 entries and is the same in every build measured:

```
1710 1614 1524 1438 1357 1281 1209 1141 1077 1017  960  908
 856  810  764  720  680  642  606  571  539  509  480  454
 428  404  381  360  340  320  303  286  270  254  240  227
 214  202  191  180  170  160  151  143  135  127  120  113
 214  202  191  180  170  160  151  143  135  127  120  113
 214  202  191  180
```

Entry 0x0C is 856 (ProTracker C-1), 0x18 is 428, 0x24 is 214, and 0x2F is 113 (ProTracker B-3). Entries 0x30–0x3F repeat the top octave.

Macro note commands use the note & 0x3F, with a detune that is the note's finetune plus the signed word `b2b3`:

- **Pro and *Masterblazer*:** period = (table × (256 + detune)) >> 8.
- **Older builds:** period = table + detune.

Frequency = Paula clock / period. The clock in the games is PAL, 3,546,895 Hz.

---

## 10. Timing

### 10.1 Within a tick
Pro builds run the macros and effects for every voice first, in the order 0, 1, 2, [4, 5, 6, 7,] 3, and then the tracks if the row counter has expired. Tracks never write Paula, so a note's macro first runs on the next tick: a note sounds 2 ticks after its row.

Rows are processed every *speed* + 1 ticks. The row counter is reloaded after the rows are read, so a trackstep speed change already applies to the first row of the line after it.

Older builds run the tracks first, and their volume and period writes come before the macro (§14.2).

Starting a song writes VOL 0 and DMA off to all four voices. Pro builds read row 0 during the start call, and older builds on the first tick.

### 10.2 The tempo word
The subsong's tempo word, at header 0x180, comes in two forms:

- **Under 16: speed.** Tracks run every *speed* + 1 ticks, and the tick comes from the frame. 1,990 of the 2,028 header entries use this form.
- **16 or more: BPM.** Speed is 0, and the tempo programs a CIA timer. 38 entries.

**How the tick is clocked.**

| Source | Tick |
|---|---|
| A PAL frame | 71,051 colour clocks: 313 lines of 227. 20.03 ms, 49.92 Hz |
| CIA timer, Pro builds | Timer = 1,790,456 / BPM, on the 709,379 Hz PAL CIA clock. BPM 125 gives $37F3 |
| CIA timer, *Turrican 1* | Only the timer's high byte is set: 7093 / BPM |
| 7V | One mix block on hardware voice 3 (§11.2) |

BPM handling varies by build:
- *Turrican 1* (from tempo 39) and *Monkey Island* (from 20) tick from their own CIA-B interrupt. In *Turrican 1*, the game's frame call only sets a flag that lets the interrupt play.
- The *X-Out* family has no BPM mode: speed = tempo − 16, ticked from the frame.
- Frame-ticked Pro builds treat the tempo as a plain speed.
- *Masterblazer*'s title build only plays a BPM tempo. Given a speed, its timer and play flag stay 0 and it's silent.

---

## 11. 7-voice mode

### 11.1 Overview
Trackstep EFFE0003 with bit 15 of *w4* clear turns on 7V. Notes can then address voices 0–7, and macros run in the order 0, 1, 2, 4, 5, 6, 7, 3. Hardware voices 0–2 play as usual, and hardware voice 3 plays a software mix of voices 4–7, at volume 64.

A common way to find 7V songs is any trackstep containing `EFFE 0003 FFxx 00xx`. 17 files use the command, and 16 songs are really 7V.

### 11.2 The tick and the mix block
The tick is the voice 3 audio interrupt, so it's one block of the mix buffer. With *x* = the signed low byte of *w4*, clamped to ≥ −32, and a mixer setting *s*:

```
words  = round((100 + x) × s / 10)
period = round(3580 / s)
tick   = words × period × 4 / 7,093,790 seconds
```

That's about 20.2 ms at x = 0, whatever the setting, as a higher setting means more words at a shorter period. Rounding moves the tick slightly between settings, so the setting has a small effect on tempo. The x adjustment changes the tempo, so *x* = −18 gives about 60.2 Hz at setting 18, and hand-tuned rates from another player fit the same rule (§18).

**The mixer setting is chosen by the game**, not stored in the song:

| Setting | Period | Used by |
|---|---|---|
| 20 | 179 | *Turrican 3*'s jukebox |
| 18 | 199 | *Turrican 2* title, *Apidya* title, *Carl Lewis Challenge*, *Anstoss*, *Mega-lo-Mania* |
| 17 | 211 | *Apidya*'s end sequence |
| 16 | 224 | *Bundesliga Manager* |
| 14 | 256 | *Turrican 3*'s intro animation, *Hexuma* |
| 13 | 275 | *B.C. Kid*'s jukebox |
| 9 | 398 | *B.C. Kid*'s intro |

One game can use more than one, even for one song: *Turrican 3* plays its title music at 14 in the intro animation and at 20 in its jukebox. TFMXreplay uses the jukebox's, the cleaner of the two, and has a table of the songs that differ from 18. A game's main loop can also reinitialise the mixer while a song plays, so a setting is best read from the running game.

### 11.3 The mixer
Each block, for voices 4–7, one byte at a time:

- **Level:** `(sample × min(volume, 63)) >> 6`, an arithmetic shift, so volume 64 equals 63. There's no scaling down for the number of voices: the bytes add up and clamp at −128 and 127. A voice at full volume is as loud as a hardware voice, and four loud voices distort.
- **Master volume:** ignored.
- **Pitch:** 21.11 fixed point. `step = mix period × 2048 / period`, rounded down, with the position starting at 2047/2048, so the first byte is the sample's first or second. The phase carries across blocks.
- **Loop:** at the block's end the position wraps, keeping its fraction, and the next block is whatever LC and LEN hold. After a wrap, an occasional byte falls one sample off, at most about 0.3% of the bytes of a short loop. The cause is a rounding too small to see.
- **Blocks under 64 bytes** (LEN < 32) are silent.
- **DMA off** doesn't silence the voice: stepping stops and it holds its last sample, a DC level, until the next start.

### 11.4 Timing
Each tick, the handler swaps the double buffer, mixes voices 4–7 into the buffer, which takes most of the tick, and then runs the replayer. The block plays from the next block start.

So the replayer's hardware writes land late in the tick, about **2.4 ms + 0.08 ms for each word of the block** after the tick starts, because the mixing is what takes the time. A sample block started then ends that much later, which a 1A wait can show. The figure was fitted to recordings of eight songs from 90 to 204 words, and they then sit within about 0.6 ms. The real machine never drops a tick.

Period, volume, sample start and length, and DMA off reach the mixer a tick after the same value would reach a hardware voice. A new note is the exception: it's mixed in the tick of its DMA start, with the volume and period as they stood at the 01 command.

01 with `b1` = 0 holds effects back only on hardware voices, not on voices 4–7, in any build tested.

### 11.5 Mixer restart
EFFE0003 with 7V already on restarts the mixer. In that tick the block is mixed at the old length as usual, so the voices step through all of it, but only its first 2 words play. The mix then stays silent for one block of the new length before the next block plays. The ticks wait with it, so each change costs a tick. In *apidya (end)*, this is a gap of about 20 ms at each change.

---

## 12. The SID commands

Macros 22–29 are a **per-tick wavetable oscillator with a slew filter**, named after the C64's sound chip in the editor's strings. Every tick they rebuild a short buffer from a window of smpl, and Paula loops that buffer. They're used in 9 files, including *GemX*, the *Turrican 2* loader and cave level, the *Turrican 3* loader, the *Denny* loader and Factor 5's effects.

| Cmd | Parameters | Meaning |
|---|---|---|
| 22 | `b1b2b3` | Source: 24-bit offset into smpl. Also writes the voice's LC at once, pointing Paula at the voice's buffer, as SetBegin would |
| 23 | `b1` = buffer length in bytes, `b2b3` = source mask | Output length (for example $20 = 32 bytes, 16 words). The source index wraps with `& mask`, which is length − 1 (for example $0F for 16 bytes). Also writes LEN (length / 2) at once, and **starts the buffer updates**: no 29 is needed, and many macros have none |
| 24 | `b2b3` | Read step, 8.8 fixed point ($0100 = one source byte per output byte). `b1` is ignored. Sets the current step, so a running sweep starts again from it |
| 25 | `b1` = count, `b2b3` = signed delta | Per-tick change of the step, in 1/65536 of a source byte ($1000 = +1/16 per tick). The direction reverses every `b1` ticks, a triangle like AddBegin. `b1` = 0 never reverses. Sets only the sweep: without a 24 the step carries on from where it is |
| 26 | `b1b2b3` | Acceleration: the step grows by this much (1/65536 units) on every output byte, which bends the waveform. Values are taken modulo the source size in effect, so $0FFFF0 on a 16-byte source acts as a tiny negative acceleration. Sets the current value, like 24 |
| 27 | `b1` = count, `b2b3` = signed delta | Per-tick change of the acceleration, a triangle as for 25, and like 25 it sets only the sweep |
| 28 | `b1` = count, `b2` = signed delta, `b3` = limit | Slew filter. The limit is a signed byte. `b2` changes it every tick in 1/16 units ($10 = +1 per tick), reversing every `b1` ticks. A limit of 0 means no filtering |
| 29 | `b1` = 1 on, 0 off | 29 00 stops the buffer updates from the next tick; songs follow it with One-shot. 29 01 restarts them, with the filter off, the sweeps cleared and the acceleration at 0. `b3` = 1 (*factor5-fx*) made no measured difference |
| 30 | `b2b3` = step | *Gem'Z*: if the voice's previous note started this same macro, jump to macro step `b2b3`; otherwise nothing. Placed after 23, it skips the following 24 and 25, so a repeated note on the same instrument keeps its step sweep going instead of restarting it |

**Buffer generation, each tick.** This reproduces every measured byte:

```
step  = the current step (24's b2b3 << 8 as 16.16, plus 25's sweep since)
accel = the current acceleration (26's value, plus 27's sweep since)
phase = 0
y     = the filter's value (see below)
for each output byte i in 0 .. length-1:
    step  += accel
    phase += step
    x = smpl[source + ((phase >> 16) & mask)]     # signed byte
    y = slew(y, x, limit)                         # only if a 28 has been given, and limit != 0
    buffer[i] = y                                 # (otherwise buffer[i] = x)
```

- Phase, step and acceleration restart every tick from the current swept values, so the buffer is the same every tick unless something sweeps.
- `slew(y, x, L)`: if x > y, n = y + L; if x < y, n = y − L, with L as a signed byte. If n passes x, or leaves the signed byte range, y = x. Otherwise y = n. With positive L that's a plain slew-rate limit ($10 turns a square into a triangle). Negative L gives odd but deterministic shapes, and real songs use them ($90 in *Denny* and *Gem'Z*). This fits all 12 measured limits.
- **The filter's starting y** differs by build. *GemX* restarts it every tick from a stored value that's normally 0, and the *Turrican 2* small build does the same (in that game the stored value was 52, left over from its own songs). The Pro builds (*Gem'Z*, the *Turrican 3* loader) carry it over: each tick starts from the last value of the tick before.
- **Buffer location:** smpl + 4 + voice × $100. So the first $404 bytes of a SID song's smpl are reserved, and every source offset is $404 or more. 22 and 23 point the voice at it, but a later SetBegin or SetLen replaces that as usual, and a macro can play another voice's buffer with SetBegin.
- **Which ticks rebuild the buffer.** *GemX* rebuilds it every tick while the updates run. The Pro builds and the *Turrican 2* small build rebuild it as part of the voice's effects: only in ticks where the effects run (so not in the tick of a DMA on with `b1` = 0, which holds the effects back), and not in a tick whose macro ran a DMA off. 23 counts as an update in its own tick.
- **Stopping.** 00 (DMA off with reset) stops the updates, as does 29 00. 13 (DMA off without reset) doesn't, so a note can keep playing a buffer that another macro is still updating.
- **Timing:** the *Turrican 2* small build writes the first buffer in the DMA on tick. *GemX* writes it one tick earlier, and its sweeps run one tick ahead. The arithmetic is the same.
- **Macro 30** (*Gem'Z*, *Denny*) breaks the *GemX* build, which doesn't know it.

Measured by reading the buffers out of each game's memory, tick by tick: *Gem'Z* credits matches in every byte of all four voices over the whole song (5:42), apart from the first tick, which starts from a filter value left in the game. The *Turrican 2* loading and *Turrican 3* loader songs match on every voice that can be compared, except one (§17). In listening tests, songs that use SID without 29 (*Gem'Z*, *Denny*, the *Turrican 2* and *Turrican 3* loaders) had notes missing or cut short before these rules.

---

## 13. Song ends and loops

The format has no song length, so a player has to work it out.

- EFFE0000 ends a song.
- Wrapping past the subsong's end position is a loop, as is a backward EFFE0001 forever jump.
- Some songs loop forever inside a pattern (F1 with count 0) and never reach F0. The sequencer depends only on the trackstep and track state, so an exact repeat of that state marks the loop.
- A trackstep line that stops every track is the end of a song, not a loop, even though the state then repeats. 606 of modland's 1,608 subsongs end this way, including 13 songs of 30 seconds or more. TFMXreplay plays on for two seconds so the last notes can ring out.

All but 7 subsongs get a length this way. Those 7 include *X-Out* family songs with BPM tempos, such as *ooops up*.

---

## 14. Replayer builds

### 14.1 Generations
Each game shipped its own build of the replayer, and some shipped several. *Apidya* has a 7V build for its intro and title and a separate 4-voice build for loading and the levels. Different releases of a game carry the same builds, and the builds within one game behave the same way, apart from the differences noted below, so per-game behaviour is well founded.

| Generation | Games |
|---|---|
| *Rock'n'Roll* (1989) | *Rock'n'Roll* |
| *X-Out* family (1990–93) | *X-Out*, *Fly Harder*, *Abandoned Places*. All three have different code layouts but match on every probe, so this is a whole family |
| TFMX 1.5 (1990) | *Turrican 1* |
| *Masterblazer* (autumn 1990) | *Masterblazer*, *Z-Out* |
| TFMX Pro and 7V (1991 on) | *Turrican 2* and *3*, *Apidya*, *B.C. Kid*, *Blade of Destiny*, *Hexuma*, *Mega-lo-Mania* and many others |
| *GemX* (1991) | *GemX*, *Monkey Island*. Macros first like Pro, but a tick late on volume |

The builds don't form a simple line. The differences are a set of independent switches, which arrived at different times and were sometimes changed per game.

### 14.2 The switches
TFMXreplay models these differences as the table below. "T2" is *Turrican 2*, with its 7V and small 4-voice builds. "T3 small" is *Turrican 3*'s cut-down loading-screen build.

| Behaviour | Pro | T2 7V | T2 small | *GemX* | T3 small | *Hexuma* | *Masterblazer* | 1.5 | *X-Out* | *Rock'n'Roll* |
|---|---|---|---|---|---|---|---|---|---|---|
| Macros run before tracks | yes | yes | yes | yes | yes | yes | – | – | – | – |
| VOL and PER written before the macro, so a tick late | – | – | yes | yes | – | – | yes | yes | yes | yes |
| Start call reads row 0 | yes | yes | yes | yes | yes | yes | yes | – | – | – |
| 00/13 `b1` ≠ 0 defers the cut | yes | yes | yes | – | yes | yes | – | – | – | – |
| First note command after that doesn't end the tick | yes | yes | yes | yes | yes | yes | yes | – | – | – |
| EFFE0001 honours its count | yes | yes | yes | yes | yes | yes | yes | – | – | – |
| 1F ends the tick | yes | yes | yes | yes | yes | yes | yes | – | yes | yes |
| 17 ends the tick | – | – | – | – | – | – | – | – | – | yes |
| Note + wait ($80–$BF) exists | yes | yes | yes | yes | yes | yes | yes | yes | – | – |
| Scaled vibrato, portamento and detune | yes | yes | yes | yes | yes | yes | yes | – | – | – |
| Vibrato speed masked to even | – | – | – | – | – | – | – | – | yes | yes |
| A note's first update runs no effects | yes | – | – | – | yes | yes | – | – | – | – |
| 01 `b1` = 0 holds effects back a tick | yes | – | – | – | yes | yes | – | – | – | – |
| F7's time nibble is used | yes | yes | yes | yes | yes | yes | yes | – | – | – |
| 1A counts from the command | yes | yes | – | – | yes | yes | – | – | – | – |

More detail on the tracks-first and volume-late builds:

- **Order of a voice's update.** In 1.5 and the *X-Out* family: effects, then the VOL and PER writes, then the macro. In *Rock'n'Roll*: VOL write, effects, PER write, then the macro. In the *GemX* generation the same writes come before the macro, with macros running before tracks.
- **Note commands** write PER at once in these builds. There's no batched period write at the end of the tick.
- **The tick a note arrives.** In tracks-first builds, the voice isn't updated at all in that tick.
- **The first update after a note in the *GemX* generation** runs only the new macro, with no effects and no VOL or PER writes.
- ***Rock'n'Roll*'s notes, portamento notes and trackstep FE stops** reach a voice at its next update, after its old macro has run once more. In that update the new macro and the effects run, but neither VOL nor PER is written. A macro that starts with a Wait therefore writes nothing at all in that tick.
- **In the *Masterblazer* loader and level-select builds**, at master volume 64, VOL is only written when the voice's unscaled volume changes. So the last step of a fade up to 64 never reaches Paula until the voice's volume next changes, and a 00 VOL write can stick. The title build and *Z-Out* write every tick.
- **In *Hexuma*'s title and in-game builds**, macro vibrato (0C) doesn't reach the hardware voices, though voices 4–7 of the mixer have it. Both builds write VOL and PER only when they change.
- **T3 small** ignores macros 06, 15, 16, 1C and 1D and pattern FC, and has no AddBegin sweep. Everything else tested behaves as in Pro.
- **Pro's AddBegin sweep and the 00 reset.** Every Pro build steps the sweep once more in a deferred 00's tick. *Turrican 2*'s small build runs effects before the macro in that update, so with 00 `b1` = 0 its sweep also steps once more before the cut.

### 14.3 Choosing a build for a song
Nothing in a song says which build it was written for, except that a TFHD header gives a rough variant. TFMXreplay decides in this order:

1. A table of mdat checksums, for songs that can't be told from their contents, each checked on its own game's replayer: *Turrican 2*'s songs, the *Turrican 3* loader, *Hexuma*'s songs, *Masterblazer*'s intro, *Z-Out*'s songs (they look like 1.5 but play on *Masterblazer*'s rules), *Rock'n'Roll*'s intro, *Turrican*'s bonus song and *Logical*'s six songs (a *GemX*-generation build with FA and track mute). A second checksum list turns track mute off for two files (§4.4).
2. `"TFMX "` magic: *Rock'n'Roll*.
3. `"tfmxsong"` magic: *Masterblazer*.
4. 7V: Pro.
5. Ten or more macros using 00's `b2b3`: *GemX*. Most builds ignore it, so only songs written for *GemX* and *Monkey Island* use it much. The next highest is 5.
6. Packed: Pro.
7. No note + wait anywhere: the *X-Out* family.
8. Otherwise: 1.5.

### 14.4 Fingerprints
These are the probe results for every build profiled. Short test songs were run on each build, using distinct SetVol values as markers because Paula only shows each voice's last volume per tick.

| Build | order | note@ | vol | b1 | b1 1st | 04 | 08 | 09 | 17 | 1F | 13 | 05 n=2 / 0 | F1 n=2 / 0 | EFFE1 n=2 | key-up |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| *Turrican 2*, 7V | M>P | 1 | 0 | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| *Apidya*, 7V | M>P | 1 | 0 | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| *Apidya*, 4-voice | M>P | 1 | 0 | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| *B.C. Kid*, *Blade of Destiny* | M>P | 1 | 0 | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| *Software Manager* | M>P | 1 | 0 | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| *Bifi 2* | M>P | 1 | 0 | +1 | cont | end | end | end | cont | end | cont | 3 / 10 | 3 / inf | 3 | −3 |
| *Logical* | M>P | 1 | 1 | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| *Turrican 2* small | M>P | 1 | 1 | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| *GemX* | M>P | 1 | 1 | same | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| *Monkey Island* | M>P | 1 | 1 | same | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| *Masterblazer* level select | P>M | 2 | 1 | same | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | −1 |
| *Masterblazer* loader, race | P>M | 1 | 1 | same | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | −1 |
| *Masterblazer* title (provisional) | P>M | 1 | 1 | same | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 1 |
| *Z-Out* title | P>M | 2 | 1 | same | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | −1 |
| *Z-Out* game | P>M | 2 | 1 | same | end | end | end | end | cont | end | cont | 3 / inf | 3 / inf | (crash) | −1 |
| *Turrican 1* | P>M | 2 | 1 | same | end | end | end | end | cont | cont | cont | 3 / inf | 3 / inf | inf | −1 |
| *X-Out*, *Fly Harder*, *Abandoned Places* | P>M | 2 | 1 | same | end | end | end | end | cont | end | cont | 3 / inf | 3 / inf | inf | −1 |
| *Rock'n'Roll* | P>M | 1 | 1 | same | end | end | end | end | end | end | cont | 3 / inf | 3 / inf | inf | 0 |

Column key:
- **order:** which song data is read first within a tick (M = macros, P = patterns).
- **note@:** the tick of a row-0 note's first macro writes, counted from the start.
- **vol:** ticks from SetVol to the Paula write.
- **b1:** when DMA off with `b1` = 1 cuts the voice, relative to `b1` = 0.
- **b1 1st:** whether the first SetNote after 00 `b1` = 1 ends the tick.
- **end / cont:** whether that command ends the macro's tick.
- **Loop columns:** the number of plays. inf = forever.
- **key-up:** the F5 reaction tick, relative to note timing.

note@ 1 against 2 and the key-up offsets come from whether the build starts the song during the start call or on the first tick. They aren't musical.

### 14.5 Effects by build

| Build | Vibrato and portamento | Even vibrato speed | AddBegin sweep | EFFE0004 | FA |
|---|---|---|---|---|---|
| *Apidya* | scaled | no | yes | yes | ignored |
| *Turrican 2* small | scaled | no | yes | yes | ignored |
| *GemX* | scaled | no | yes | yes | ignored |
| *Monkey Island* | scaled | no | no | yes | yes |
| *B.C. Kid*, 4-voice | scaled | no | yes | yes | yes |
| *Blade of Destiny* | scaled | no | yes | yes | ignored |
| *Masterblazer* | scaled | no | no | yes | yes |
| *Z-Out* title | scaled | no | no | yes | yes |
| *Z-Out* game | scaled | no | no | yes | ignored |
| *Turrican 1* | linear | no | no | yes | yes |
| *X-Out*, *Fly Harder*, *Abandoned Places* | linear | yes | no | ignored | yes |
| *Rock'n'Roll* | linear | yes | no | ignored | yes |

The Stember builds take the first sweep step in the same tick as the 11 command, one tick earlier than the others. The sweep is otherwise the same.

**FA across the Pro-era builds.** It works in *Turrican 2*'s 7V build, *B.C. Kid*'s 4-voice build, *Mega-lo-Mania*'s title build and both *Hexuma* builds. It's ignored by both *Apidya* builds, *B.C. Kid*'s 7V build, *Blade of Destiny*, all three *Turrican 3* builds, *Carl Lewis Challenge* and *Mega-lo-Mania*'s in-game build. Only 13 modland songs use FA, and none was written for a build that ignores it, so TFMXreplay runs FA in every Pro variant. *mlawtitle* subsong 1 needs it.

### 14.6 Rarer commands by build

| Command | Builds |
|---|---|
| 1C, 1D split | Work in the Pro-like builds, *Masterblazer*, *Z-Out* title and *Turrican 1*. *B.C. Kid* and *Z-Out* game always jump. *X-Out*, *Rock'n'Roll*, *Blade of Destiny*, *Fly Harder* and *Abandoned Places* never jump. TFMXreplay's Pro variant has the working form |
| 10, 12, 15, 16 | The same in every build, except that T3 small ignores 15 and 16 |
| 21 Play macro | Only *Apidya* and the Stember builds (*B.C. Kid*, *Blade of Destiny*). Ignored elsewhere, including *Turrican 2* |
| 20 Signal | Seen in *Apidya* 7V, *Blade of Destiny*, *Monkey Island*, *Masterblazer*, *Z-Out* |
| FB Play pattern | Starts the pattern at once (the same or the next tick) in *Turrican 2* (both builds), *B.C. Kid*, *Monkey Island*, *Masterblazer*, *Z-Out* title and *Turrican 1* |
| FD Cue | No audio effect in any build, and the word wasn't stored anywhere in RAM |

### 14.7 Build notes
**Calling a replayer.** Builds start with a jump table of BRA.W entries, 17 to 25 depending on the build, with a similar layout across builds:

| Entry | Use |
|---|---|
| 1 | One tick, in builds that the game calls from an interrupt |
| 3 | Start song: D0 = subsong |
| 5 | Init: D0 = mdat, D1 = smpl, and D2 = the mix buffer in 7V builds |
| 24 | Set the 7V mixer, in 7V builds |

Pro builds also hold internal dispatch tables of 15 entries (pattern commands) and 42 entries, which matches macro commands 00–29 including SID. Games call entry 1 from a copper or CIA interrupt. *Rock'n'Roll* calls through a pointer, A0 = hunk start and `jsr d16(a0)`.

**Sizes.** The 7V builds are 33–36 KB. The small 4-voice builds are about 4.5–5.9 KB.

**Individual builds.**
- *Masterblazer* has three builds, and *Z-Out* two. The title build ticks from its own CIA-B interrupt, writes DMA per voice rather than in a batch, and writes CIA-B timer B once per voice every tick.
- The *Masterblazer* builds address the song with junk ($FF) in the top byte of a pointer, which works on a 68000 with its 24 address lines.
- A subsong with start = end = 0 is treated as unused by *Masterblazer*'s title build.
- *Turrican 1* has two builds, one for the title and one for the game, with identical fingerprints, so 1F not ending the tick is a consistent trait of that game.
- *Turrican 3* has three: 7V (for the intro and the menu's music player, which also plays level songs in 4-voice mode), small (loading music) and in-game. The in-game build gives the same results as the 7V build, apart from skipping voice 2's notes from track 6 in stretches of *level 1* subsong 4, most likely to keep a voice free for sound effects.
- *Turrican 2*'s 7V build differs from the other Pro builds only by running effects in a note's first update, as far as is known.
- *Denny* was never released, so *denny-title* has no game.
- *Bundesliga Manager*'s and *Anstoss*'s 7V builds initialise through operating system library calls, and a game's own title song can have a slightly different copy from modland's. *B.C. Kid*'s `bckid-titel` is 80 bytes shorter in memory.

---

## 15. The songs in modland

- 368 songs (mdat/smpl pairs) in 17 composer folders, 230 packed and 138 unpacked, with 1,608 playable subsongs. One 540-byte file, *hanse-ascon*, has none.
- Core commands used by almost every song: pattern F0, F3, F4 and notes, and macros 00–04, 07, 08, 0D, 0F, 18 and 19.
- Common: F1, F5, F7, and macros 05, 09, 0B, 0C, 0E, 11 and 14.
- Rare (fewer than 25 files each): EFFE0002 and 0003, F2, F6, FA–FF, and macros 0A, 10, 12, 13, 15–17, 1A–1F, 20 and 21.

Chris Hülsbeck's *Soundfactory* CD (1995) has 236 TFMX songs by Hülsbeck and Stember in its Jukebox folder, as `.TFX`/`.SAM` pairs. 169 are byte-identical to modland's, 7 have the same song data with a different header text, 23 share modland's samples but have repacked song data (§3), and 37 match nothing by checksum, though the ones compared are the same songs, packed and renumbered. A few may be different versions: the Jukebox *Turrican 2* title is 9 KB bigger, with patterns 103 and up differing.

---

## 16. How this was measured

### 16.1 Method
The original replayers were run in a 68000 emulator from savestates of the games, with interrupts delivered once per tick and every Paula write logged. Test songs of one or two lines were run through each build to establish single behaviours, and whole songs were then compared with TFMXreplay's output tick by tick: per voice, the DMA on/off transitions and the final LC, LEN, PER and VOL. `tests/compare.py` makes that comparison, using the output of `tfmxtrace`.

Three other references check the first:
- **WinUAE memwatch captures** of the running game, converted by `tests/capture.py`. These are the most direct reference, and they count wherever the emulator's rough CPU timing differs, as with 1A waits in 7V.
- **The 7V mix buffers,** read straight out of the original replayer's memory and compared byte by byte.
- **WinUAE recordings** of the songs, with no filter and no interpolation, compared with TFMXreplay's output as sound.

The replayers were run, not disassembled. The exceptions are automated searches of memory images for jump tables and for writes to particular hardware registers. The savestates hold the games themselves, so they and the test rig aren't distributed, and the comparisons with the originals can't be rerun without them. `tests/selftest.py` guards the same behaviour without them, using small test songs and saved output from a verified build.

### 16.2 What each reference can see
The sound passes through three stages, and each reference sees a different part of it:

1. **The replayer.** The game's 68000 code reads the song and writes Paula's registers every tick: LC, LEN, PER, VOL, and DMA on and off through DMACON.
2. **Paula.** Between writes it fetches sample words from chip RAM by itself, steps through them at the period, reloads LC and LEN when a block ends, and raises the voice's audio interrupt.
3. **The output.** On an Amiga, Paula's DACs, the analogue filter and the output capacitors. In WinUAE, the emulator's conversion of its output to the recording's sample rate.

| | Replayer logic | Game code outside the replayer | Time of a write within the tick | What Paula does with the writes | Output stage |
|---|---|---|---|---|---|
| Harness (savestate replayer) | Yes, exactly | No | No | No (its interrupts are modelled) | No |
| WinUAE memwatch capture | Yes, exactly | Yes, by the writing PC | Order only | Only through the real audio interrupts | No |
| WinUAE recording | Indirectly | Indirectly | Yes | Yes | WinUAE's, not the hardware's |

- **The harness** runs only the replayer, with one interrupt per tick, and logs its writes per tick. A match there means TFMXreplay sends Paula exactly what the original does. It says nothing about the sound, because it doesn't run Paula: what follows a write is only known from recordings.
- **A memwatch capture** logs the writes of the running game: register, value and the PC that wrote it. It adds whatever the game itself writes, and audio interrupts from WinUAE's Paula rather than a model of it. It carries no beam position, so it shows the order of the writes but not their exact times.
- **A recording** is the only reference for Paula's own behaviour, for example when new LC and LEN take effect, the delay before a new sample starts, the shortest period, and what a voice outputs after DMA off. It's also the only one that sees the timing of writes within a tick. But it is indirect: a difference shows that something differs, not why, and WinUAE's output stage adds its own differences, which an Amiga doesn't have.

**Comparing with a recording.** A plain subtraction of TFMXreplay's output from a recording leaves a large residual, which can sound like a lot when played back, even when the two sound alike. As with comparing a WAV and an MP3 of it, most of it is inaudible. In the *Rock'n'Roll* recordings, three things that can't be heard made up nearly all of it:
- **A DC offset.** After DMA off a voice keeps outputting its last sample, so idle voices leave a constant level on the output (+0.051 on the left through *lovely feelings*), which TFMXreplay, at 0, doesn't have.
- **Per-voice timing.** The game writes the voices in turn, about 0.12 ms apart, where TFMXreplay writes all four at once. A fit that gives a side one time offset breaks down wherever its two voices play together.
- **Detail above about 6 kHz.** With no interpolation, the two emulators' sample steps fall differently at 48 kHz, so above about 6 kHz the outputs no longer line up.

Taking those out (a 20 Hz high-pass, a 5 kHz low-pass, and each voice rendered on its own with `tfmxrender -V` and fitted with its own time offset by `tests/wav/voicefit.js`) leaves gains of 0.98–1.00 and residuals of −12 to −16 dB, except *lovely feelings*' right side at −7.6 dB, not yet explained. Between 20 Hz and 6 kHz, *lovely feelings*' left side matches within 0.1 dB.

**Changes and regressions.** `tests/everysong.py` renders every subsong of a collection with two builds of `tfmxrender` and lists the ones that differ, and by how much. It shows which songs a change reaches, not whether the new output is right. That still needs the references above. Whether a difference can be heard is a separate question: `tests/wav/splice.js` joins random stretches of two or more versions into one file for a blind listening test (§8.5 has an example).

### 16.3 Results
Hardware-voice comparisons cover every register of every tick, over 1,500 ticks (30 seconds) of each song unless noted. In 7V they cover voices 0–2.

| Build or games | Result |
|---|---|
| *Apidya*, 4-voice | All seven songs exact |
| *Turrican 1*, both builds | All 14 songs exact, including BPM subsongs |
| *X-Out* family | All 17 songs exact |
| *Rock'n'Roll* | All 10 songs exact. A WinUAE capture of *lovely feelings* in the game: 458 of 458 ticks, and every Paula write comes from the replayer |
| *Masterblazer* | Loader, intro, title and *Z-Out* title exact. *museum* and *lucasarts* differ only in a start-up volume left over from the game. *race* is exact on voices 0–1, and voices 2–3 use 1B |
| *GemX* | All 8 subsongs exact |
| *Super Gem'Z* | *gemz-credits*: all 17,090 ticks of the whole song exact, and two WinUAE captures of the game (732 and 663 ticks) exact, with every Paula write from the replayer. Its SID buffers match too (§12). *gemz-loader* and *gemz-title* exact on the same build |
| *Monkey Island* | 22 subsongs exact apart from start-up leftovers, except that in three of them a 1A wait ends a tick early now and then |
| *Turrican 2*, small build | 13 of 15 subsongs exact (§17) |
| *Apidya*, 7V | End music exact. Title exact against three WinUAE captures, apart from a few notes at the edges of each capture |
| *Turrican 2*, 7V | Title exact |
| *B.C. Kid*, 7V | All 3 title subsongs exact |
| *Carl Lewis Challenge*, *Mega-lo-Mania* | Titles exact on their own builds, plus *Mega-lo-Mania*'s three in-game songs, all 5 subsongs |
| *Hexuma* | Six level songs, 7 subsongs, exact. Title: 463 of 464 ticks of a WinUAE capture |
| *Bundesliga Manager*, *Anstoss* | WinUAE captures of the titles: 507 of 508 and 724 of 726 ticks, the differences being notes that began before the capture |
| *Turrican 3* | 3 of 4 intro subsongs and 29 of 32 level subsongs exact on Pro. The small build matches 4 of its 5 level songs |
| Seven other games' 7V songs | Exact when played through *B.C. Kid*'s 7V build |

**The 7V mix buffers.**

| Song | Ticks identical | Notes |
|---|---|---|
| *Carl Lewis Challenge* | 1,200 of 1,200 | 8,210 of 9,000 over a longer run, the rest nearly all single bytes |
| *Apidya* title | 1,499 of 1,500 | One block length at a mid-song change |
| *Apidya* end song, subsong 1 | 16,230 of 18,100 | All 6:12. 0.1% of bytes differ, mostly single bytes at loop points. Hardware voices: 0 of 18,201 ticks differ |
| *Turrican 3* title music | 14,970 of 15,000 | All 5:08, at setting 20 |
| *Hexuma* title | 7,492 of 7,500 | |
| *Turrican 2* title | 1,997 of 2,000 | See §17 |

**WinUAE recordings.** Recordings of eleven songs from ten games agree with TFMXreplay's output in tempo, level and timing, at gains of 0.96–1.00, with residuals down to −41 dB. *Anstoss*, *B.C. Kid*, *Rock'n'Roll*'s *take off song* and *lovely feelings*, *Turrican 1*'s intro, *Turrican 3*'s jukebox and the *Apidya* end music stay locked for the whole song. *Bundesliga Manager*, *Carl Lewis Challenge* and *Hexuma* lock too, after whole-tick steps of 51, 128 and 98 ms in the first minute or two, and the *Turrican 2* and *Turrican 3* intro versions show similar steps. Those come from the games losing ticks, not from the songs.

The *Apidya* end music's percussion is one 29,504-word sample at period 158, restarted every 128 ticks. At setting 17 that leaves a gap of about 6 ms in which the sample starts again before the restart cuts it off. That click is in the original too.

---

## 17. Open questions

**Not implemented.**
- **Macro 1B** (random play), used only by *Masterblazer race*. Partly measured on the loader build: voice 2 gets a new table note every 6 ticks (`b2` = 06), and voice 3 repeats each one 4 ticks later. With `b3` = 0 it cycles a fixed sequence of 10 notes, and other parameters mostly held one note in the probes. It crashes several other builds.
- **Macro 1E,** only in *Masterblazer race*.
- **Macro 31.** It needs a *Gem'Z*, *Denny* or *Turrican 3* build to measure. 30 is now known (§12), measured on *Gem'Z*'s build only.
- **Pattern FB's `b3`.** In the probes the transpose didn't show, but *Denny* uses 0, 5, 2, 7, which looks like one.

**Known differences.**
- *turrican 3 intro* subsong 1, voice 1 from tick 130: the original leaves a 14 (timeout 16) about 12 ticks early. The voice's previous note ran the same macro and was cut short partway through the same wait, so the shared counter (§6.4) looks like it carries over, but not resetting it at a note breaks dozens of other songs.
- *turrican 3 level 1* subsong 3, voice 1 from tick 355, with periods above $7FF: $A01 against $A0C.
- *turrican 3 level 2* subsong 7: an AddBegin step at tick 227.
- *turrican 2 level 5-aliens* subsong 0: a pattern portamento reaching $100 lands on $FF. The original keeps $FF for a tick, and its vibrato then centres two lower. Not clamping fixes the first tick but not the rest, and breaks songs that do clamp.
- *turrican 2 level 1-desert* subsong 2 differs in 4 ticks: a 1F plays the "previous note" the game's own music left in the replayer.
- *Monkey Island* subsongs 1, 3 and 4: a 1A wait ends a tick early now and then. A 4 KB first block ends within a fraction of a tick of a tick boundary, and the original starts DMA a little way into the tick, its own run time, which the model doesn't have.
- *turrican 3 loader:* voice 2's SID buffer differs in both subsongs (voices 0, 1 and 3 match). The original also rewrites LEN and LC every tick, and some periods differ.
- SID buffers, small remainders: *gemz-loader* subsong 1, voice 0, is out by ±1 in some bytes; *gemz-title* subsong 4, voice 0, by up to about 9, an error 36 dB below the signal; *gemxtitle* subsong 0, voice 1, in 35 of 3,000 ticks. Probably a sweep a step out of phase. None was audible in listening tests.
- *turrican 2 level 2-cave* song 9 (subsong index 8), voice 1 from tick 70: the original slides the period ($29E, $290, $283, …) where TFMXreplay holds $2A0.
- *Denny*: no savestate, so its SID songs rest on the rules measured on the other builds, and on listening.
- *Turrican 3*'s small build, *level 1:* differs on voice 3 from tick 642, probably state left over from the loading music.
- In the 7V mixer, two notes reaching voice 4 a tick apart: the original briefly starts the first.
- *Turrican 2*'s title mix: 3 ticks are a 1F with no previous note, which reads a period from outside the table, so only that build's memory can answer.
- Hardware voices are silenced at DMA off, where the hardware holds the last byte as a DC level (§8.5). The difference can't be heard. A hold model fits recordings better on most songs, but not *Rock'n'Roll*'s *take off song*.
- Hardware write time in 7V is a fitted average (§11.4). The real time varies a little with how much the mixer has to do.

**Unknown or unmeasured.**
- FC in the older builds, because the probe's notes come too close together for them.
- 1A in the builds other than *Apidya*, where the test rig can't show it, and in T3 small.
- *Masterblazer* title build's BPM row timing.
- TFHD byte 0x09.
- *bifi2-ongame*'s mute word, until the song is caught playing in its game (§4.4).
- *Bifi 2*'s build plays a macro loop with count 0 ten times, where the other Pro-era builds loop forever, and its key-up comes 3 ticks earlier (§14.4). Not looked at further, and not modelled.
- The ends of *X-Out* family songs with BPM tempos (§13).

---

## 18. Earlier descriptions

This work began with what had been published about the format, along with a scan of the modland songs:

| Source | Description |
|---|---|
| Pickard | Jonathan H. Pickard, *The TFMX Professional 2.0 Song File Format* (1993–98). Partly reverse engineered and partly from the editor manual, and open about its uncertainty |
| tfmxplay | tfmxplay 0.5, 1.0 and 1.1.7 (Pickard, later David Banz and others; GPL). 1.1.7 adds single-file support and per-song fixes |
| tildearrow | tildearrow's tfmxplay (GPL v2). An independent player written from documentation, guesswork and listening tests, without disassembly. It models Paula's register latching explicitly |
| UADE | UADE's format detection, as copied into tfmxplay 1.1.7 |
| *Soundfactory* | Chris Hülsbeck's CD (1995). The readme of PMS 0.2, a DOS TFMX player by Sebastian Mies, gives hand-tuned playback rates for songs the player got wrong |

These were read as descriptions of the format. Both tfmxplays are GPL and Pickard's file has a restrictive redistribution notice, so TFMXreplay was written from this document instead. No code was copied. The *Soundfactory* CD is commercial, and was used as reference material only.

**Where the traces settled a disagreement.** This is for anyone comparing with those sources.

| Question | Measured answer |
|---|---|
| Unpacked table offsets | 0x800, 0x400 and 0x600 (tfmxplay and UADE). Pickard's 0x600, 0x200 and 0x400 don't parse |
| EFFE0003 | The 7V switch (tfmxplay, UADE), not a fade (Pickard) |
| EFFE0001 and 05/F1 counts | n + 1 plays, 0 = forever (Pickard, tildearrow). tfmxplay's n + 2 is wrong |
| Order within a tick | Macros first in Pro (tfmxplay), tracks first in older builds (tildearrow's order) |
| 00 `b1` | See §6.3. Pickard has `b1` backwards, tildearrow's "always ends the tick" is wrong for `b1` = 0, and tfmxplay's "new style" behaviour is right, though its "lets the pass finish" isn't: the cut comes on the next tick regardless. Its 04, 08, 09, 1A and 1F list is right too |
| 1.5 versus Pro | tfmxplay's "new style" flag was modelling Pro's deferred cut |
| 17 SetPeriod | Doesn't end the tick (tfmxplay), except in *Rock'n'Roll*, the generation Pickard and tildearrow describe |
| SetNote ending the tick | With `b1` = 0 it ends the tick even while DMA is off, so tildearrow's "only while the sample plays" is wrong |
| 13 with `b1` = 0 | Cuts at once and continues. Tildearrow's "end the tick" is wrong |
| 1C, 1D | Greater than (tfmxplay), not less than (Pickard) |
| FC, FD | FC = Lock. Portamento is done by notes $C0–$EF, so Pickard's FC entry is a shifted listing |
| 0D cap | Uncapped, as tildearrow has it |
| Master fade rate | Once per voice per tick, 4 steps at speed 1 (tfmxplay) |
| Tick rate | 49.92 Hz. tfmxplay's 50 Hz is close, and tildearrow's 60 Hz is wrong. The *Soundfactory* table fits the 7V rate formula: *Denny* title 40 (formula 40.7), *Carl Lewis Challenge* 56 (55.6), *Apidya* credits 49 (49.0), *Hexuma* title 60 (61.7), *Apidya* title 45 (43.1–44.6) |
| Clocks | PAL. tfmxplay uses NTSC clocks |
| EFFE0002 | tfmxplay's operator-precedence slip gives a nonsense tempo for the BPM half, so it's no reference for the 20 files using it |
| Periods | tildearrow's formula, 2033.83 / 2^((note + 3) / 12), gives the table to within ±1. Pickard's note names are shifted 6 semitones from ProTracker's, so use periods, not names |

**Trackers and other formats.** A TFHD stream suits libraries that take one input, and the format would map onto a tracker-style engine with voices as channels, flattening the trackstep into patterns. Macros would need something like libopenmpt's instrument scripts, with one large sample and events following Paula's rule at §8.1.

---

This documentation was generated using AI development tools (Claude Code by Anthropic).
