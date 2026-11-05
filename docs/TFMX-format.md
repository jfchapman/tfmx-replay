# TFMX (Chris Hülsbeck) — working format notes

A consolidated description of the Amiga TFMX song format, written for a clean-room implementation.
It combines three sources and a scan of the 368 songs in the modland collection's `modules\TFMX` folder:

| Tag | Source |
|---|---|
| **[JHP]** | Jonathan H. Pickard, *The TFMX Professional 2.0 Song File Format* (`tfmx-format.txt`, 1993–98). Partly reverse engineered and partly from the editor manual. The author says it may be inaccurate. |
| **[TP]** | tfmxplay 0.5 / 1.0 / 1.1.7 (Pickard, later David Banz and others; GPL). The three versions share the same playback logic. 1.1.7 adds single-file support, looping fixes and per-song hacks. |
| **[UADE]** | The UADE format-detection heuristic, as copied into tfmxplay 1.1.7. |
| **[TA]** | tildearrow's tfmxplay (`tfmxplay-tildearrow`, GPL v2). An independent player written from documentation, guesswork and listening tests, without disassembly. It's unfinished: no 7V, fades, gosub, PPat or Lock (it aborts on those). It's tuned against *Turrican 2*. It models Paula's register latching explicitly. |
| **[SF]** | Chris Hülsbeck's *Soundfactory* CD (1995): 236 TFMX songs in the Jukebox folder, and the readme of PMS 0.2, a DOS TFMX player by Sebastian Mies (§14). |
| **[SCAN]** | My scan of the 368 local mdat files (2026-11-01). |

Throughout, **⚠ Disagreement** marks places where the sources conflict, and **? Unknown** marks gaps.
Both are things to settle with traces from the original replayer running in UAE.

This documentation was generated using AI development tools (Claude Code by Anthropic). No code was copied.
Both tfmxplays are GPL and Pickard's file has a restrictive redistribution notice, so the loader was written using these notes, not from those sources.

