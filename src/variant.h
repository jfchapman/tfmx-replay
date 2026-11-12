// TFMXreplay: replayer behaviour switches (docs/TFMX-format.md, section 14).
#pragma once

#include "module.h"
#include "tfmxreplay.h"

namespace tfmx {

// What 00 (DMA off) does with a non-zero b2b3.
enum class DmaOffVolume {
	Ignored,    // most builds
	Voice,      // b2 = 0: volume = velocity * 3 + b3, otherwise b3 (GemX, Monkey Island)
	Paula,      // b2b3 is written to Paula's VOL at once; the voice's volume is kept (Masterblazer)
};

// Each game shipped its own build of the replayer. These are the behaviours that differ.
struct Behaviour {
	DmaOffVolume dmaoff_volume;
	bool macros_first;        // macros run before tracks within a tick (Pro); otherwise tracks first
	bool volume_late;         // Paula VOL is written before the voice's macro runs, so a tick late
	bool effects_first;       // with volume_late: effects run before the writes (1.5, X-Out), not
	                          // between the volume and period writes (Rock'n'Roll)
	bool note_tick_skips;     // with volume_late: in the tick a note arrives, the voice isn't
	                          // updated at all (1.5, X-Out); otherwise only its macro waits
	bool interleaved;         // pattern events reach a voice at its next update (Rock'n'Roll)
	bool start_reads_row;     // starting a song reads row 0 at once (Pro); otherwise on the first tick
	bool dmaoff_defer;        // 00/13 with b1 != 0 cut on the next tick, ending this one
	bool note_after_b1_continues;  // the first note command after 00 b1 != 0 doesn't end the tick
	bool effe1_count;         // trackstep EFFE0001 honours its count (otherwise it always jumps)
	bool setprevnote_ends;    // 1F ends the tick
	bool setperiod_ends;      // 17 ends the tick
	bool scaled_effects;      // vibrato, portamento and finetune scale the period (otherwise add)
	bool vibrato_even;        // the vibrato speed is masked to even
	bool addbegin_sweep;      // 11 with a count sweeps (otherwise a single add)
	bool effe4_fade;          // trackstep fade EFFE0004 works
	bool fa_fade;             // pattern fade FA works
	bool split_jumps;         // 1C/1D jump if note/volume > b1 (otherwise ignored)
	bool play_macro;          // 21 works
	bool sid;                 // 22-29 work
	bool sid_filter_carry;    // the SID slew filter keeps its value from tick to tick (Pro builds);
	                          // otherwise it restarts every tick
	bool sid_effects_gate;    // a SID buffer is rebuilt only in ticks where the voice's effects run
	                          // and its macro ran no DMA off (Pro builds, Turrican 2's small build)
	int mix_setting;          // the 7V mixer setting of the song's game
	bool dmaon_delays_effects;  // 01 with b1 = 0 holds the effects back until the next tick
	bool bpm_coarse;          // BPM tempo sets only the CIA timer's high byte: 7093 / BPM (1.5)
	bool no_bpm;              // no BPM mode: a tempo >= 16 is speed (tempo - 16) at 50 Hz (X-Out)
	bool dma_wait_from_command; // 1A counts block starts from the command (Pro: its own interrupt
	                            // handler); otherwise the one pending since DMA start counts too
	bool f7_time;             // F7's b2 high nibble is a time (otherwise it always steps every 2 ticks)
	bool volume_on_change;    // at master volume 64, VOL is written only when the voice's volume
	                          // changes (Masterblazer)
	bool note_skips_effects;  // the update in which a pattern note's macro starts runs no effects
	                          // (Pro, except Turrican 2's 7V build)
	bool no_macro_jumps;      // 06, 15 and 16 are ignored (Turrican 3's small build)
	bool no_lock;             // pattern FC is ignored (Turrican 3's small build)
	bool no_macro_vibrato;    // macro 0C is ignored on hardware voices (Hexuma)
	bool track_mute;          // a non-zero header word at 0x1C0 + 2t mutes track t: it runs, but
	                          // starts no notes (Logical, B.C. Kid, Masterblazer)
	bool note_same_tick;      // with tracks first, a note's macro starts in the same tick
	bool note_resets_effects; // a note stops the voice's effects (vibrato, portamento, envelope)
	bool loop_exit_ends;      // 05 ends the tick when its count runs out
	bool porta_from_dest;     // 0B starts from the last slide's destination, even if the slide
	                          // hadn't got there (Rock'n'Roll, Danger Freak)
	bool periods_48;          // the period table has four octaves; notes 48-63 give period 0
	bool fa_line_jump;        // a trackstep line starting FAxx jumps to the line in word 2
	bool env_overshoot;       // an envelope ending past its target writes the target, but the
	                          // next envelope starts from the value past it
	bool dmaon_immediate;     // 01 writes DMA on at once, so Paula takes the sample set before it
	                          // rather than one set later in the tick (Danger Freak)
};

Behaviour behaviour_of(tfmx_variant variant, const Module &module);

// Picks a variant from the song's format features.
tfmx_variant detect_variant(const Module &module);

}  // namespace tfmx
