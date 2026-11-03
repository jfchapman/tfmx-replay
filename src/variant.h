// TFMXreplay: replayer behaviour switches (docs/TFMX-format.md, sections 15.2-15.9).
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
	bool no_macro_vibrato;    // macro 0C is ignored (Hexuma's in-game build)
};

Behaviour behaviour_of(tfmx_variant variant, const Module &module);

// Picks a variant from the song's format features.
tfmx_variant detect_variant(const Module &module);

}  // namespace tfmx