All values are big-endian. "Word" means 16 bits and "long" means 32 bits. A "tick" (Pickard's "jiffy") is one call of the replayer, 50 Hz by default.

---

## 1. Files and containers

### 1.1 Two-file form (all 368 local songs)
- `mdat.<name>` holds the song data: header, trackstep, patterns and macros.
- `smpl.<name>` holds raw signed 8-bit sample data with no header. Macros address it by byte offset.
- Every local mdat has its smpl partner [SCAN]. tfmxplay also accepts the DOS-style names `<name>.tfx` / `<name>.sam`.

### 1.2 Single-file form, "TFHD" [TP/UADE]
| Offset | Size | Meaning |
|---|---|---|
| 0x00 | 4 | `"TFHD"` |
| 0x04 | long | Header size, which is also the offset of the mdat data |
| 0x08 | byte | Variant: 1 = TFMX 1.5, 2 = TFMX Pro, 3 = TFMX 7V |
| 0x0A | long | mdat size |
| 0x0E | long | smpl size |

The file is the header, then the mdat, then the smpl, and header + mdat + smpl must equal the file size. This is a ready-made container for joining a pair into one stream, which is what libopenmpt needs. It also lets the variant be stated explicitly instead of guessed.
**? Unknown:** byte 0x09, and whether any header fields follow 0x12.

---

## 2. mdat header (0x000–0x1FF)

| Offset | Size | Meaning |
|---|---|---|
| 0x000 | 10 | Magic (see below) |
| 0x00A | word | Unused [JHP] |
| 0x00C | long | Unused by the player. Early versions kept a PAL/NTSC flag here [JHP] |
| 0x010 | 240 | Text: 6 lines × 40 chars (song title, composer, date…) |
| 0x100 | 32 words | Subsong start positions (trackstep line numbers) |
| 0x140 | 32 words | Subsong end positions |
| 0x180 | 32 words | Subsong tempo (§6) |
| 0x1C0 | 8 words | **? Unknown.** tfmxplay calls them `mute` and never uses them. Non-zero in 7 files [SCAN] |
| 0x1D0 | long | File offset of the trackstep |
| 0x1D4 | long | File offset of the pattern pointer table |
| 0x1D8 | long | File offset of the macro pointer table |
| 0x1DC | 36 | Padding |

**Magic** [SCAN]: `"TFMX-SONG "` (353 files), `"tfmxsong"` (5), `"TFMX "` followed by binary (9) and `"TFMX-SONG\0"` (1). UADE also accepts `"TFMX_SONG "` and `"TFMXSONG"`. The 14 files without `TFMX-SONG` are all *Masterblazer* or *Rock'n'Roll*, and they look like a distinct earlier or later variant (§9).

**Unpacked layout:** if all three longs at 0x1D0 are zero (138 of 368 files [SCAN]), fixed offsets apply.
- ⚠ **Disagreement.** [JHP] gives trackstep 0x600, patterns 0x200 and macros 0x400. [TP] and [UADE] use **trackstep 0x800, pattern table 0x400 and macro table 0x600**. The scan parsed all 138 unpacked files sensibly with the [TP] values, so **[JHP] is wrong here.**

**Pointer tables:** up to 128 longs each, holding file offsets of the patterns and macros. The table length isn't stored. tfmxplay stops at the first entry that is unaligned or beyond the end of the file, and the loader should do the same plus bounds checks. Trackstep lines run from the trackstep offset up to the first pattern.

**Subsongs:** a subsong is in use if its start or end entry is non-zero. Up to 32 per file. Many files have several, and those would map naturally to libopenmpt subsongs.
Subsong 31 often holds an editor placeholder (start = end = 0x1FF, tempo 5), which isn't a real song [SF]. Treat subsongs whose positions lie beyond the trackstep as unused.

**Packed files are also pruned** [SF]. The Jukebox copies of songs that modland holds unpacked contain the same music. But the packer drops unused patterns and macros and **renumbers** the rest. For example, pattern 0x20 in modland's *Turrican intro* is pattern 0x00 in the Jukebox copy. So packed and unpacked files of the same song differ byte for byte. Unpacked files keep all 128 table entries, including junk pointers for unused slots, so the loader must tolerate those.

---

## 3. Trackstep (the sequence)

The trackstep is an array of 16-byte lines, and "position" N means line N. Each line holds either 8 track words (one per track) or a command.

### 3.1 Track words
The high byte is a pattern number or code, and the low byte is a signed transpose (in semitones, added to note numbers).

| High byte | Effect |
|---|---|
| 0x00–0x7F | Start this pattern on this track from step 0, clearing the track's wait and loop state |
| 0x80 | Keep the track's current pattern running, but take the new transpose |
| 0xFE | Stop the voice given in the low byte (once), then the track is idle |
| 0xFF | Track off [JHP]. [TP] treats any value ≥ 0x90 as idle |

[TP] updates the transpose from every word, including idle ones.

### 3.2 Command lines (first word 0xEFFE)
The second word selects the command, and words 3 and 4 are parameters.

| Cmd | [SCAN] use | Meaning |
|---|---|---|
| 0000 | 31 files | Stop the song |
| 0001 | 230 files | Loop: jump to position *w3* and repeat *w4* times, then continue |
| 0002 | 20 files | Tempo: *w3* = new speed (the track prescaler, §6). *w4* = new CIA BPM, if (w4 & 0xF200) == 0 and (w4 & 0x1FF) > 15 |
| 0003 | 17 files | **7-voice mode** (tfmxplay calls it "timeshare"): if bit 15 of *w4* is clear, turn on 7V mode and adjust the tick rate (§8) |
| 0004 | 38 files | Master volume fade: low byte of *w3* = speed, low byte of *w4* = target volume |
| other | 1 file (FF00) | Ignored [TP] |

- ⚠ **Disagreement (0003).** [JHP] describes 0003 and 0004 as both being fades. [TP] and [UADE] treat 0003 as the 7V switch, and UADE's 7V detection relies on it. The files using it include *Apidya*, *Turrican 3 intro* and *BC Kid*, which are known 7V songs. **[JHP] is wrong.** One file named "(4ch)" also uses 0003, presumably with bit 15 set.
- ⚠ **Disagreement (0001 count).** [JHP] and [TA] agree: 0 repeats forever, and n jumps back n times. [TP]'s counter makes the section play count + 2 times, so 0 means "play twice". Two sources against one, so **[JHP]/[TA] is most likely right**, but confirm with a trace.
- **Tick-rate formula for 0003 confirmed** [SF]. The Jukebox readme gives hand-tuned playback rates for songs its player got wrong. Its player doesn't implement 0003. Every song in the table uses 0003 (or BPM), and the rates match rate = 50 × 100 / (100 + x) Hz: *Denny* title 40 (formula 40.7), *Carl Lewis Challenge* 56 (55.6), *Apidya* credits 49 (49.0), *Hexuma* title 60 (61.7), *Apidya* title 45 (43.1–44.6). This also supports **50 Hz as the base tick rate** (§6).
- ⚠ **tfmxplay bug (0002).** An operator-precedence slip in [TP] means the BPM half of the command sets a nonsense tempo, so tfmxplay is no reference for the 20 files that use it. Use the [JHP] description and traces.

### 3.3 Advancing
When **any** track's pattern reaches *End* (F0, §4.3), the whole trackstep advances one position. After the subsong's end position it wraps to the subsong's start. The new line is loaded straight away, and [TP] reprocesses all 8 tracks again in the same tick.

In practice one "master" track ends each pattern with F0, and the others end with *Stop* (F4). F4 is used 18,862 times and F0 24,977 times [SCAN].

---

## 4. Patterns

A pattern is a series of longs `b0 b1 b2 b3`, run from step 0. Patterns are processed only on **track ticks**, once every *speed + 1* ticks (§6). Waits count in track ticks, so a track tick is effectively a tracker "row".

Tracks and voices are separate. Each note names its own voice, so one pattern can play chords across several voices, and several tracks can drive one voice.

### 4.1 Notes (b0 < 0xF0)
| b0 | Kind | b3 |
|---|---|---|
| 0x00–0x7F | Note, then fetch the next command immediately (same row) | Finetune (signed) |
| 0x80–0xBF | Note, then wait b3 more rows | Wait count |
| 0xC0–0xEF | Portamento to this note (no retrigger) | Rate |

- The note number is (b0 + track transpose) & 0x3F.
- `b1` = macro number (the "instrument") for normal notes. For portamento notes it's the porta speed in ticks per step.
- `b2` high nibble = velocity 0–F, which feeds AddVolume as velocity × 3. Low nibble = voice (0–3, or 0–7 in 7V mode).
- A normal note resets the voice's macro to step 0, sets "key held", remembers the previous note (for SetPrevNote) and starts the macro on the voice's next tick. It doesn't touch the voice's effects, which only macro commands clear.
- A portamento note sets the destination period and rate. It keeps the current period as the start point and doesn't restart the macro. Used 592 times [SCAN].

### 4.2 Wait accounting
"Wait n" means the track is next processed n + 1 rows later. This holds for both note waits (0x80–0xBF) and F3.

### 4.3 Pattern commands (b0 ≥ 0xF0)
| b0 | Name | Parameters and effect | [SCAN] files |
|---|---|---|---|
| F0 | End | Advance the trackstep (§3.3) | 365 |
| F1 | Loop | Repeat from step `b2b3` up to here `b1` times, 0 = forever. Both sources agree on the counting | 223 |
| F2 | Jump | Continue in pattern `b1` at step `b2b3` | 41 |
| F3 | Wait | Wait `b1` + 1 rows | 345 |
| F4 | Stop | Stop this track until the trackstep restarts it. A later F0 is never reached | 357 |
| F5 | Key up | Release the voice in `b2 & 0xF`. Macros waiting for release continue | 209 |
| F6 | Vibrato | Voice `b2 & 0xF`: speed `b1 & 0xFE`, depth `b3` (as macro 0C) | 36 |
| F7 | Envelope | Voice `b2 & 0xF`: step `b1`, every (`b2 >> 4`) + 1 ticks, toward volume `b3` | 105 |
| F8 | Gosub | Save the return point, then act as F2 (one level only) | 0 |
| F9 | Return | Back to the saved point | 0 |
| FA | Fade | Master volume: every `b1` ticks, step by 1 toward `b3` | 13 |
| FB | Play pattern | Start pattern `b1` on track `b2 & 7` with transpose `b3`. [JHP]: takes effect this tick if the target track is processed later, otherwise next tick | 11 |
| FC | Lock | Lock voice `b2 & 0xF` against new notes for `b3` ticks; `b1` = lock flag (used for game sound effects) | 18 |
| FD | ? | See below | 7 |
| FE | Stop custom pattern | As F4, and clears the "custom pattern playing" flag | 1 |
| FF | NOP | — | 12 |

- ⚠ **Disagreement (FC/FD).** [JHP] lists FC = portamento and FD = Lock. [TP]'s editor-derived name table says FC = Lock and FD = "no entry". [TP] actually implements FC as Lock and FD as setting a cue word (FD `b1&3` ← `b2b3`). Portamento is done by notes C0–EF, so [JHP]'s FC entry looks like a shifted listing, and **FC = Lock** is most likely. FD appears in *Apidya*, *Blade of Destiny*, *Monkey Island* and others. A cue/signal word for game sync would make sense there, and wouldn't affect the sound.

---

## 5. Macros (the sound programs)

A macro is a series of longs `cmd b1 b2 b3` run per voice, usually one step per tick. Most commands complete instantly and the macro carries on to the next command in the same tick. Commands marked ⏸ end the macro's work for the current tick.

Each voice holds this state: current and previous note, velocity, macro pointer, step and wait, loop counter, gosub return, sample begin and length (the "next" values for Paula), the current and destination period, volume, and the effect states (vibrato, portamento, envelope, AddBegin). It also has three flags: key held, effects enabled, and lock.

### 5.1 Command table
| Cmd | Name | Parameters and behaviour | Files |
|---|---|---|---|
| 00 | DMAoff + reset | Clear all effects (envelope, vibrato, portamento, AddBegin), then as 13 | 367 |
| 01 | DMAon | Start the sample from the current begin/length, restarting playback. `b1` sets the effects flag: 0 = effects start next tick, 1–7F = run now, 80–FF = effects off [TP] | 367 |
| 02 | SetBegin | Sample begin = 24-bit offset `b1b2b3` into smpl. Stops AddBegin | 366 |
| 03 | SetLen | Sample length = `b2b3` words. 0 = 65,536 words [TP] | 366 |
| 04 | Wait ⏸ | Wait `b2b3` ticks ([TA] reads a 24-bit count, `b1b2b3`). **? Unknown:** bit 0 of `b1` changes this in [TP] (it ends only the current tick, on the first wait after a note) | 367 |
| 05 | Loop | Jump to step `b2b3` `b1` times, then continue. 0 = forever | 171 |
| 06 | Continue | Jump to macro `b1`, step `b2b3` | 79 |
| 07 | Stop ⏸ | Stop this voice's macro until the next note | 367 |
| 08 | AddNote ⏸ | Period = table[(current note + `b1`) & 0x3F] × (256 + note finetune + signed `b3`) / 256 | 362 |
| 09 | SetNote ⏸ | As 08 with note `b1` directly (no current note) | 231 |
| 0A | Reset | Clear all effects | 30 |
| 0B | Portamento | Every `b1` ticks, multiply the period by (256 ± `b2b3`)/256 toward the destination period, stopping there. Starts from the current period | 139 |
| 0C | Vibrato | Speed `b1`, signed depth `b3` (§5.3) | 254 |
| 0D | AddVolume | Volume = velocity × 3 + `b3`. [TP] caps it at 0x40 | 354 |
| 0E | SetVolume | Volume = `b3` | 263 |
| 0F | Envelope | Every `b2` ticks, step the volume by `b1` toward `b3` | 357 |
| 10 | Loop until key up | As 05, but falls through once the key is released | 33 |
| 11 | AddBegin | `b1` = 0: add signed `b2b3` to begin once. Otherwise add it every tick for `b1` ticks, then reverse direction, repeating (a sweeping sample start) | 172 |
| 12 | AddLen | Length += `b2b3` | 52 |
| 13 | DMAoff ⏸? | Stop the sample, but keep effects. See the ⚠ below | 70 |
| 14 | Wait until key up ⏸ | Wait `b3` ticks or until key up. 0 = indefinitely | 278 |
| 15 | Gosub | Save the return point, then as 06 | 20 |
| 16 | Return | Back to the saved point | 19 |
| 17 | SetPeriod | Period = `b2b3` directly. See the ⚠ below | 23 |
| 18 | SampleLoop | Begin += `b2b3` (even), length −= `b2b3`/2 words. Sets the loop part of the sample | 327 |
| 19 | One-shot | Begin = 0, length = 1 word: the "null sample" (the start of smpl is normally silence). Makes a sample play once and then go silent | 328 |
| 1A | Wait on DMA ⏸ | Hold the macro until the sample has looped `b2b3` (+1?) times | 22 |
| 1B | Random play | **? Unknown** | 2 |
| 1C | Split key | Jump to step `b2b3` depending on the note versus `b1` | 12 |
| 1D | Split volume | The same, comparing volume | 5 |
| 1E | AddVol + note | ? See below | 1 |
| 1F | SetPrevNote ⏸ | As 08, using the previous note | 28 |
| 20 | Signal | Cue word `b1&3` ← `b2b3`. No audio effect | 2 |
| 21 | Play macro | Start macro `b1` on voice `b2 & 0xF` with the current note, velocity and finetune `b3` | 3 |
| 22–29 | "SID" commands | A per-tick wavetable oscillator with a slew filter. Measured: see §15.10 | 9 |
| 30, 31 | ? | **? Unknown.** [TP] ignores 31 ("safe to ignore") | 4, 1 |

### 5.2 Notes on the table
- ⚠ **Disagreement (00/13 `b1`).** There are now three versions.
  - [JHP] says `b1` ≠ 0 stops the voice immediately and the macro continues, while `b1` = 0 stops it at the end of the tick and pauses the macro for a tick.
  - [TP] does the opposite: `b1` = 0 stops it now and continues, while `b1` ≠ 0 lets the current sample finish its pass and ends the tick.
  - [TA] always ends the tick. Its `b1` only chooses whether the effects are cleared after this tick's macro run (`b1` = 0) or at the start of the next one (`b1` ≠ 0).
  This is the top question for traces, since every song uses 00.
- **00 `b3` = volume.** [TA] always sets the voice volume from a non-zero `b3` on 00, and [TP] does it only in its GemX hack (with `b2` choosing whether velocity × 3 is added). The editor's command name, "flag / addset / vol", suggests both are real: `b1` = flag, `b2` = add-or-set, `b3` = volume.
- **13 DMAoff** [TA]: if the sample is playing, stop it and end the tick. Otherwise do nothing and continue.
- ⚠ **Disagreement (17).** [JHP] and [TA] say SetPeriod ⏸ ends the tick, and [TA] does the same for 1F. [TP] continues. Two against one.
- **08/09 ending the tick** [TA]: only while the sample is playing. Before DMAon, the macro carries on so it can set up the sample in the same tick. That's another take on what [TP]'s "new style" flag (below) is trying to capture.
- **0C vibrato speed:** [TA] masks the speed to an even number (`b1 & 0xFE`) in the macro as well as in pattern command F6. [TP] masks it only in F6.
- **0B portamento** [TA]: also stops vibrato.
- **1A wait on DMA:** both players wait for `b2b3` + 1 loop points.
- **0D cap:** [TA] doesn't cap the volume at 0x40, while [TP] does.
- ⚠ **Disagreement (1C/1D).** [JHP] says jump if the note or volume is **less than** `b1`. [TP] jumps if it's **greater than**. Only 12 and 5 files use them.
- ⚠ **Disagreement (1E).** [JHP] and the editor's name table say "AddVol + note" (`b1` = note, `b2` = 0xFE constant, `b3` = volume): AddVolume, then AddNote. [TP] calls it "random limit" and ignores it. [TP] also rejects 0D/0E when `b2` = 0xFE, which hints that some versions encode AddVol + note as 0D with `b2` = FE. Only *Masterblazer race* uses 1E.
- **Which commands end the tick.** This is a version difference. [TP] tracks a "new style macro" flag: after a 00/13 with `b1` ≠ 0, the next 04/08/09/1A/1F *doesn't* end the tick, and 01 skips re-latching the sample. This is tfmxplay's heuristic for TFMX 1.5 versus Pro behaviour, and its README admits that 1.5 songs (*Turrican* and others) sound broken. It needs traces from one 1.5 song and one Pro song.

### 5.3 Effects (run every tick after the macro, if the effects flag allows)
Order in [TP]: AddBegin, vibrato, portamento, envelope, master fade.

- **AddBegin:** begin += delta. After the set number of ticks, the delta's sign flips and the count restarts.
- **Vibrato:** offset += depth, and period = destination period × (2048 + offset) / 2048. The first direction change comes after speed/2 ticks, then every *speed* ticks, giving a triangle wave centred on the note (starting at the rising zero crossing, as [JHP] says). Vibrato doesn't write the period while a portamento is running.
- **Portamento:** every *speed* ticks, period × (256 − rate)/256 going up in pitch (with −128 rounding in [TP]), or × (256 + rate)/256 going down. It snaps to the destination and stops once it passes it.
- **Envelope:** every *time* ticks, the volume moves by *step* toward the target, then snaps to it and stops. [TP] counts *time* + 1 ticks per step for macro 0F.
- **Master fade:** every *speed* ticks, the master volume moves 1 toward the target (0–64).
  - ⚠ [TP] runs this inside each voice's effects, so it steps 4 times per tick (8 in 7V mode). That's probably wrong, and needs a trace.
- **Output volume** = voice volume × master volume / 64, clamped to 0–64.

### 5.4 Sample playback (Paula semantics)
The real replayer only sets Paula's registers. The loader has to reproduce how Paula behaves:
- DMAon starts reading `length` words from `begin`.
- When that pass ends, Paula reloads **whatever begin and length the registers hold at that moment** and keeps looping. So a typical macro is SetBegin, SetLen, DMAon, wait a tick, then SampleLoop (or One-shot). The first pass plays the attack and later passes loop the sustain part. ProTracker-style sample swapping works the same way.
- With length 1 (One-shot), the sample effectively stops after its first pass.
- Changing begin or length while a sample plays doesn't restart it. The change only takes effect at the next loop point.
- AddBegin sweeps change the loop start while the sample plays, which gives a PWM-like sound.
- [TA] models this explicitly. While the sample plays, SetBegin, SetLen, AddLen, SampleLoop, One-shot and one-off AddBegin all go into "pending" registers, which are applied at the next loop point. Before DMAon they take effect at once. [TP] gets much the same result by refreshing the "next" begin/length every tick and only reading them at the loop point. Both agree that the loop point is where changes land.
- **Volume while playing** [TA]: AddVolume/SetVolume (and 00's volume) take effect on the next tick, not straight away. **? Unknown:** whether that's real or an artefact of [TA]'s ordering.

### 5.5 Periods
- [TA] computes periods from a formula instead of a table: 2033.83 / 2^((note + 3) / 12), × (256 + detune) / 256. That gives the same periods as [TP]'s table to within ±1. A comment in [TA] gives 2015.28 as the PAL constant.
- [TP]'s note table has 64 entries. Entries 0x00–0x2F are four octaves of standard Amiga periods, from 1710 down to 113. Entry 0x0C is 856 (ProTracker C-1), 0x18 is 428, 0x24 is 214, and 0x2F is 113 (ProTracker B-3). Entries 0x30–0x3F repeat the top octave ([JHP]'s "!" notes).
- [JHP]'s note names are shifted 6 semitones from ProTracker's (it calls 0x1E "C-3", which is period 303). Its "$1E = 8363 Hz" claim doesn't fit the table either. **Use periods, not names.**
- Frequency = Paula clock / period. [TP] uses the NTSC clock, 3,579,545 Hz. **? Unknown:** PAL (3,546,895 Hz) would be about 1% lower, and the same question applies to the tick rate in §6.

---

## 6. Timing
- **The tick** comes from a CIA timer. [TP] uses the NTSC CIA clock (715,909 Hz) with a count of 14,318, which gives 50 Hz. **? Unknown:** PAL or NTSC clocks?
  - ⚠ **Disagreement:** [TA] defaults to **60 Hz**. Its BPM formula is the same as [TP]'s, though. The Jukebox tempo table [SF] only fits the 7V rate formula with a **50 Hz** base (§3.2), so 50 Hz is most likely right.
- **Subsong tempo word** (header 0x180) [JHP and TP agree]:
  - **< 16:** "speed". Tracks are processed every *speed + 1* ticks, so 0 means every tick and 1 every other tick. 1,990 of 2,028 subsongs use this form [SCAN].
  - **≥ 16:** "BPM", with a beat of 24 ticks, so ticks per second = BPM × 24 / 60 (BPM 125 gives 50 Hz). Speed is 0 in this case. 38 subsongs [SCAN].
- **Within a tick:** [TP] runs the macros and effects for every voice first (order 0, 1, 2, [4, 5, 6, 7], 3), then the tracks, if the speed counter has expired. A note from a track therefore starts its macro on the next tick.
  - ⚠ **Disagreement:** [TA] runs the tracks first, then the macros, so a note's macro starts in the same tick. That's a one-tick (20 ms) difference in every note's timing relative to effects, and a trace should settle it easily.
- **Game hacks in [TP]:** *Oops Up* forces speed 5, so two rips evidently have wrong tempo data. *Danger Freak* forces finetune to 0.

---

## 7. Mixing
- 4 Amiga voices, panned like Paula: voices 0 and 3 left, 1 and 2 right.
- Volume 0–64 per voice, 8-bit signed samples.

## 8. 7-voice mode (TFMX 7V)
- It's turned on by trackstep command EFFE0003 with bit 15 of *w4* clear. The low byte of *w4*, as a signed value clamped to ≥ −32, adjusts the tick rate: timer = 14,318 × (100 + x) / 100.
  - **Measured (§15.15):** the tick is one block of the mix buffer on hardware voice 3, which is round((100 + x) × 1.8) words at period 199 with mixer setting 18. That's 20.2 ms at x = 0, the same rate as the timer formula (14,318 CIA ticks = 20.18 ms). Mixer setting 12 (98 words at period 298 for x = −18) gives the same rate.
- Voices 4–7 exist. Notes can address voices 0–7, and macros run in the order 0, 1, 2, 4, 5, 6, 7, 3.
- Hardware voice 3 is replaced by a **software mix of voices 4–7**, at volume 64. §15.21 has the mixer in full. In short, each voice adds `(sample × min(volume, 63)) >> 6` to one 8-bit sum, clamped to ±127/−128, so a voice at full volume is as loud as a hardware voice. The mix rate is a setting each game chooses, not song data.
- [UADE] detection: any subsong's trackstep containing `EFFE 0003 FFxx 00xx`. Plus two hard-coded titles: BMWi and the B.C. Kid title.

## 9. Things nobody has documented
- **Macros 22–29, "SID"** (GemX/GemZ, the *Turrican 2* loader and cave level, the *Turrican 3* loader, the *Denny* loader, Factor 5 FX: 9 files). Editor strings name them: SID setbeg / setlen, op2 and op3 offset and frequency, op1 filter (speed / amplitude / time constant), and SID stop. They appear to synthesise a waveform into a buffer in real time, in the style of the C64's SID chip. [TP] doesn't implement them, and plays GemX with a partial hack. Needs a disassembly-level study of the behaviour (black-box traces of the generated buffer).
- **Macros 1B (random play), 1E, 30, 31.**
- **The Masterblazer / Rock'n'Roll group** (14 files with non-`TFMX-SONG` magic). The macro scan finds bytes 0x83–0x9F in *Masterblazer race* and *Masterblazer title*. That's either an extended command set or my scanner wandering into data. Handle this group separately, later.
- **Header words at 0x1C0.**

## 10. Detecting the variant
- TFMX 1.5 and Pro differ in macro behaviour (§5.2), but the mdat doesn't say which one it is. UADE guesses 1.5 from header text and layout quirks, with per-game exceptions. tfmxplay computes the guess but never uses it. TFHD files carry the variant explicitly (§1.2).
- For the loader: 7V is detectable reliably (§8), 1.5 versus Pro needs a heuristic, and traces should show which behaviours actually differ.

---

## 11. Corpus summary [SCAN]
- 368 songs (mdat/smpl pairs) in 17 composer folders. 230 packed, 138 unpacked.
- Core commands used by almost every song: pattern F0, F3, F4 and notes; macros 00–04, 07, 08, 0D, 0F, 18, 19.
- Common: F1, F5, F7; macros 05, 09, 0B, 0C, 0E, 11, 14.
- Rare (fewer than 25 files each): EFFE0002/0003, F2, F6, FA–FF; macros 0A, 10, 12, 13, 15–17, 1A–1F, 20, 21.
- Undocumented: SID (9 files), the Masterblazer / Rock'n'Roll group (14), macros 30/31 (5).
- So a loader that handles everything documented above should cover roughly 340+ songs, before any accuracy problems.

## 12. Questions for UAE traces, in priority order
1. DMAoff `b1` semantics (§5.2): three different versions, and every song uses it.
2. Order within a tick: macros before tracks ([TP]) or after ([TA]) (§6).
3. Which commands end a macro tick, and how 1.5 differs from Pro (§5.2). Compare one *Turrican 2* song (1.5) with one Pro song.
4. Paula re-latching around DMAon/DMAoff and the first tick after a note (§5.4, §6). Also when effects start (the DMAon `b1` flag), and whether volume changes are delayed a tick.
5. PAL or NTSC clocks for the tick and the periods. The 50 Hz base looks settled.
6. The EFFE0001 loop count (probably as [JHP]/[TA]), and EFFE0002 BPM changes.
7. Master fade speed. Split key/volume comparison direction. Pattern FC/FD.
8. 7V software mix: rate and resolution (*Apidya* title).
9. SID macros (*GemX* title), 1B/1E (*Masterblazer race*).

**Correction (trace session, 2026-11-01):** the *Turrican 2* title (modland `turrican 2 level 0-intro`) is a **7V** song, not a 4-voice 1.5 song. Its trackstep line 0 is `EFFE 0003 FF00 00EE`, and its notes use voices 0–2 and 4–7. It uses both DMAoff variants on hardware voices (b1=0 in macros 0E/0F/14/15, b1=1 in most others), so it answers questions 1, 2 and 8 together. *Turrican 2 loading* uses the SID macros 22–29. A 4-voice *Turrican 2* song for the 1.5 question still needs choosing.
Also on order within a tick (question 2): tracks never write Paula, so the order only shifts every macro by one tick against the track timeline. That's a uniform latency, observable only from the init call, and of low importance for the loader.

Suggested trace songs: a simple 4-voice Pro song first. Then *Turrican 2* (1.5), *Apidya (title)* (7V), *Monkey Island* (FD/FC and EFFE0002), *GemX title* (SID) and *Masterblazer race*.

## 13. How it might map onto libopenmpt (first thoughts)
- **Channels = voices** (4, or 7 for 7V), not tracks, because notes address voices directly. The trackstep and the 8 tracks get flattened into ordinary patterns, one row per track tick, with subsongs as separate order sequences. Speed comes from the tempo word, with every tick = one libopenmpt tick.
- **Macros become InstrumentSynth scripts** under new `TFMX_*` event types, like the Future Composer and Face The Music loaders. Effects (vibrato, portamento, envelope, AddBegin) live in the script state.
- **Samples:** macros address the smpl file by byte offset and change begin/length while playing. The simplest faithful model is one large sample (the whole smpl file), with script events that set the next begin/length and DMA on/off and follow Paula's re-latch rule. This is close to FTM's SetSampleStart/SetRepeatLength events and the general SampleOffset events.
- **Input:** a single stream, either a TFHD file or a joined mdat+smpl. modplayer-web could serve pairs as TFHD.

## 15. Trace results [TRACE]

**Method.** I ran the original replayer from a WinUAE savestate of the *Turrican 2* title, in a 68000 emulator: the tfmx-trace reference harness (`harness.py`, `run.py`). I delivered its interrupt once per tick and logged every Paula write. It's black-box: the code was executed, never read.
I checked it against a WinUAE console memwatch capture from the same savestate (`compare.py`): **231 of 231 ticks identical**, every write in order. The run stopped matching only when the game's main loop re-initialised the 7V mixer (see below).
The game's song data is byte-identical to modland's `turrican 2 level 0-intro` pair. This is a 7V song, so it says nothing yet about TFMX 1.5.

**Tick structure (this replayer, 7V).**
- The tick is the voice 3 audio interrupt. Each tick:
  1. Swap the mix double buffer.
  2. Mix voices 4–7, which takes most of the tick.
  3. Run the macros and effects for voices 0, 1, 2, 4, 5, 6, 7 and write Paula.
- The tick rate equals the buffer rate: 296 samples × period 199 at PAL ≈ 60.2 Hz, with the song's EFFE0003 x = −18. Later the game's main loop switched the mixer to 196 samples × period 298 (≈11.9 kHz, ≈60.7 Hz) by calling a separate replayer routine. So the 7V mix rate is a player setting, not song data.
- **Paula DMA is batched.** At the start of the macro phase there's at most one DMACON clear, for the voices to restart (§ b1≠0 below), with PER = 9 written to each of them so the word in progress ends at once. At the end of the macro phase there's a single DMACON set for every voice whose macro ran DMAon this tick, followed by all four PERs. Volumes are written per voice as each macro finishes.
- The replayer reads INTREQR every tick, and clears and disables each voice's audio interrupt bit as part of DMAoff. It uses the bits as "Paula has started a new block" flags. For example, the loop LC/LEN (SampleLoop) is written on the tick after DMAon, once the bit shows the first block was latched.

**DMAoff (macro 00), settled for this replayer** (question 1):
- **b1 = 0:** DMACON clear **immediately**, inside the macro, and the macro **continues**. In macro 0E (00 00, SetBegin, SetLen, SetVol 0, AddNote, AddVol, DMAon), tick T writes DMA off, LC, LEN, VOL = 0 and PER. Tick T+1 writes VOL = velocity × 3 + 0x14 and DMA on. So **AddNote ended tick T**.
- **b1 = 1:** tick T only clears the voice's interrupt bit and **ends the tick**, with no DMACON write. At the start of tick T+1, the batched DMACON clear and PER = 9. Then the macro continues: SetBegin, SetLen, AddVol, AddNote **and DMAon, all in tick T+1**. So here **AddNote did not end the tick**.
- Both variants put DMAon at the end of tick T+1. b1 only decides whether the old sample is cut at T or played on until T+1.
- So of the three earlier readings, [JHP] has b1 backwards, [TA]'s "always ends the tick" is wrong for b1 = 0, and [TP]'s "new style" behaviour is right: after 00 with b1 ≠ 0, the next 08 doesn't end the tick. [TP]'s "b1≠0 lets the pass finish" is wrong for this replayer, which cuts at the next tick regardless.

### 15.1 Test songs through the same replayer
`player.py` loads any mdat/smpl into the game's replayer through its own jump table at $3F522, using the entries the game itself calls:
- 5 = init (D0 mdat, D1 smpl, D2 7V buffer);
- 24 = 7V mixer setting;
- 3 = start song (D0 subsong);
- 1 = one 4-voice tick, which the game calls from its CIA interrupt.

Without EFFE0003 the replayer runs in 4-voice mode. `testsong.py` builds minimal songs: one trackstep line, one pattern and a few macros. Paula only shows each voice's last volume per tick, so the macros below use distinct SetVol values as markers.

- **Order within a tick: macros first, then tracks** ([TP] is right, [TA] is wrong). In every tick that touches both, the replayer reads macro data before pattern data. Row 0 is read on the first tick after "start song", at any speed, and its note's macro first runs on the next tick. So a note sounds 2 ticks after its row with DMAoff b1=0 (cut at T+1, DMAon at T+2), or with b1≠0 (cut and restart together at T+2).
- **Rows** are processed every speed + 1 ticks (speeds 0, 1, 2 and 5 checked).
- **DMAoff b1:** any non-zero value (1 or 3) behaves as described in §15. Confirmed in 4-voice mode too.
- **Wait (04) n:** ends the tick, and the macro resumes n + 1 ticks later. Wait 0 means "continue next tick".
- **Commands that end the macro's tick:** 04 Wait, 08 AddNote, 09 SetNote, 1F SetPrevNote.
  - The one exception: the first note command after a 00 with b1≠0 does **not** end the tick, so DMAon lands in the same tick. A later 08/09 in the same macro ends the tick again.
  - With b1 = 0, SetNote ends the tick even while DMA is off ([TA]'s "only while the sample plays" is wrong).
- **Commands that do not end the tick:** 17 SetPeriod ([TP] is right; [JHP]/[TA] are wrong), 13 DMAoff and 0D AddVol.
  - 13 with b1 = 0 on a playing voice writes DMACON at once, inside the macro, like 00 b1=0. The macro then continues ([TA]'s "end the tick" is wrong).

- **Loop counts** (Turrican 2 replayer): macro 05, pattern F1 and trackstep EFFE0001 all mean "jump back n times", so the body plays **n + 1** times, and **0 = forever**. [JHP]/[TA] are right; [TP]'s n + 2 is wrong.
- **Key-up (F5)** reaches a macro waiting in 14 on the tick after the row (because macros run first).

### 15.2 TFMX 1.5 (Turrican 1) vs Pro (Turrican 2)
From a savestate of the *Turrican* title.
- The game's title song is a different revision of modland's `turrican intro`. The trackstep and samples are the same, but many patterns differ. That doesn't matter here: the test songs run on its replayer.
- The replayer is at $1BB20, with the same jump-table layout (22 entries). The game calls 5 (init: D0 mdat, D1 smpl), 10 (D0 = $40), 3 (start) and then 1 from its copper interrupt, once per frame.
- It only understands the **unpacked layout** (fixed table offsets) and ignores the pointers at 0x1D0. Test songs now carry both.

| Behaviour | 1.5 (Turrican 1) | Pro (Turrican 2) |
|---|---|---|
| Song start | header and trackstep read on the first tick; row 0 on the second | row 0 read during "start song" |
| Order within a tick | tracks, then macros | macros, then tracks |
| Note row to first macro step | next tick | next tick |
| Pattern command (F5 key-up) to the macro | same tick | next tick |
| Volume to Paula | **one tick late**: VOL is written before the voice's macro runs | same tick |
| DMAoff b1 | **ignored**: always "cut now and continue" (Pro's b1 = 0 behaviour) | b1 = 0 cut now; b1 ≠ 0 cut and restart on the next tick, with that tick's first note command not ending the tick |
| Ends the tick | 04, 08, 09 | 04, 08, 09, **1F** |
| Doesn't end the tick | 17, 13, 0D, **1F** | 17, 13, 0D |
| EFFE0001 | **count ignored**: always jumps | count n gives n + 1 plays, 0 = forever |
| Macro 05, pattern F1 | count n gives n + 1 plays, 0 = forever | the same |

- Turrican 1's own songs only use EFFE0001 at the end of a subsong, with w4 = FF00, as "loop the song". So a loader can treat w4 = 0 or FF00 as forever, and otherwise use the Pro count.
- This is what tfmxplay's "new style" flag was trying to model: the b1 ≠ 0 behaviour is Pro-only.
- The loader needs a 1.5/Pro switch for: DMAoff b1, 1F, the volume latency, and the order of tracks and macros. The last one only matters for pattern commands that act on running macros (F5, F6, F7) relative to notes.

### 15.3 Several builds per game, and a behaviour fingerprint
Each game ships its own replayer build, sometimes more than one:
- **Apidya:** a 7V build (25 entries, 33.7 KB) for the intro and title, and a separate 4-voice build without a hunk header (25 entries, at $4DE20) for loading and the levels.
- **Turrican 2:** the 7V build, plus a small 15-entry build (5.8 KB) that the game calls a lot (not yet profiled).

`survey.py` finds builds by their BRA.W jump tables and fingerprints each one by its table layout. It also finds each Pro build's internal dispatch tables: 15 entries (pattern commands) and **42 entries, which matches macro commands $00–$29, including the "SID" range**.

`fingerprint.py` runs the same probe songs on each profiled build:

| Build | order | note@ | vol | b1 | b1 1st | 04 | 08 | 09 | 17 | 1F | 13 | 05 n=2 / 0 | F1 n=2 / 0 | EFFE1 n=2 | key-up |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Turrican 2, 7V | M>P | 1 | 0 | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| Apidya, 7V | M>P | 1 | 0 | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| Apidya, 4-voice | M>P | 1 | 0 | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| Turrican 1 (1.5) | P>M | 2 | 1 | same | end | end | end | end | cont | cont | cont | 3 / inf | 3 / inf | inf | −1 |

Column key:
- **order:** which song data is read first within a tick (M = macros, P = patterns).
- **note@:** the tick of a row-0 note's first macro writes, counted from start.
- **vol:** ticks from SetVol to the Paula write.
- **b1:** when DMAoff b1 = 1 cuts the voice, relative to b1 = 0.
- **b1 1st:** whether the first SetNote after 00 b1 = 1 ends the tick.
- **end / cont:** whether that command ends the macro's tick.
- **Loop columns:** the number of plays; inf = forever.
- **key-up:** the F5 reaction tick, relative to note timing.

| Rock'n'Roll (1989) | P>M | 1 | 1 | same | end | end | end | end | **end** | end | cont | 3 / inf | 3 / inf | inf | 0 |

So far the Pro builds agree with each other on every probe, and 1.5 differs on order, start tick, volume latency, b1, 1F and the EFFE0001 count. Two behaviour families look enough for a loader. More games, especially other composers' and later Factor 5 titles, should be fingerprinted before relying on that.

### 15.4 Rock'n'Roll (1989): an earlier generation
From savestates of the *Rock'n'Roll* title and level 1.
- **Song data.** The game keeps several songs in memory at once: *high times*, *rockomotion*, *take off song* and (in level 1) *lovely feelings*, the level 1 music. All are byte-identical to modland's, including the odd header: `"TFMX "`, then `00 00 01 00`, then zeros, with no text. They share one sample bank at $3912C.
  So the odd-magic header (§2) is genuine for this generation, not a ripper's artefact. The rest of the layout is the usual unpacked one: subsongs at 0x100/0x140/0x180, tables at 0x400/0x600, trackstep at 0x800.
- **Replayer.** A 4.6 KB hunk with a **20-entry** jump table at $37F00, in a layout unlike the other builds. The game calls it with A0 = hunk start and `jsr d16(a0)`, so absolute-call scans miss it. The entries are the same as in later builds: 5 = init (D0 mdat, D1 smpl), 3 = start, 1 = tick. 10 is called with D0 = $100000 (fade), and 8 is called for each voice 0–3.
- **No "note + wait" form.** Pattern bytes $80–$BF hang the replayer. None of the 9 Rock'n'Roll songs uses them, and every note is a plain note followed by F3. Porta notes ($C0–$EF) are used (5 in *lovely feelings*). *Masterblazer* also has the odd header but uses note + wait heavily, so it needs a later build than Rock'n'Roll's.
- **Fingerprint** (table above):
  - Like 1.5: tracks before macros, one-tick volume latency, DMAoff b1 ignored, EFFE0001 count ignored.
  - Unlike any other build: **17 SetPeriod ends the tick** ([JHP]/[TA] describe this generation).
  - Like Pro: **1F ends the tick**.
- **Three generations so far:** Rock'n'Roll (1989), Turrican 1 / 1.5 (1990), and Pro (Turrican 2, Apidya, 1991). A loader needs to tell at least these apart:
  - an odd `"TFMX "` header with no note + wait means the 1989 generation;
  - an unpacked `TFMX-SONG ` file means 1.5 or Pro (needs a further hint);
  - a packed file or 7V means Pro.

### 15.5 Masterblazer (1990): two more builds, in between
From savestates of the *Masterblazer* loading screen and title. The disk image used may be bad: it doesn't get into the first level.
- **Loading music** is `masterblazer loader`, with the `tfmxsong` magic. It's played by a 22-entry build at $7AD20, set up with 5 then 3, and ticked from the game's copper interrupt via entry 1.
  - It **only writes a Paula register when its value changes**. No other build does that.
- **Title music** is `masterblazer intro` (`TFMX-SONG `, packed, tempo 142). It's played by a different 22-entry build at $ADF2, set up with 5, 21, 3.
  - It **ticks from its own CIA-B interrupt** (the level-6 vector points into the replayer).
  - It **only plays a BPM-form tempo** (≥ 16; 125 → timer $37F3). Given a speed value, its timer and play flag stay 0 and it's silent. With tempo 125, rows came two ticks apart. That needs checking: the fingerprint's timing-related columns for this build are provisional.
  - DMA on/off is written per voice, not batched.
  - It writes CIA-B timer B ($3140) once per voice every tick.
- **24-bit addresses.** Both builds address the song with junk ($FF) in the top byte, e.g. $FFC706D8. That works on a 68000, which has 24 address lines. The harness now maps RAM and chip registers at $FFxxxxxx as well.
- **A subsong with start = end = 0** is treated as unused by the title build. The test songs now use two identical lines.

**Fingerprint rows:**

| Build | order | note@ | vol | b1 | b1 1st | 04 | 08 | 09 | 17 | 1F | 13 | 05 n=2 / 0 | F1 n=2 / 0 | EFFE1 n=2 | key-up |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Masterblazer loader | P>M | 1 | 1 | same | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | −1 |
| Masterblazer title (provisional) | P>M | 1 | 1 | same | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 1 |

**So the builds don't form a simple line.** Masterblazer has the 1.5-style order and volume latency, and ignores b1's cut deferral. But it has Pro's "first note command after 00 b1≠0 doesn't end the tick", Pro's 1F and Pro's EFFE0001 count.

This supports the view that each game got its own tweaked replayer. For a loader:
- most behaviours are shared by all builds (Wait, 08/09, 13, 05/F1 counts);
- the differences are a handful of independent switches, not one 1.5/Pro switch;
- songs need per-game (or per-format-feature) defaults for each switch.

### 15.6 More builds: Masterblazer (Universal single-disk version) and X-Out
From savestates of the Universal *Masterblazer* (loading, level select, level 1) and the *X-Out* intro.
- **Universal loading:** the same loader build as the other disk (identical table layout).
- **Universal level 1** plays `masterblazer race` (the in-game music), also on the **loader build**, which the game calls directly from many places. So that row covers the race music.
- **Universal level select** plays `masterblazer title` on a **third Masterblazer build** ($1A2FA, 22 entries, 5.9 KB, its own layout), set up with 5, 10 ($40), 3, and ticked by the main loop once a frame.
- **X-Out title (1990)** is `x-out (title)`, unpacked with an "(Empty)" text block, identical to modland's. It's played by a **20-entry build** ($21C20, 4.7 KB, a different layout from Rock'n'Roll's), set up with 5 then 3, and ticked by entry 1 from the copper interrupt. Like Rock'n'Roll it has **no "note + wait" form**, and none of the 9 X-Out songs uses one.

All nine builds:

| Build | order | note@ | vol | b1 | b1 1st | 04 | 08 | 09 | 17 | 1F | 13 | 05 n=2 / 0 | F1 n=2 / 0 | EFFE1 n=2 | key-up |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Turrican 2, 7V | M>P | 1 | 0 | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| Apidya, 7V | M>P | 1 | 0 | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| Apidya, 4-voice | M>P | 1 | 0 | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| Masterblazer level select | P>M | 2 | 1 | same | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | −1 |
| Masterblazer loader / race | P>M | 1 | 1 | same | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | −1 |
| Masterblazer title (provisional) | P>M | 1 | 1 | same | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 1 |
| Turrican 1 | P>M | 2 | 1 | same | end | end | end | end | cont | cont | cont | 3 / inf | 3 / inf | inf | −1 |
| X-Out | P>M | 2 | 1 | same | end | end | end | end | cont | end | cont | 3 / inf | 3 / inf | inf | −1 |
| Rock'n'Roll | P>M | 1 | 1 | same | end | end | end | end | end | end | cont | 3 / inf | 3 / inf | inf | 0 |

(note@ 1 vs 2 and the key-up offsets come from whether the build starts the song during the start call or on the first tick. They aren't musical.)

**Reading the table.** Wait, 08/09, 13 and the 05/F1 counts agree everywhere. The rest groups into behaviour switches:

1. **Order and volume latency:** Pro builds run macros first and write VOL in the same tick. Every pre-Pro build runs tracks first, with VOL one tick late.
2. **DMAoff b1 cut deferral** (Pro only).
3. **No end-of-tick on the first note command after 00 b1≠0:** Pro and Masterblazer. Not Turrican 1, X-Out or Rock'n'Roll.
4. **EFFE0001 count honoured:** Pro and Masterblazer. Ignored by Turrican 1, X-Out and Rock'n'Roll; their songs only use FF00, "forever", anyway.
5. **1F ends the tick:** all builds except Turrican 1.
6. **17 ends the tick:** Rock'n'Roll only.
7. **"Note + wait" ($80–$BF) exists:** all builds except Rock'n'Roll and X-Out.

**Chronologically:** Rock'n'Roll (1989), X-Out (1990), Turrican 1 (1990), Masterblazer (autumn 1990), then Pro (Turrican 2 from early 1991, Apidya). Switches 3–4 arrive with Masterblazer, and switches 1–2 with Pro. Turrican 1's 1F is the only behaviour against the trend, so it's a per-game tweak.

**Picking defaults from the file** (for a loader):
- 7V or packed: Pro.
- No note + wait anywhere: the Rock'n'Roll / X-Out style. Only 17 tells those two apart, and that needs a per-game exception.
- Otherwise unpacked: 1.5 / Masterblazer style.
- Per-game overrides for the rest.

### 15.7 Other releases of the same games
From savestates of the TRSI release of *Turrican* and the Kixx release of *X-Out* (title or intro, loading and level 1 of each).
- **X-Out (Kixx release):** intro, loading and level 1 (`x-out (level 1-2)`) all use the same build as the other X-Out disk (identical table layout), just loaded at different addresses. Its loading screen plays modland's `alcatraz1-2`, probably from the compilation's loader, on that same build.
- **Turrican (TRSI release):** the title uses the same build as before ($1BB20). The game proper (loader `turrican loader`, level 1 `turrican outside`) uses a **second build** ($1A2A0, 22 entries, 5.1 KB, its own layout). It's set up with 5, 2, 3 and ticked by entry 1 from the copper interrupt (profile `t1-game`).
  - It fingerprints **identically** to the title build, including 1F not ending the tick. So 1F is a consistent Turrican 1 trait, not one build's accident.
- **Conclusion:** a game can ship several builds (Turrican 1, Turrican 2, Apidya, Masterblazer), but different releases and cracks of a game carry the same builds, and builds within one game behave the same. So per-game defaults are well founded.

### 15.8 Effects maths and fades
Measured with `effects.py <test> [profiles]` on Apidya (Pro), Masterblazer loader, Turrican 1, X-Out and Rock'n'Roll. The base note is $18 (period 428); voice volume is 48 unless stated.

**Vibrato (0C speed, depth)**
- The offset starts at 0 and moves by *depth* each tick, reversing after speed/2 ticks (integer division) and then every *speed* ticks. So it's a triangle, and lopsided for odd speeds: speed 5, depth −4 gives −8 … +12.
- **Pro and Masterblazer:** period = base + ⌊base × offset / 2048⌋ (rounded toward minus infinity). This matches every measured value, e.g. depth 16, speed 6 gives 417 / 421 / 424 / 428 / 431 / 434 / 438.
- **Turrican 1, X-Out and Rock'n'Roll:** period = base + offset, unscaled, so the same numbers give a much wider vibrato.
- **Rock'n'Roll and X-Out mask the speed to even** (5 acts as 4). The other builds don't.
- **Timing:** the first non-zero offset comes two ticks after the sound tick.

**Portamento (porta notes $C0–$EF: b1 = speed, b3 = rate)**
- **Pro and Masterblazer:** exponential. Every *speed* ticks, period ← ⌊period × (256 ∓ rate) / 256⌋ (truncated, with no −128 rounding), then snap to the target once it's reached or passed. E.g. 428 → 414 → 401 → 388; downward 428 → 441 → 454.
- **Turrican 1, X-Out and Rock'n'Roll:** linear, period ∓= rate every *speed* ticks, then snap.
- **Macro 0B on its own** (no porta note) does nothing: the destination is the current period.
- **Order effect:** in the tracks-first builds, a porta note on the row straight after a note **is cancelled**, because it's read in the same tick as that note's macro runs 00 (DMAoff + reset effects). In Pro it survives. Real songs put porta notes later.

**Envelope (0F step, time, target):** the same in every build. Every *time* + 1 ticks the volume moves by *step* toward the target and snaps to it.
- **Time 0:** no change at all in 300 ticks (Pro and Turrican 1), so effectively off, unless the counter is 16 bits. Used 27 times in 16 files.

**AddVol (0D):** volume = velocity × 3 + b3, **uncapped** in every build (93 is written). Paula clamps values above 64 in hardware.

**AddBegin (11 count, delta)**
- **Count 0:** a single add to the sample start, in every build.
- **Count > 0:** a sweep (delta every tick, reversing every *count* ticks) **only in the Pro builds**. Masterblazer, Turrican 1, X-Out and Rock'n'Roll just add once.

**Master fade**
- **EFFE0004 (trackstep)** works in Pro, Masterblazer and Turrican 1, and **is ignored by X-Out and Rock'n'Roll**.
- **FA (pattern)** works in Masterblazer, Turrican 1, X-Out and Rock'n'Roll, and **is ignored by the Pro (Apidya 4-voice) build**.
- **Rate, where it works:** the master volume moves **one step per voice per tick, i.e. 4 steps per tick at speed 1** ([TP] was right). Speed n means each voice steps every n ticks (speed 4 gives 1 step per tick). Speed 0 jumps straight to the target.
- **Output** VOL = ⌊voice volume × master / 64⌋.
- 7V builds may step 7 or 8 times per tick (not measured).

**More behaviour switches** (added to §15.6's list):

8. Vibrato and portamento maths: **scaled/exponential** (Pro, Masterblazer) vs **linear** (Turrican 1, X-Out, Rock'n'Roll).
9. AddBegin sweep: Pro only.
10. Trackstep fade EFFE0004 vs pattern fade FA support, as above.
11. Vibrato speed masked to even: X-Out and Rock'n'Roll.

### 15.9 Eight more games (second set of savestates)
Savestates for *Turrican 2* (loading, level 1), *GemX* (title, game), *B.C. Kid* (title, loading, game), *Blade of Destiny* (intro), *Z-Out* (title, game), *Monkey Island* (intro, two game scenes, loading), *Fly Harder* (intro, game) and *Abandoned Places* (intro, game). `survey.py` now also scans slow RAM, where four of these games keep their replayer. `locate.py` finds each song's smpl and the replayer variables that point at it.

| Profile | Game, build | Song in the savestate |
|---|---|---|
| `t2-small` | Turrican 2, the small 23-entry build ($1F010, 5.8 KB): loading screens and levels | `turrican 2 level 1-desert` |
| `gemx` | GemX (Kaiko, 1991), 23 entries; the title and the game use the same layout | `gemxgame` |
| `bckid` | B.C. Kid (Factor 5, Rudolf Stember), 4-voice, 25 entries. The title also has a 7V build (33.7 KB) | `bckid-ongame4` |
| `blade` | Blade of Destiny intro (Stember), 25 entries, in slow RAM, its own layout | `blade of destiny - intro` |
| `zout-title`, `zout-game` | Z-Out (Stember): two 22-entry builds (5.4 KB title, 4.5 KB game) | `z-out`, and an unlisted game song |
| `monkey` | Monkey Island, 23 entries, slow RAM; one 46 KB mdat holds every song | `monkey island` |
| `flyharder` | Fly Harder (Tim Follin, 1993), 20 entries, slow RAM; smpl at chip $A4 | `fly harder-ingame` |
| `abandoned` | Abandoned Places (Lajos Suvak, 1992), 20 entries, slow RAM | `abandonedplaces-l3` |

All of them play their own song with the usual 5 (init), 3 (start), 1 (tick) sequence. Fingerprints (columns as in §15.6):

| Build | order | note@ | vol | b1 | b1 1st | 04 | 08 | 09 | 17 | 1F | 13 | 05 | F1 | EFFE1 n=2 | key-up |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Turrican 2 small | M>P | 1 | **1** | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| GemX | M>P | 1 | **1** | **same** | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| Monkey Island | M>P | 1 | **1** | **same** | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| B.C. Kid, Blade of Destiny | M>P | 1 | 0 | +1 | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | 0 |
| Z-Out title | P>M | 2 | 1 | same | cont | end | end | end | cont | end | cont | 3 / inf | 3 / inf | 3 | −1 |
| Z-Out game | P>M | 2 | 1 | same | **end** | end | end | end | cont | end | cont | 3 / inf | 3 / inf | (crash) | −1 |
| Fly Harder, Abandoned Places | P>M | 2 | 1 | same | end | end | end | end | cont | end | cont | 3 / inf | 3 / inf | inf | −1 |

Effects (`effects.py`, as in §15.8):

| Build | vibrato / porta | vibrato speed even-mask | AddBegin sweep | EFFE0004 | FA |
|---|---|---|---|---|---|
| Turrican 2 small, GemX | scaled | no | yes | yes | ignored |
| Monkey Island | scaled | no | **no** | yes | yes |
| B.C. Kid | scaled | no | yes | yes | **yes** |
| Blade of Destiny | scaled | no | yes | yes | ignored |
| Z-Out title | scaled | no | no | yes | yes |
| Z-Out game | scaled | no | no | yes | ignored |
| Fly Harder, Abandoned Places | **linear** | **yes** | no | **ignored** | yes |

**What this adds to the switch list (§15.6, §15.8):**
- **Order and volume latency aren't one switch.** Turrican 2's small build, GemX and Monkey Island run macros first but write VOL a tick late. So switch 1 splits into 1a (order) and 1b (volume latency).
- **The b1 deferral (switch 2)** is in the Turrican 2 builds, Apidya and the Stember builds, but not in GemX or Monkey Island, which are otherwise Pro-like.
- **Fly Harder and Abandoned Places** behave exactly like X-Out on every probe (no note + wait, linear effects, even-masked vibrato speed, EFFE0004 ignored, FA works), although all three have different code layouts. So the "X-Out style" is a whole family, used by other composers into 1993, not one game's build. The "no note + wait anywhere" test in §15.6 picks these songs out.
- **Z-Out's game build** mixes switches: scaled effects (Pro), no first-note rule after b1 (like X-Out), and EFFE0004 but not FA.
- **B.C. Kid** is the only build seen so far with *both* fades.
- AddBegin sweep (switch 9): in the Turrican 2, Apidya, GemX and Stember 4-voice builds, but not Monkey Island or Z-Out. The Stember builds take the first sweep step in the same tick as the 11 command, one tick earlier than the others; the sweep is otherwise the same.

### 15.10 The SID commands (22–29)
Measured with `sid.py` on the Turrican 2 small build and GemX, which play `turrican 2 loading`, `turrican 2 level 2-cave` and `gemxtitle`. SID is a **per-tick wavetable oscillator with a slew filter**: every tick it rebuilds a short buffer from a window of smpl, and Paula loops that buffer.

**Commands**
| Cmd | Parameters | Meaning |
|---|---|---|
| 29 | `b1` = 1 on, 0 off | 29 01 starts SID for this voice; the next DMAon plays its buffer (Paula LC = the buffer, LEN = buffer length / 2). 29 00 stops the buffer updates from the next tick. Songs follow it with One-shot (19). `b3` = 1 (*factor5-fx*) made no measured difference |
| 22 | `b1b2b3` | Source: 24-bit offset into smpl |
| 23 | `b1` = buffer length in bytes, `b2b3` = source mask | Output length (e.g. $20 = 32 bytes, LEN 16 words); the source index wraps with `& mask` (length − 1, e.g. $0F for 16 bytes) |
| 24 | `b2b3` | Read step, 8.8 fixed point ($0100 = one source byte per output byte). `b1` is ignored |
| 25 | `b1` = count, `b2b3` = signed delta | Per-tick change of the step, in 1/65536 units of a source byte ($1000 = +1/16 per tick). The direction reverses every `b1` ticks (a triangle, like AddBegin); `b1` = 0 never reverses |
| 26 | `b1b2b3` | Acceleration: the step grows by this much (1/65536 units) on every output byte, which bends the waveform. Values are taken modulo the source size in effect, so e.g. $0FFFF0 on a 16-byte source acts as a tiny negative acceleration |
| 27 | `b1` = count, `b2b3` = signed delta | Per-tick change of the acceleration, a triangle as for 25 |
| 28 | `b1` = count, `b2` = signed delta, `b3` = limit | Slew filter: the limit is a signed byte. `b2` changes it every tick in 1/16 units ($10 = +1 per tick), reversing every `b1` ticks |

**Buffer generation, each tick** (reproduces every measured byte):
```
step  = 24's step (as 16.16: b2b3 << 8) + 25's swept offset
accel = 26's value + 27's swept offset
phase = 0
for each output byte i in 0 .. length-1:
    step  += accel
    phase += step
    x = smpl[source + ((phase >> 16) & mask)]     # signed byte
    y = slew(y, x, limit)                         # only if a 28 has been given
    buffer[i] = y
```
- Phase, step and acceleration restart every tick from the current (swept) values. So the buffer is the same every tick unless something sweeps.
- `slew(y, x, L)`: if x > y, n = y + L; if x < y, n = y − L, with L as a signed byte. If n passes x, or leaves the signed byte range −128…127, y = x; otherwise y = n. With positive L that's a plain slew-rate limit ($10 turns a square into a triangle). Negative L gives odd but deterministic shapes, and real songs use them ($90, *Denny*, *Gem'Z*). It fits all 12 measured limits (`fitfilter.py`).
- The filter's y also restarts every tick, from a stored value: 0 in GemX. In the Turrican 2 game it was 52, left over from the game's own songs. A loader should use 0.
- **Buffer location:** smpl + 4 + voice × $100 (voice 0 at +4, voice 1 at +$104, …), whatever SetBegin says. So the first $404 bytes of an SID song's smpl are reserved buffer space. That's why every SID source offset is $404 or more.
- **Timing:** the Turrican 2 small build writes the first buffer in the DMAon tick. GemX writes it one tick earlier, and its sweeps run one tick ahead. The arithmetic is the same.
- **Macro 30** (*Gem'Z*, *Denny*, always just after an SID setup) breaks the GemX build, which doesn't know it. It needs a later build (Gem'Z or Denny).

### 15.11 The less-used commands
Measured with `cmds.py` on all 19 profiles. "Pro-like" means macros-first builds (Turrican 2, Apidya, GemX, Monkey Island, Stember 4-voice); "older" means tracks-first builds.

| Command | Result |
|---|---|
| **1C split key** | Jump to step `b2b3` if the note is **greater than** `b1`, otherwise continue ([TP]'s reading). Same in Pro-like builds, Masterblazer, Z-Out title and Turrican 1. B.C. Kid and Z-Out game always jump. X-Out, Rock'n'Roll, Blade of Destiny, Fly Harder and Abandoned Places never jump |
| **1D split volume** | The same comparison against the voice's volume, with the same per-build results |
| **10 loop until key up** | As 05, but falls through at key-up. With count 0 it loops until key-up; with count n it also stops after n + 1 passes. The same in every build |
| **12 AddLen** | Length += signed `b2b3` words (32 + 16 = 48, 32 − 16 = 16). The same in every build |
| **15/16 Gosub/Return** | Work as described, one level, in every build |
| **21 Play macro** | Starts macro `b1` on voice `b2` with the current note (voice 1 played period 340 for note $1C). **Only in Apidya and the Stember builds** (B.C. Kid, Blade of Destiny); ignored elsewhere, including Turrican 2 |
| **20 Signal** | Writes `b2b3` into the replayer's variables (found in Apidya 7V, Blade, Monkey Island, Masterblazer, Z-Out). No audio effect |
| **FD cue** | No audio effect, and the word wasn't stored anywhere in RAM in any build. Safe to ignore |
| **FC lock** | In the Pro-like builds, `b1` ≠ 0 locks voice `b2` against new notes for `b3` + 2 ticks, counting the FC row; `b1` = 0 doesn't lock. Not measured in the older builds, because the probe's notes come too close together for them |
| **1A wait on DMA** | 1A n resumes after n + 1 audio interrupts. The first comes right at DMA start, so 1A 0 waits one tick and 1A 1 one full sample loop. Measured in Apidya, whose replayer installs its own level 4 handler. In the other builds the harness can't show it yet (they resume after one tick); real games may route the interrupt through their own handler. **Refined in §15.15:** 01 clears the voice's interrupt, and a request still pending when the 1A runs counts as the first |
| **FB play pattern** | Starts pattern `b1` on track `b2` straight away (the same or the next tick) in Turrican 2 (both builds), B.C. Kid, Monkey Island, Masterblazer, Z-Out title and Turrican 1. The transpose in `b3` didn't show (period 428, not 321), so its meaning is still open. *Denny* uses `b3` = 0, 5, 2, 7, which looks like a transpose |
| **1B random** | Inconsistent: crashes several builds, and gives odd periods in others. It's only used by *Masterblazer race*; leave it for that build |

**Still open:**
- FB's `b3`; 1B; 1A in the non-Apidya builds; FC in the older builds.
- Macro 30/31 (needs a Gem'Z, Denny or Turrican 3 build).
- The Masterblazer title build's BPM-mode row timing, and EFFE0002 tempo changes (Monkey Island).
- The 7V fade rate.
- 14's timeout, 18/19 latching details, 1E.

### 15.12 Whole songs against the original replayers (2026-11-02)
TFMXreplay was written from these notes. Its `tfmxtrace` tool prints every Paula write per tick. `tfmx-trace/reference.py` plays any song through a game's own replayer and prints the same, and `tests/compare.py` compares them tick by tick: per voice, the DMA on/off transitions and the final LC, LEN, PER and VOL.
- **Apidya 4-voice build:** all seven 4-voice Apidya songs are identical for 1,500 ticks (30 s), every register of every tick.
- **Turrican 1 (1.5) build:** most ticks match. The remaining differences are envelope timing and ±1 volume steps in some songs.

These rules came out of the comparisons:
1. **Macro note detune is a word.** 08, 09 and 1F use the signed word `b2b3`, not just `b3`. Pro: period = table × (256 + note finetune + `b2b3`) / 256. Older builds: period = table + note finetune + `b2b3`.
2. **The period table** is the same in Pro, 1.5 and X-Out builds: 1710 1614 1524 1438 1357 1281 1209 1141 1077 1017 960 908, then 856 810 764 720 680 642 606 571 539 509 480 454, 428 404 381 360 340 320 303 286 270 254 240 227, 214 202 191 180 170 160 151 143 135 127 120 113, then that top octave again for 0x30–0x3F (`periods.py`).
3. **After a deferred DMA off** (00/13 with `b1` ≠ 0 in Pro), the first **04 Wait** doesn't end the tick either, like the first note command. [TP]'s list (04/08/09/1A/1F) is right.
4. **Portamento moves the note's base period,** not the vibrato-shifted one Paula plays. While a portamento runs, 08/09/1F/17 only set its destination.
5. **The volume is a byte:** 0D gives (velocity × 3 + `b3`) & 0xFF. Uncapped below 256, but 8 × 3 + 0xE8 is 0 (*Turrican bonus*).
6. **14 with the key already up** continues at once, in the same tick.
7. **F7's time** is (`b2` >> 4) + 1 ticks per step. Time nibble 0 steps every 2 ticks, unlike macro 0F with time 0, which never steps.
8. **A fade with speed 0** (EFFE0004 or FA) sets the master volume at once, in that command.
9. **The fade steps once per voice per tick** whether or not that voice's effects are running.
10. **After an F0, every track is processed again in the same tick,** including tracks that keep their pattern (8000) and were already processed. Their waits count down twice.
11. **Starting a song** writes VOL 0 and DMA off to all four voices.
12. **Older builds (1.5):** the voice's VOL *and* PER are written before its macro runs, so changes made by effects reach Paula a tick late. Note commands write PER at once. There's no batched period write at the end of the tick, and DMAon with `b1` = 0 doesn't hold the effects back (in Pro it holds them for a tick).
13. **00 with a non-zero `b3` sets the volume** (GemX title, matched against GemX's replayer): `b2` = 0 gives velocity × 3 + `b3`, otherwise `b3`. This confirms the editor's "flag / addset / vol" naming (§5.2). About 180 macros in the collection use it.
14. **GemX's build** (and Monkey Island's, by fingerprint) runs macros first like Pro but doesn't defer the 00 `b1` cut, so a Pro-detected song from these games starts notes a tick late. A per-game variant table is needed.

**Song ends**, for durations:
- EFFE0000 ends a song.
- Wrapping past the subsong's end, or a backward EFFE0001 forever jump, is a loop.
- Some songs loop forever inside a pattern (F1 with count 0) and never reach F0. The sequencer depends only on the trackstep and track state, so an exact repeat of that state marks the loop.
- Jingles stop every track with F4; TFMXreplay ends them 2 seconds later.
- Over the 368 local songs, all but 4 subsongs get a length this way. One file (*hanse-ascon*, 540 bytes) has no playable subsong.

### 15.13 More variants compared (2026-11-02, later)
Results against the original replayers (1,500 ticks each, `tests/check_song.py`):
- **Turrican 1 (both builds):** all 14 songs/subsongs tried are exact, including the BPM subsongs of *turrican intro* (run through `t1-cia`).
- **X-Out family:** X-Out (title, level 1–2, 3–4, 5+7, 6, 8, end tune, hiscore), *alcatraz1-2*, *Fly Harder* and Abandoned Places (l1, l3, city1, temple, part1.1, encounter, effect) are all exact. X-Out's loader song crashes the X-Out build in the harness, so it's not compared.
- **Rock'n'Roll:** all 10 songs are exact.
- **Masterblazer:** Z-Out title is exact on the Masterblazer variant. *masterblazer title* (level-select build) is exact apart from volume: init leaves the master volume as the game had it, so it was compared without VOL. Loader, race, museum, lucasarts and intro still differ, mostly in volume. Not investigated yet.

New rules:
1. **1A, older builds:** a 1A enables the voice's interrupt and reads INTREQR. If the bit is already pending (set at DMA start or at a block end that nothing consumed), it clears it and passes at once, ignoring the count. Otherwise it waits for interrupts from that point. Either way the tick ends. In Pro (Apidya) the count runs from the command.
2. **Older builds, voice update order:** effects, then the VOL and PER writes, then the macro (1.5, X-Out). Rock'n'Roll: VOL write, effects, PER write, then the macro.
3. **Older builds:** in the tick a note arrives (tracks run first), the voice isn't updated at all (1.5, X-Out).
   - **Rock'n'Roll:** the old macro runs once more, and notes, porta notes and trackstep FE stops take effect at the voice's next update.
   - In that update the new macro runs and the effects run, but neither VOL nor PER is written. A macro that starts with a Wait therefore writes nothing at all in that tick, which looked like a skipped update.
   - **Rock'n'Roll's 0B** starts its slide from the last note's period (the previous slide's destination), even when that slide hadn't got there (*take off song*).
4. **1.5: 0C** restarts from the note's base period and writes it at once.
5. **Vibrato keeps running during a portamento** (all builds); it just doesn't write the period.
6. **A porta note becomes the voice's note,** so a later 08 builds on it (X-Out level 1–2).
7. **F7's high nibble isn't a time:** F7 always steps every 2 ticks (nibbles 0 and 8 both).
8. **Tempo ≥ 16 differs by build:**
   - Turrican 1 (from 39) and Monkey Island (from 20) tick from their own CIA-B interrupt. Turrican 1 sets only the timer's high byte, 7093 / BPM. The game's frame call to entry 1 only sets the flag that lets the CIA handler play.
   - The X-Out family has no BPM mode: speed = tempo − 16, ticked at 50 Hz.
   - Frame-ticked Pro builds treat the tempo as plain speed. A Pro BPM timer is 1,790,456 / BPM (Masterblazer title: 125 gives $37F3) on the PAL CIA clock (709,379 Hz).
9. **Harness fixes:**
   - Paula's period changes now stretch the block in progress, as on real hardware.
   - The minimum period is 113, as in the library.
   - CIA ticks are spaced by the timer the replayer sets, and get the replayer's audio interrupts.
   - **The audio filter (CIA-A port bit 1, the power LED):** every replayer build in the savestates sets the bit (in GemX, inside its audio reset routine), which turns the LED filter off, and none has code that clears it. The format has no command for it (§3.2), so a TFMX song can't turn the filter on.

**Still open:** GemX, Monkey Island, the Turrican 2 small build, the Stember builds, 7V (Apidya title and end, Turrican 2 title) and SID compared song by song; the Masterblazer volume differences. Songs whose variant can't be told from their contents are in a table of mdat checksums (`src/variant.cpp`), each checked on its own game's replayer: all of Turrican 2, the Turrican 3 loader, Hexuma's level songs, Masterblazer's intro, Z-Out's songs (they look like 1.5 but play on Masterblazer's rules), Rock'n'Roll's intro and Turrican's bonus song.

### 15.14 Masterblazer, GemX and Monkey Island compared (2026-11-02, evening)
Results (1,500 ticks each, `tests/regress.py` reruns every cached comparison):
- **Masterblazer:** loader, intro, title and *Z-Out* are exact. *museum* and *lucasarts* differ only in a first VOL write, made from voice volumes the game's own song left in the replayer (savestate artefacts, like the title's master volume). *race* is exact on voices 0–1; voices 2–3 use 1B (below).
- **GemX** (new variant `TFMX_VARIANT_GEMX`): *gemxtitle* (3 subsongs) and *gemxgame* (5) are exact.
- **Monkey Island** (the GemX variant): the frame-ticked subsongs (16–19, 30) and BPM subsongs 0, 2, 5–15 and 29 are exact apart from leftover-volume artefacts. In 1, 3 and 4 a 1A wait ends one tick early now and then: the 4 KB first block ends within a fraction of a tick of a tick boundary, and the original starts DMA a little way into the tick (its own run time), which this model doesn't have.
- The BPM subsongs need the `monkey-cia` profile (ticks from the replayer's own CIA-B timer, 1,790,456 / BPM). The `monkey` profile calls entry 1 once a frame, which spaces BPM ticks 20 ms apart, so its references were wrong for anything timed by Paula (1A).

New rules (probes: `tfmx-trace/loops.py`, `fades.py`, `f7.py`, `rnd.py`):
1. **05, 10 and 14 share one counter per voice** (every build tested: Masterblazer, Apidya, Turrican 1). With L = −1 when idle:
   - 05/10 n: if L = 0, fall through and set L = −1; otherwise L −= 1, and if that makes it negative, L = n − 1; then jump. 10 with the key up falls through and leaves L alone.
   - 14 T with the key down: if L = 0, continue and set L = −1; otherwise L −= 1, and if negative, L = T − 1; wait (the command runs again next tick). With the key up, it continues and leaves L alone.
   - So a 10/05 loop whose body contains a 14 with a timeout never runs out, and a "forever" 05 reached with L = 0 falls through. *masterblazer loader* relies on both.
2. **00 with b2b3 ≠ 0** differs three ways:
   - GemX and Monkey Island set the voice volume (§15.12 rule 13) and write it to Paula at once, even though other volume changes reach Paula a tick late.
   - Masterblazer (all three builds) and Z-Out title write b2b3 straight to Paula's VOL and leave the voice's volume alone.
   - Turrican 1 and 2, Apidya, X-Out, Rock'n'Roll, B.C. Kid, Z-Out game and Fly Harder ignore it.
   - Only GemX, Monkey Island, *apidya (title)* (7V) and the ST *turrican 2 title* use it in more than 5 macros, so 10 or more uses (without 7V) selects the GemX variant.
3. **Masterblazer loader and level-select builds:** at master volume 64, VOL is only written when the voice's (unscaled) volume changes. So the last step of a fade up to 64 never reaches Paula until the voice's volume next changes, and a 00 VOL write can stick. The title build and Z-Out write every tick.
4. **Paula's VOL register has 7 bits:** 64–127 are full volume, and higher values wrap (0x110 is 16).
5. **F7's time:** one step every (b2 >> 4) + 2 ticks in the Pro builds, GemX/Monkey Island, Masterblazer and Z-Out. Rock'n'Roll, the X-Out family and Turrican 1 ignore the nibble (every 2 ticks).
6. **Pattern commands address voice b2 & 3 in 4-voice songs** (F5, F6, F7, FC and porta notes), as notes do. *Monkey Island* sends key-ups to "voice 4".
7. **GemX and Monkey Island timing:** macros run before tracks, the volume and period are written before the voice's macro (a tick late), and the first update after a note runs only the new macro (no effects, no writes). DMAon doesn't hold the effects back, the 00 b1 cut isn't deferred, and a 1A counts a block start already pending (as the older builds do).
8. **1B (race only), partly measured on the loader build:** voice 2 gets a new table note every 6 ticks (b2 = 06), and voice 3 repeats each one 4 ticks later. With b3 = 0 it cycles a fixed sequence of 10 notes; other parameters mostly held one note in the probes. Not implemented.

**7V, first comparisons** (hardware voices 0–2 only: voice 3 carries the mix of voices 4–7, which Paula writes can't show):
- *turrican 2 level 0-intro* (`t2`): exact for 1,310 ticks; then 12 ticks differ by one AddBegin sweep step (rule 9).
- *apidya (title)* (`apidya7v`): exact for 987 ticks.
- *apidya (end)*: exact for 770 ticks. The divergence comes right after trackstep lines `EFFE0002 0002` and `EFFE0003 FF00 0010` (a mid-song change of the 7V mix setting): the original starts voice 1's next note 2 ticks later than TFMXreplay, and voice 0's note after that too. Not investigated yet.

9. **00 with b1 ≠ 0 and the effects reset** (`resetdefer.py`): Apidya (both builds) and B.C. Kid reset the effects at the 00, so a running AddBegin sweep stops at once. Turrican 2's 7V build runs the effects once more in that tick (one more sweep step), and its small build makes that step at the start of the next tick. TFMXreplay follows Apidya; Turrican 2 needs a per-game switch. **Corrected in §15.15:** this probe's 00 was in a new note's first update, which Apidya skips. All the Pro builds stop the sweep only at the cut.

**Still open:** 1B; the 7V divergences above and the mix itself; the Turrican 2 small build, the Stember builds and SID compared song by song.

### 15.15 7V compared (2026-11-02, night)
Results (hardware voices 0–2, 1,500 ticks; `tests/regress.py` passes `--voices 012` for the 7V profiles):
- *apidya (end)* (`apidya7v`, variant Pro): exact.
- *turrican 2 level 0-intro* (`t2`, new variant `TFMX_VARIANT_TURRICAN2`): exact.
- *apidya (title)*: exact apart from 7 ticks where a 1A wait ends a tick earlier in the reference (below).

New rules (probes: `tfmx-trace/noteeffects.py`, `resetdefer.py --all`, `dmawait.py`):
1. **A speed change applies to the next line's first row.** The row counter is reloaded after the rows are read, so `EFFE0002` takes effect before the following line's first row is counted, not one row later. (*apidya (end)* changes speed mid-song; no other compared song did.)
2. **A new note's first update runs no effects** (Apidya 4V and 7V, B.C. Kid, Blade of Destiny, the small Turrican 2 build). It's invisible when the new macro starts with 00 b1 = 0, which resets them anyway. **Turrican 2's 7V build** runs them; that's its only difference found so far, so it's a variant of its own (not detected automatically: nothing in the song marks it).
3. **00 with b1 ≠ 0 resets vibrato, portamento and envelope at once, but the AddBegin sweep only stops with the deferred cut**, so it steps once more in the 00 tick (every Pro build). This replaces §15.14 rule 9.
4. **1A in Pro:** 01 writes INTENA and INTREQ for the voice, which disables it and clears any pending request, even when DMA is already on; DMA then starts at the end of the macro phase and raises a new request. When 1A enables the interrupt, a pending request is served at once and counts as the first of the n + 1. So DMAon, Wait, 1A 0 continues on the next tick, but DMAon, Wait, DMAon (already playing), Wait, 1A 0 waits for the block end (*apidya (level 1)*).
5. **The 7V tick length** is one mix block on voice 3 (§8): round((100 + x) × 1.8) words at period 199, 4 CPU cycles per word per period unit.
6. **The small Turrican 2 build runs the effects before the macro**: with 00 b1 = 0 its sweep steps once more, before the DMA off. (§15.16: it's GemX's generation.)
7. **Turrican 2's 7V build seems to ignore 1A** (it always continues on the next tick). None of its 7V songs use 1A.

**Harness fixes (7V):**
- 7V ticks now come at voice 3's block starts. Before, the spacing came from voice 3's length at setup (2 words), so ticks were spaced by the handler's run time and every 1A in the 7V references was mistimed.
- Voices 0–2 share the level 4 handler: their pending interrupts (from 1A) are delivered just before the tick. Before, a pending one made the handler return early, and the reference skipped that tick entirely.
- `TFMX_CPU_RATE` (cycles per instruction byte, default 4) runs the replayer faster. In the harness, the 7V handler's mixing takes about 135,000 of a tick's 154,000 cycles, so the replayer's DMA starts land about 95% of the way through the tick, and a handler that overruns the tick loses the next one (it clears voice 3's request when it returns). How close that is to a real A500 is unknown. The 7 title ticks are this: at the default rate the reference loses a tick between DMA start and the block end; at rate 0.2 a different 1A wait ends a tick later instead (its block ends 0.01 tick after a tick boundary, and the original starts DMA after the mix). TFMXreplay starts DMA at the tick boundary.

**Still open:** the other 7V builds (B.C. Kid title, Blade of Destiny, Turrican 3, Denny and others: 16 songs use 7V); the mix itself (rate, resolution, clipping); the small Turrican 2 build; 1B; SID.

### 15.16 Turrican 2's small build compared (2026-11-02, night)
The `t2-small` profile (a savestate of *Turrican 2*'s level 1) plays any song through the game's in-level replayer. Songs bigger than its 13,024-byte buffer (*cave*, *flight*, *factory*) need placing elsewhere first.
- **The build is GemX's generation** (macros first, volume written before the macro, a note's first update runs only the macro, DMAon doesn't hold back the effects), **but it defers the 00 b1 cut as Pro does**, and ignores 00's b2b3. `TFMX_VARIANT_TURRICAN2` picks this for 4-voice songs and the 7V build's behaviour (§15.15) for 7V songs.
- Results, 1,500 ticks: *level 1-desert* 0, 1, 3–6 and *level 5-aliens* 1, 2, 4–10 exact. *desert* #2 differs in 4 ticks: a 1F in voice 1's first note plays the "previous note" the game's own music left in the replayer. *aliens* #0 differs from tick 219 (below).

New rules (probes: `oneshot.py`, `vibspeed.py`):
1. **With a deferred cut, an earlier DMAon in the same tick still starts the sample** (DMACON on at the end of the tick, the cut at the start of the next). TFMXreplay used to cancel it.
2. **19 (one-shot) stops an AddBegin sweep** (both Turrican 2 builds, GemX, Apidya).
3. **Vibrato with speed 1 never turns** (every Pro-era build): the counter starts at speed / 2 and turns the direction when a decrement reaches exactly 0. With speed 1 it starts at 0 and goes negative, so the period keeps moving one way. Speeds 2 and 3 turn every 2 and 3 ticks after a half period at the start.
4. **The scaled portamento works in 16 bits and keeps 11 bits:** period' = ((period × ((256 ∓ rate) & $FFFF)) >> 8) & $7FF. A rate above 256 wraps into a jumping 6-step cycle (*aliens* #9 uses $3776: 270 → $48F → $6BC → $53D …) rather than arriving.
5. **Open, *aliens* #0:** a pattern portamento (× 255 / 256 a step) reaching $100 lands on $FF; the original keeps $FF for a tick, and its vibrato then centres two lower than TFMXreplay's. Not clamping to the destination fixes the first tick but not the rest, and breaks *apidya (level 1)* and the *masterblazer intro*, which do clamp.

### 15.17 B.C. Kid's 7V build, and other games' 7V songs (2026-11-02, night)
A savestate of the *B.C. Kid* title holds two replayers: the 4-voice game build at $28BEE and the 33.7 KB 7V build at $3DD6A, which is playing (the level 4 vector points into it). Profile `bckid7v`: test songs go to slow RAM ($C40000; the game's own copy of *bckid-titel* at $2A264 is 80 bytes shorter than modland's, with its pointers 80 lower), the samples stay at $2C058, setup as Apidya's (5 with the mix buffer $5D200, then 24). The game uses a different mixer setting: voice 3 plays 90 words at period 398, and after the harness's setup 130 words at period 275. Every setting gives the same tick rate (§8). `bckid7v-hi` puts the samples at $60000 (over the title graphics, 128 KB) for bigger sample files.
- *bckid-titel*, all 3 subsongs: exact (Pro).
- Other games' 7V songs through B.C. Kid's build, subsong 0: *anstoss-titel*, *blade of destiny - intro*, *turrican 3 intro*, *hexuma-title*, *denny-title*, *bundesliga manager-title* and *carl lewis challenge* exact. *mlawtitle* matches but starts only 6 samples on voices 0–2 in 1,500 ticks, so it shows little. These aren't their own games' builds, but they test the 7V rules on a second build with seven more songs.

New rule (`fades.py --7v`):
0. (See §15.18 for Turrican 3's own 7V build.)
1. **In 7V the master fade steps for the seven real voices** (0–2 and 4–7), not for hardware voice 3: 7 steps per tick at speed 1, so a fade at speed 4 climbs 7 every 4 ticks (Apidya's and B.C. Kid's 7V builds alike), however many voices play. This settles the "7V fade rate" left open in §15.11.

### 15.18 Turrican 3 (2026-11-02, night)
Savestates of the start-up sequence, the level 1 loading music, a level being played, and the menu's music player (one per track; its "Theme" and "Hall of Heroes" play the intro song).
- **The 7V build** (33.6 KB at $368AA) plays the intro (7V, mixer setting 14: voice 3 plays 143 words at period 256) and, in the menu player, every level song in 4-voice mode, ticked from the game's frame interrupt (entry 1). The intro song sits at $3EBE2 with its samples at $44562; the menu loads the chosen level's song to $40004 with its samples straight after. Profiles `t3` (intro state, from its level 4 interrupt) and `t3-menu` (a menu state; songs in slow RAM at $C10000, samples at $40004).
- **The small build** (5.5 KB at $48E7C) plays the loading music (*turrican 3 loader*, SID macros). Profile `t3-small` (songs in slow RAM at $C50000, samples at $36000 over the 7V build, which nothing runs meanwhile).
- **The in-game build** (a third build, 17 entries, 5 KB at $6ED40, ticked from the game's frame interrupt) plays the levels' music. Profile `t3-game` (songs in slow RAM at $C10000, samples at $70114, where the game keeps the level's song and samples). It's plain Pro: the same results as the 7V build below (29 of 32 level subsongs exact), except *level 1* #4, where it skips voice 2's notes from track 6 for long stretches (ticks 396–706 and from 1036). The menu's build plays them; this is most likely the game keeping a voice free for sound effects, so it's game state rather than song behaviour.

Results (Pro, 1,500 ticks):
- 7V build: *turrican 3 intro* subsongs 0, 2, 3 exact on voices 0–2, and 29 of the 32 level subsongs exact on all four voices. Open:
  - *intro* #1, voice 1 from tick 130: the original leaves a 14 (wait for key-up, timeout 16) about 12 ticks early and moves on to the macro's second envelope. The voice's previous note ran the same macro and was cut short by this note partway through the same wait, so the shared 05/10/14 counter looks like it carries over, but not resetting it at a note breaks dozens of other songs.
  - *level 1* #3 (voice 1 from tick 355: periods above $7FF, $A01 against $A0C) and *level 2* #7 (an AddBegin step at tick 227).
  - *level 1* #2: voices 1 and 2 differ only in volume until tick 245, starting from tick 0, which looks like the savestate's leftover master volume during a fade-in.
- Small build, `TFMX_VARIANT_TURRICAN3_SMALL`: *level 2*–*level 5* exact. *level 1* differs on voice 3 from its first note there (tick 642: period 170 against 303, and one AddBegin step instead of none further); at tick 0–2 that voice is still sliding from the loading music in the savestate, so this is probably leftover state. *loader* differs on its SID voices (the original also rewrites LEN and LC every tick, and some periods differ), which is SID work for later.

New rules (probes: `cmds.py`, `cont.py`, `addnote.py`):
1. **Turrican 3's small build is a cut-down Pro:** the AddBegin sweep is a single add (as in the older builds), and 06 (continue), 15/16 (gosub/return), 1C/1D (splits) and pattern FC (lock) are ignored; the macro just goes on to its next step. Everything else tested (timing, 1B not crashing, FB, 21, 10, 12) behaves as in Pro. 1A couldn't be measured (the harness raised an error).
2. **0C in Pro puts the period back to the note's base,** so 0C 00 (vibrato off, at key-up in *level 1* #4) drops the vibrato's offset. The older builds already did this and wrote it at once; Pro writes it with the tick's period write.
3. **The scaled portamento checks for arrival on the whole product,** before keeping 11 bits (§15.16 rule 4). With a rate above 256 the kept value can pass the destination while the whole product hasn't, so the slide doesn't stop there: *level 2* #7 uses rate $6237 from 428 toward 401 and plays 428, 376, then 401 (the step back up lands past 401 and stops). With normal rates nothing wraps, and the slide clamps on arrival as before (*apidya*, *masterblazer intro*).

### 15.19 Hexuma, Mega-lo-Mania, Carl Lewis, Bundesliga Manager, Anstoss (2026-11-03)
Savestates of the *Hexuma*, *Bundesliga Manager*, *Carl Lewis Challenge*, *Anstoss* and *Mega-lo-Mania* titles, and of *Hexuma* and *Mega-lo-Mania* in game. Each title holds a 7V build (25 entries, 33–36 KB; Hexuma's in slow RAM) playing its title song; the in-game states hold small 4-voice builds (25 entries, about 5.5 KB). Every song in memory is byte-identical to modland's. The 7V mix lengths fit §8's rule at mixer settings 14 (Hexuma), 16 (Bundesliga) and 18 (the others). *denny-title* has no game (Denny was never released).

Profiles and results (Pro unless named, 1,500 ticks; 7V on voices 0–2):
- `carllewis7v`: *carl lewis challenge*, both subsongs exact.
- `mlaw7v`: *mlawtitle*, all 3 subsongs exact. (Subsong 0 starts only 6 samples on voices 0–2; it lives on voices 4–7.)
- `mlaw` (Mega-lo-Mania in game, playing Stember's *mlaw1*): *mlaw1*, *mlaw2*, *mlaw3*, all 5 subsongs exact.
- `hexuma` (Hexuma in game, playing *hexuma-house*; new variant `TFMX_VARIANT_HEXUMA`): all six level songs, 7 subsongs, exact.
- Not working yet: `hexuma7v` (the setup runs but no note ever plays: the game's own calls into this build weren't found, so its setup is unknown), `bundesliga7v` and `anstoss7v` (their init goes through the operating system's library calls, which the harness can't run; without init the sample addresses come out wrong). All three title songs already matched exactly through B.C. Kid's 7V build (§15.17).

New rules (probes: `vibspeed.py`, `cont.py`, `oneshot.py`, `cmds.py`, `fa.py`):
1. **Hexuma's in-game build ignores macro 0C** (vibrato): the period never moves. Everything else probed is as in Pro. It writes VOL and PER only when they change, which the comparison doesn't see (it compares the last value written).
2. **Pattern FA (master fade) per build:** it works in Turrican 2's 7V build, B.C. Kid's 4-voice build, Mega-lo-Mania's title build, both Hexuma builds and the older builds; it's ignored by both Apidya builds, B.C. Kid's 7V build, Blade, all three Turrican 3 builds, Carl Lewis, Mega-lo-Mania's in-game build and GemX. Only 13 modland songs use FA, none of them written for a build that ignores it, so TFMXreplay now runs FA in every Pro variant (*mlawtitle* #1 needs it). GemX keeps it off.
4. **WinUAE captures** (`tests/capture.py` converts a console memwatch capture to the tfmxtrace format, keeping the replayer's writes by PC; `compare.py --search N` aligns a capture taken mid-song). *hexuma-title* from the running game (a capture from the start; the replayer was at $C3xxxx and the samples at $20798 that boot): exact for 463 of 464 ticks with `TFMX_VARIANT_HEXUMA` (the last tick was cut off by the end of the capture). So Hexuma's title build has no macro vibrato either. It too writes VOL and PER only when they change. *bundesliga manager-title* (replayer $31000–$3A900, samples at $3BAF0, from about 12 s into the music): aligned at tick 608, 507 of 508 ticks exact with Pro (the other is the capture's first DMA start, whose cut came before the capture). The song's first note on voices 0–2 is at tick 554, so a capture from the start only shows them silent. *anstoss-titel* (replayer $42900–$4B700, samples at $2559E): a capture from the start, 334 of 334 ticks exact; a second from tick 594 (voice 1 only starts at tick 490), 390 of 392 exact, the other two being DMA cuts of notes that started before the capture. Both Pro.
3. **Paula ignores LC bit 0** (DMA is word-aligned).

### 15.20 The Apidya title from the running game (2026-11-03)
Two WinUAE captures of *apidya (title)* (capture 1 from the start, capture 2 from about 15 s in, tick 767 of the song; replayer $5C00–$E000, samples at $343F2). The game boots to this song, and its title animation is part of the same subsong. `tests/regress.py` now reruns every capture as well (`CAPTURES`).
- The game also re-initialises the 7V mixer from its main loop now and then (an AUD3LC write with LEN 2, from another instruction than the tick's buffer swap). `capture.py` only starts a tick at the swap's own PC (the most common one).
- **The real machine never drops a tick** (450 consecutive ticks line up). The harness's lost ticks (§15.15) came from its rough CPU timing, so its 7V references are wrong wherever a 1A wait ends near a tick boundary.
- **DMA starts come late in a 7V tick.** The replayer runs after the mixing, so its DMACON write lands well into the tick, and a sample block started then ends that much later. Capture 2 has four 1A waits that end a tick later than TFMXreplay's old model (DMA at the tick boundary) and the harness's (4 cycles per instruction byte): 137 ticks differed. Modelling the start as delayed by a fraction f of the tick makes the capture exact for every f from 0.2 to at least 0.95; TFMXreplay uses 0.5. Capture 1 is exact either way. A third capture (from tick 1375, during the title animation) is exact with any f from 0.2 to 0.95 too (63 ticks differ without the delay), so the range stays. Only *apidya (title)* uses 1A among the 7V songs, so no other song can narrow it, and audibly the delay only moves a 7V sample start by a few milliseconds.
- A capture that starts mid-song shows a DMA cut as no transition when the voice's note began before the capture (its "playing" state isn't known yet): those first-note differences are edge effects. The harness's own *apidya (title)* reference now differs from TFMXreplay in those places, and the captures are what count there. Hexuma's build passes odd sample starts through (*hexuma-ice*, *-mine*); the comparison now masks the bit.

## 14. The Soundfactory CD [SF]
- **Jukebox** has 236 TFMX songs by Chris Hülsbeck and Rudolf Stember, as DOS-named `.TFX`/`.SAM` pairs. The magic is `TFMX-SONG ` in 230 of them.
  - 169 are byte-identical to modland's.
  - 7 have the same song data with a different header text.
  - 23 share modland's samples but have repacked song data (§2).
  - 37 match nothing by checksum: *Apidya*, *Apprentice*, *Jim Power*, *X-Out*, *Z-Out* and others. The ones I compared are the same songs, packed and renumbered. *Apidya level 1* matches modland's in every pattern and macro.
  - A few may be different versions, for example the Jukebox *Turrican 2* title is 9 KB bigger, with patterns 103+ differing. Worth a closer look later as an alternative source.
- **PMS 0.2** (Sebastian Mies, 1994–95) is the DOS player behind the Jukebox. Its revision notes say "still no real TFMX docs" and it doesn't implement 7V timing, so it isn't a behavioural reference. Its readme's hand-tuned tempo table turned out useful anyway (§3.2).
- Copyright: the CD's contents are commercial ("neither shareware nor freeware"). Use them as reference and test material only.

### 15.21 The 7V mixer (2026-11-04)
The mix buffer can be read straight out of the original replayer's memory, so this comes from the buffers it wrote: one-note test songs through *Turrican 2*'s build, then whole songs, compared with TFMXreplay byte by byte. A WinUAE recording of the *Turrican 2* title (no filter, no interpolation) checked the result as sound.

**Each block,** for voices 4–7, one byte at a time:
- **Level:** `(sample × min(volume, 63)) >> 6`, arithmetic shift, so volume 64 equals 63. No scaling down for the number of voices: the bytes add up and clamp at −128 and 127. A voice at full volume is as loud as a hardware voice, and four loud voices distort.
- **Master volume:** the mixer ignores it. A fade moves hardware voices 0–2 only (all three builds tested).
- **Pitch:** 21.11 fixed point. `step = mix period × 2048 / period`, rounded down, with the position starting at 2047/2048: the first byte is the sample's first or second, and the phase carries across blocks. All 48 notes, at four mixer settings, match for 12 blocks.
- **Loop:** at the block's end the position wraps, keeping its fraction, and the next block is whatever LC and LEN hold. After a wrap, an occasional byte falls one sample off, at most about 0.3% of the bytes of a short loop. The phase carries across the wrap within what can be measured, so the cause is a rounding too small to see.
- **Blocks under 64 bytes** (LEN < 32) are silent.
- **DMA off** doesn't silence the voice: stepping stops and it holds its last sample, a DC level, until the next start.

**Timing.** The handler writes the buffer pointer, mixes into that buffer, and then runs the replayer, whose hardware writes therefore land late in the tick: about 2.4 ms + 0.08 ms for each word of the block after the tick starts, as the mixing is what takes the time (fitted to recordings of eight songs, from 90 to 204 words; the songs then sit within about 0.6 ms). The block plays from the next block start. Period, volume, sample start and length, and DMA off reach the mixer a tick after the same value would reach a hardware voice. A new note is the exception: it's mixed in the tick of its DMA start, with the volume and period as they stood at the 01 command.

**Mixer settings,** read from the games: 18 (period 199) in *Turrican 2*'s title, *Apidya*'s title, *Carl Lewis Challenge*, *Anstoss* and *Mega-lo-Mania*, 17 (211) in *Apidya*'s end sequence, 16 (224) in *Bundesliga Manager*, 14 (256) in *Turrican 3*'s intro animation and *Hexuma*, 9 (398) in *B.C. Kid*'s intro. One game can use more than one, even for one song: *Turrican 3* plays its title music at 14 in the intro animation and at 20 (179) in its jukebox, and *B.C. Kid* at 9 in its intro and 13 (275) in its jukebox. TFMXreplay uses the jukeboxes', the cleaner of the two. The period is about 3580 / setting. The WinUAE recording fits 18, not the 12 seen earlier in that game. Rounding moves the tick slightly between settings, so the setting has a small effect on tempo. `tfmx_settings.mix_setting` overrides it.

**Replayer findings.** 01 with b1 = 0 holds effects back only on hardware voices: voices 4–7 aren't held back in any build tested, and *Turrican 2*'s 7V build doesn't hold back any voice, so an envelope right after DMA on steps a tick sooner there. The mixer buffers showed both, on voices the Paula traces can't see.

**Results (mix buffers, byte for byte, with the song in slow RAM where it doesn't fit the game's own space):**
- *Apidya* end song, subsong 1, all 6:12 on Apidya's own build at setting 17: 16,230 of 18,100 ticks identical, 6,132 bytes differing (0.1%), mostly single bytes at a loop point. Hardware voices 0–2: 0 of 18,201 ticks differ. Subsong 0, 3,900 ticks: 3,595 identical, 1,463 bytes differing, plus the two restarts.
- *Carl Lewis Challenge*: all 1,200 of 1,200 ticks identical. *Apidya* title: 1,499 of 1,500 (one block length at a mid-song change). *Turrican 3* intro, all 5:08 at setting 20: 14,970 of 15,000 (at 14: 13,764). About 20 of the rest are one-tick differences where two notes reach voice 4 a tick apart: the original briefly starts the first, and TFMXreplay doesn't.
- *Turrican 2* title, subsong 0: 1,997 of the first 2,000 ticks. Those 3 ticks are a 1F with no previous note, which reads a period from outside the table, so only that build's memory can answer. Over 6,000 ticks the rest are single bytes.
- *Hexuma* title: 1,186 of 1,200, not yet explained.
- *B.C. Kid*'s build runs setting 13 when the harness asks for 9, so its results are at 13; the game itself uses 9.
- The WinUAE recording of *Turrican 2*: left channel at gain 1.00 with residuals down to −41 dB, and the offset from the hardware voices within 2 samples. In the first two minutes the game itself loses a few ticks, which the song's timing doesn't explain.

**Song ends.** A trackstep line that stops every track is the end of a song, not a loop, even though the sequencer's state then repeats. 606 of the 1,608 subsongs in the modland collection end this way and had been counted as looped, with "loops" of at most 0.16 s; 13 of them are songs of 30 s or more. They now play on for the two seconds that let the last notes ring out.

**Mixer restart.** Trackstep EFFE0003 with 7V already on restarts the mixer. In that tick the block is mixed at the old length as usual, so the voices step through all of it, but only its first 2 words play, and the mix then stays silent for one block of the new length before the next block plays. The ticks wait with it, so each change costs a tick. The harness shows the 2-word block and the stepping, but not the gap, which needs a real recording: a WinUAE recording of the *Apidya* end music shows a gap of about 20 ms at each change, and with it the song stays locked to the recording from start to end.

**The *Apidya* end music against that recording.** Setting 17 fits, read from a savestate of the running game: at 18, the song drifts over a second behind by the end. Left channel at gain 1.00 with residuals down to −36 dB. The percussion loop is one 29,504-word sample at period 158, restarted every 128 ticks. At setting 17 that leaves a gap of about 6 ms, in which the sample starts again before the restart cuts it off, and this click is in the original too. The restarts land within about 2 ms of the recording, on either side: the moment of a hardware write varies with how much the mixer has to do, which a fixed 82% can't follow.

**The *Turrican 3* title music against a WinUAE recording.** In the jukebox it stays locked for the whole song, to within a millisecond, with the left channel at gain 1.00 and residuals down to −30 dB. In the intro animation the song loses about 90 ms in steps, as the *Turrican 2* title's timing also steps, which comes from the game rather than the song. The song's left channel runs at the same offset from the right throughout.

### 15.22 Recordings of seven more games (2026-11-05)
WinUAE recordings and savestates of the *Anstoss*, *Bundesliga Manager*, *Carl Lewis Challenge*, *Hexuma*, *B.C. Kid*, *Rock'n'Roll* and *Turrican* titles, against TFMXreplay's output.

- **A PAL frame is 20.03 ms, not 20.** 313 lines of 227 colour clocks, 49.92 Hz. Songs ticked from the frame, without a BPM tempo, ran 0.16% fast. *Turrican 1*'s intro goes from drifting 170 ms in two minutes to locked for four minutes. BPM tempos and 7V aren't affected.
- **Locked for the whole song:** *Anstoss*, *B.C. Kid* (at 9 in the intro and 13 in the jukebox), *Rock'n'Roll*'s *take off song* and *lovely feelings*, *Turrican 1*'s intro. Gains are 0.96–0.99.
- ***Bundesliga Manager*, *Carl Lewis Challenge* and *Hexuma*** lock too, after whole-tick steps in the first minute or two that come from the games: 51 ms, 128 ms and 98 ms.
- ***Hexuma*'s macro vibrato only reaches the mixer.** In its 7V build, voices 4–7 have it, in the mix buffers, while hardware voices don't, in a WinUAE capture of their writes. `TFMX_VARIANT_HEXUMA` does the same. Mix buffers: 7,492 of 7,500 ticks identical.
- *Carl Lewis Challenge* over 9,000 ticks: 8,210 identical, the rest nearly all single bytes. *Bundesliga Manager*'s harness profile restarts the song over the game's own state, so its mix buffers can't be compared; the recording reaches gain 0.99 and −26 dB.
- *Rock'n'Roll*'s title screen plays modland's *take off song*, not *rocknroll-intro*.
