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

// Songs that can't be told from their contents, each checked against its own game's replayer (mdat
// checksums; the names are modland's).
struct KnownSong {
	uint64_t checksum;
	tfmx_variant variant;
};

const KnownSong kKnownSongs[] = {
	{0x0EA0827F2A56176Dull, TFMX_VARIANT_TURRICAN2},        // turrican 2 level 0-intro
	{0xB065F19BB8207C76ull, TFMX_VARIANT_TURRICAN2},        // turrican 2 level 1-desert
	{0xDC7442D54C210DD9ull, TFMX_VARIANT_TURRICAN2},        // turrican 2 level 2-cave
	{0x7B867913AD9FA782ull, TFMX_VARIANT_TURRICAN2},        // turrican 2 level 3-flight
	{0x144B1ABF0E853D75ull, TFMX_VARIANT_TURRICAN2},        // turrican 2 level 4-factory
	{0x2D77785303492C7Bull, TFMX_VARIANT_TURRICAN2},        // turrican 2 level 5-aliens
	{0x11764AF4AFF82D15ull, TFMX_VARIANT_TURRICAN2},        // turrican 2 loading
	{0xB521C066ED2AD597ull, TFMX_VARIANT_TURRICAN3_SMALL},  // turrican 3 loader
	{0x747BC3475AB79E79ull, TFMX_VARIANT_HEXUMA},           // hexuma-house
	{0xA9CE5D9B57BE0DC7ull, TFMX_VARIANT_HEXUMA},           // hexuma-ice
	{0xFF13A4315D659C12ull, TFMX_VARIANT_HEXUMA},           // hexuma-mine
	{0xEF072A9DFFE766A7ull, TFMX_VARIANT_HEXUMA},           // hexuma-ocean
	{0xBDC8105281054FA0ull, TFMX_VARIANT_HEXUMA},           // hexuma-robot
	{0x5E47EB9A0E357E86ull, TFMX_VARIANT_HEXUMA},           // hexuma-stoneage
	{0xC141B7FDDF75EE3Aull, TFMX_VARIANT_MASTERBLAZER},     // masterblazer intro
	{0x33139DC60B7CE354ull, TFMX_VARIANT_MASTERBLAZER},     // z-out
	{0xF9A1F31B42A5E3EDull, TFMX_VARIANT_MASTERBLAZER},     // z-out level 2
	{0x103FD5E1829D5F5Bull, TFMX_VARIANT_MASTERBLAZER},     // z-out level 3
	{0x929A49EFEE90760Dull, TFMX_VARIANT_MASTERBLAZER},     // z-out level 4
	{0x54CA05DAD14C2F49ull, TFMX_VARIANT_MASTERBLAZER},     // z-out level 5
	{0xFCA2DB4D7D677E36ull, TFMX_VARIANT_MASTERBLAZER},     // z-out level 6
	{0x668A930DC707D3DFull, TFMX_VARIANT_ROCKNROLL},        // rocknroll-intro
	{0x9CBEEBC37F5C4ADEull, TFMX_VARIANT_15},               // turrican bonus
};

tfmx_variant detect_variant(const Module &m) {
	for (const KnownSong &song : kKnownSongs)
		if (song.checksum == m.checksum)
			return song.variant;
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
