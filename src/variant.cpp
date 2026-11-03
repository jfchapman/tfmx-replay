// TFMXreplay: replayer behaviour switches.
#include "variant.h"

namespace tfmx {

Behaviour behaviour_of(tfmx_variant variant, const Module &module) {
	Behaviour b{};
	// Turrican 2 shipped two builds: a 7V one for its title (Pro-like) and a small 4-voice one for
	// the loading screens and levels, which is GemX's generation but defers the 00 b1 cut.
	const bool t2_small = variant == TFMX_VARIANT_TURRICAN2 && !module.has_7v;
	if (t2_small)
		variant = TFMX_VARIANT_GEMX;
	switch (variant) {
	case TFMX_VARIANT_PRO:
	case TFMX_VARIANT_GEMX:
	case TFMX_VARIANT_TURRICAN2:
	case TFMX_VARIANT_TURRICAN3_SMALL:
	case TFMX_VARIANT_HEXUMA:
	default:
		// Apidya, B.C. Kid, Blade of Destiny, Turrican 2. GemX and Monkey Island run macros
		// first too, but write the volume a tick late, don't defer the 00 b1 cut, and use 00's
		// b2b3 as a volume. Turrican 2's 7V build runs the effects in a note's first update.
		b.macros_first = true;
		b.volume_late = false;
		b.start_reads_row = true;
		b.dmaoff_defer = true;
		b.note_after_b1_continues = true;
		b.effe1_count = true;
		b.setprevnote_ends = true;
		b.setperiod_ends = false;
		b.scaled_effects = true;
		b.vibrato_even = false;
		b.addbegin_sweep = true;
		b.effe4_fade = true;
		b.split_jumps = true;
		b.play_macro = true;
		b.sid = true;
		b.dmaon_delays_effects = true;
		b.dma_wait_from_command = true;
		b.f7_time = true;
		b.note_skips_effects = variant != TFMX_VARIANT_GEMX && variant != TFMX_VARIANT_TURRICAN2;
		b.no_macro_vibrato = variant == TFMX_VARIANT_HEXUMA;
		// Pattern FA works in some Pro builds and not others (§15.19), but no song written for one
		// that ignores it uses it, so it's on.
		b.fa_fade = variant != TFMX_VARIANT_GEMX;
		if (variant == TFMX_VARIANT_TURRICAN3_SMALL) {
			// A cut-down Pro build: no AddBegin sweep (one add), no 06/15/16, 1C/1D or FC.
			b.addbegin_sweep = false;
			b.no_macro_jumps = true;
			b.no_lock = true;
			b.split_jumps = false;
		}
		if (variant == TFMX_VARIANT_GEMX) {
			b.volume_late = true;
			b.effects_first = true;
			b.dmaoff_defer = false;
			b.dmaon_delays_effects = false;
			b.dma_wait_from_command = false;
			b.play_macro = false;
			b.dmaoff_volume = DmaOffVolume::Voice;
			if (t2_small) {
				b.dmaoff_defer = true;
				b.dmaoff_volume = DmaOffVolume::Ignored;
			}
		}
		break;
	case TFMX_VARIANT_15:
		// Turrican 1 (both of its builds).
		b.bpm_coarse = true;
		b.macros_first = false;
		b.volume_late = true;
		b.effects_first = true;
		b.start_reads_row = false;
		b.dmaoff_defer = false;
		b.note_after_b1_continues = false;
		b.effe1_count = false;
		b.setprevnote_ends = false;
		b.setperiod_ends = false;
		b.scaled_effects = false;
		b.vibrato_even = false;
		b.addbegin_sweep = false;
		b.effe4_fade = true;
		b.fa_fade = true;
		b.split_jumps = true;
		b.play_macro = false;
		b.sid = false;
		break;
	case TFMX_VARIANT_MASTERBLAZER:
		// Masterblazer (three builds), Z-Out title.
		b.macros_first = false;
		b.volume_late = true;
		b.effects_first = true;
		b.start_reads_row = true;
		b.dmaoff_defer = false;
		b.note_after_b1_continues = true;
		b.effe1_count = true;
		b.setprevnote_ends = true;
		b.setperiod_ends = false;
		b.scaled_effects = true;
		b.vibrato_even = false;
		b.addbegin_sweep = false;
		b.effe4_fade = true;
		b.fa_fade = true;
		b.split_jumps = true;
		b.play_macro = false;
		b.sid = false;
		b.volume_on_change = true;
		b.dmaoff_volume = DmaOffVolume::Paula;
		b.f7_time = true;
		break;
	case TFMX_VARIANT_XOUT:
	case TFMX_VARIANT_ROCKNROLL:
		// X-Out, Fly Harder, Abandoned Places; Rock'n'Roll differs only in 17.
		b.no_bpm = true;
		b.macros_first = false;
		b.volume_late = true;
		b.start_reads_row = false;
		b.dmaoff_defer = false;
		b.note_after_b1_continues = false;
		b.effe1_count = false;
		b.setprevnote_ends = true;
		b.setperiod_ends = variant == TFMX_VARIANT_ROCKNROLL;
		b.effects_first = variant != TFMX_VARIANT_ROCKNROLL;
		b.note_tick_skips = variant != TFMX_VARIANT_ROCKNROLL;
		b.interleaved = variant == TFMX_VARIANT_ROCKNROLL;
		b.scaled_effects = false;
		b.vibrato_even = true;
		b.addbegin_sweep = false;
		b.effe4_fade = false;
		b.fa_fade = true;
		b.split_jumps = false;
		b.play_macro = false;
		b.sid = false;
		break;
	}
	return b;
}

tfmx_variant detect_variant(const Module &m) {
	if (m.magic == Magic::Odd)
		return TFMX_VARIANT_ROCKNROLL;
	if (m.magic == Magic::LowerCase)
		return TFMX_VARIANT_MASTERBLAZER;
	if (m.has_7v)
		return TFMX_VARIANT_PRO;
	// 00 with a volume is ignored by most builds, so only songs written for GemX's and Monkey
	// Island's build use it much (the next most is 5, in mlaw1).
	if (m.dmaoff_volumes >= 10)
		return TFMX_VARIANT_GEMX;
	if (m.packed)
		return TFMX_VARIANT_PRO;
	if (!m.has_note_wait)
		return TFMX_VARIANT_XOUT;
	return TFMX_VARIANT_15;
}

}  // namespace tfmx
