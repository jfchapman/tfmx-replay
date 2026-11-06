// TFMXreplay: the sequencer and macro engine.
//
// Written from docs/TFMX-format.md, which was itself derived from black-box traces of the
// original replayers. Section numbers in comments refer to that document.
#include "replayer.h"

#include <algorithm>
#include <cstring>

namespace tfmx {

namespace {

// §9: four octaves, then the top octave again.
const uint16_t kPeriods[64] = {
	1710, 1614, 1524, 1438, 1357, 1281, 1209, 1141, 1077, 1017, 960, 908,
	856, 810, 764, 720, 680, 642, 606, 571, 539, 509, 480, 454,
	428, 404, 381, 360, 340, 320, 303, 286, 270, 254, 240, 227,
	214, 202, 191, 180, 170, 160, 151, 143, 135, 127, 120, 113,
	214, 202, 191, 180, 170, 160, 151, 143, 135, 127, 120, 113,
	214, 202, 191, 180,
};

// Macros run in this order; in 7V mode hardware voice 3 (the mix) comes last.
const int kOrder4[4] = {0, 1, 2, 3};
const int kOrder7[8] = {0, 1, 2, 4, 5, 6, 7, 3};


inline int s8(uint32_t v) { return int8_t(uint8_t(v)); }
inline int s16(uint32_t v) { return int16_t(uint16_t(v)); }

const int kMaxCommandsPerTick = 256;  // guards against macros that loop without waiting

}  // namespace

void Replayer::init(Module *module, const Behaviour &behaviour, Paula *paula) {
	m_ = module;
	b_ = behaviour;
	paula_ = paula;
}

void Replayer::write(int voice, PaulaWrite::Reg reg, uint32_t value) {
	if (log)
		log->push_back({voice, reg, value});
	if (voice >= paula_->channels())
		return;
	Channel &c = paula_->channel(voice);
	switch (reg) {
	case PaulaWrite::LC: c.lc = value; break;
	case PaulaWrite::LEN: c.len = uint16_t(value); break;
	case PaulaWrite::PER: c.per = uint16_t(value); break;
	case PaulaWrite::VOL: c.vol = int(value & 0x7F); break;  // 7 bits; 64 and above are full
	case PaulaWrite::DMA_OFF:
		paula_->dma_off(voice);
		// DMA off also clears the voice's pending audio interrupt (macro 1A counts from here).
		voice_[voice].dma_wait_mark = c.block_starts;
		break;
	case PaulaWrite::DMA_ON:
		// In 7V the replayer runs after the tick's mixing, so its DMA starts come when that's done:
		// measured from recordings of eight songs, 2.4 ms + 0.08 ms per word of the block (§11.4).
		paula_->dma_on(voice, seven_ ? 0.0024 + 0.00008 * (playing_words_ ? playing_words_ : mix_words()) : 0.0);
		break;
	case PaulaWrite::NOTE: break;
	}
}

void Replayer::start(const Subsong &subsong) {
	sub_ = subsong;
	for (Track &t : tracks_)
		t = Track();
	for (Voice &v : voice_)
		v = Voice();
	paula_->reset(8);
	// Starting a song silences every voice.
	for (int n = 0; n < 4; n++) {
		write(n, PaulaWrite::VOL, 0);
		write(n, PaulaWrite::DMA_OFF, 0);
	}
	line_ = sub_.start;
	trackstep_loop_ = -1;
	if (sub_.tempo >= 16 && b_.no_bpm) {
		bpm_ = 0;
		speed_ = sub_.tempo - 16;
	} else if (sub_.tempo >= 16) {
		bpm_ = sub_.tempo;
		speed_ = 0;
	} else {
		bpm_ = 0;
		speed_ = sub_.tempo;
	}
	seven_ = false;
	seven_adjust_ = 0;
	mix_restart_ = false;
	playing_words_ = next_words_ = 0;
	master_ = 64;
	fading_ = false;
	ended_ = false;
	loops_ = 0;
	restarts_ = 0;
	idle_ticks_ = 0;
	seen_.clear();
	line_time_.assign(size_t(m_->trackstep_lines), -1.0);
	loop_start_ = loop_end_ = -1;
	// The first rows are read in tick 0 (Pro, Masterblazer) or tick 1 (1.5, X-Out). When the song
	// comes back to a line later, its rows are read at once, so the first time counts from there.
	time_ = b_.start_reads_row ? 0 : tick_seconds();
	load_line();
	time_ = 0;
	row_counter_ = b_.start_reads_row ? 0 : 1;
}

// The song has come back to something it played at time 'target'. The first time is the loop point.
void Replayer::mark_loop(double target) {
	loops_++;
	if (loop_end_ < 0) {
		loop_start_ = target;
		loop_end_ = time_;
	}
}

// The mix block, from trackstep EFFE0003 and the mixer setting: round((100 + x) * setting / 10) words
// at a period of about 3580 / setting.
int Replayer::mix_words() const { return ((100 + std::max(seven_adjust_, -32)) * mix_setting_ + 5) / 10; }
int Replayer::mix_period() const { return (3580 + mix_setting_ / 2) / mix_setting_; }

double Replayer::tick_seconds() const {
	if (seven_) {
		// The tick is one block of the mix buffer on voice 3, 4 CPU cycles per word and period unit.
		// That's about 20.2 ms at x = 0 (§11.2).
		return (playing_words_ ? playing_words_ : mix_words()) * mix_period() * 4 / 7093790.0;
	}
	if (bpm_ >= 16) {
		// A BPM tempo programs a CIA timer (PAL clock 709,379 Hz). Pro builds use 1,790,456 / BPM
		// (Masterblazer: 125 gives $37F3); Turrican 1 sets only the high byte, 7093 / BPM.
		const double timer = b_.bpm_coarse ? double((7093 / bpm_) << 8) : double(1790456 / bpm_);
		return timer / 709379.0;
	}
	return 71051 / kPaulaClock;  // a PAL frame: 313 lines of 227 colour clocks, 49.92 Hz
}

int Replayer::period_of(int note, int finetune) const {
	const int base = kPeriods[note & 0x3F];
	// §9: Pro-style builds scale, older builds add.
	if (b_.scaled_effects)
		return base * (256 + finetune) >> 8;
	return base + finetune;
}

// Master volume fade: every 'speed' voice updates, one step toward 'target'. Speed 0 sets the
// volume at once.
void Replayer::start_fade(int speed, int target) {
	fade_speed_ = speed;
	fade_target_ = target;
	fade_counter_ = speed;
	fading_ = speed != 0 && master_ != target;
	if (!speed)
		master_ = target;
}

// A note or period command. While a portamento runs it only sets the destination.
void Replayer::set_period(int n, int period) {
	Voice &v = voice_[n];
	v.dest_period = period;
	if (v.porta)
		return;
	v.period = v.base = period;
	if (b_.volume_late)
		write(n, PaulaWrite::PER, uint32_t(period));
}

void Replayer::reset_effects(Voice &v) {
	v.vib_speed = 0;
	v.vib_offset = 0;
	v.porta = false;
	v.envelope = false;
	v.addbegin = false;
}

// --- trackstep ----------------------------------------------------------------------------

void Replayer::load_line() {
	for (int guard = 0; guard < 64; guard++) {
		if (line_ < 0 || line_ >= m_->trackstep_lines) {
			ended_ = true;
			return;
		}
		if (line_time_[size_t(line_)] < 0)
			line_time_[size_t(line_)] = time_;
		const uint8_t *l = m_->trackstep_line(line_);
		if (be16(l) == 0xEFFE) {
			const int cmd = be16(l + 2), w3 = be16(l + 4), w4 = be16(l + 6);
			switch (cmd) {
			case 0:  // stop the song
				ended_ = true;
				return;
			case 1:  // loop: jump to w3, w4 times (0 = forever)
				// A count of 0 or FF00 means forever (Turrican 1's songs use FF00 to loop the song).
				if (!b_.effe1_count || w4 == 0 || w4 == 0xFF00) {
					if (w3 <= line_)  // a backward jump forever is the song's loop point
						mark_loop(line_time_[size_t(w3)]);
					line_ = w3;
					continue;
				}
				if (trackstep_loop_ < 0) {
					trackstep_loop_ = w4;
					line_ = w3;
					continue;
				}
				if (--trackstep_loop_ > 0) {
					line_ = w3;
					continue;
				}
				trackstep_loop_ = -1;
				break;
			case 2:  // tempo
				speed_ = w3;
				if (!(w4 & 0xF200) && (w4 & 0x1FF) > 15)
					bpm_ = w4 & 0x1FF;
				break;
			case 3:  // 7-voice mode
				if (!(w4 & 0x8000)) {
					mix_restart_ = seven_;
					seven_ = true;
					seven_adjust_ = s8(w4);
					paula_->set_7v(true);
				}
				break;
			case 4:  // master fade
				if (b_.effe4_fade)
					start_fade(w3 & 0xFF, w4 & 0xFF);
				break;
			default:
				break;
			}
			line_++;
			if (line_ > sub_.end) {
				line_ = sub_.start;
				mark_loop(line_time_[size_t(line_)]);
			}
			continue;
		}
		// Eight track words: pattern number (or code) and transpose.
		for (int t = 0; t < 8; t++) {
			const int w = be16(l + 2 * t), hi = w >> 8;
			Track &tr = tracks_[t];
			tr.transpose = s8(w);
			if (hi < 0x80) {
				tr.active = m_->patterns[size_t(hi)] != 0;
				tr.pattern = m_->patterns[size_t(hi)];
				tr.step = 0;
				tr.wait = 0;
				tr.loop = -1;
			} else if (hi == 0x80) {
				// Keep the pattern running with the new transpose.
			} else if (hi == 0xFE) {
				const int v = w & 7;
				if (after_update(v)) {
					// The voice was already updated this tick: it stops at its next update.
					voice_[v].queued_stop = true;
					voice_[v].queued_tick = tick_count_;
				} else {
					write(v, PaulaWrite::DMA_OFF, 0);
					voice_[v].macro_on = false;
				}
				tr.active = false;
			} else {
				tr.active = false;
			}
		}
		return;
	}
	ended_ = true;
}

void Replayer::advance_line() {
	line_++;
	if (line_ > sub_.end) {
		line_ = sub_.start;
		mark_loop(line_time_[size_t(line_)]);
	}
	load_line();
}

// --- patterns -----------------------------------------------------------------------------

void Replayer::process_rows() {
	restarts_ = 0;
	const int loops_before = loops_;
	for (int t = 0; t < 8 && !ended_; t++) {
		if (!tracks_[t].active)
			continue;
		if (run_track(t)) {
			// F0: the trackstep moved on, and every track is processed again in this tick,
			// including tracks that keep their pattern (8000) and were already handled.
			if (++restarts_ > 16)
				return;
			t = -1;
		}
	}
	// Some songs loop inside a pattern (F1 with count 0) and never reach F0. The sequencer only
	// depends on the trackstep and track state, so once that repeats exactly, the song has looped.
	if (loops_ != loops_before)
		seen_.clear();
	// With every track stopped the state can't change, but that's the song's end, not a loop: tick()
	// lets the last sounds ring out.
	bool any = false;
	for (const Track &t : tracks_)
		any |= t.active;
	if (!any)
		return;
	uint64_t h = 1469598103934665603ull;
	auto mix = [&](uint64_t x) { h = (h ^ x) * 1099511628211ull; };
	mix(uint64_t(line_));
	mix(uint64_t(trackstep_loop_ + 1));
	mix(uint64_t(speed_));
	for (const Track &t : tracks_) {
		mix(t.active ? t.pattern : 0);
		mix(t.active ? t.step : 0);
		mix(uint64_t(t.wait) << 32 | uint32_t(t.loop + 1));
		mix(uint64_t(t.transpose & 0xFF) << 32 | t.return_pattern);
		mix(t.return_step);
	}
	const auto state = seen_.find(h);
	if (state != seen_.end()) {
		mark_loop(state->second);
		seen_.clear();
	}
	seen_.emplace(h, time_);
}

bool Replayer::run_track(int t) {
	Track &tr = tracks_[t];
	current_track_ = t;
	if (tr.wait > 0) {
		tr.wait--;
		return false;
	}
	for (int guard = 0; guard < 256; guard++) {
		const uint32_t at = tr.pattern + 4 * tr.step;
		if (at + 4 > m_->mdat.size()) {
			tr.active = false;
			return false;
		}
		const uint8_t *c = &m_->mdat[at];
		tr.step++;
		const int b0 = c[0], b1 = c[1], b2 = c[2], b3 = c[3];
		// The voice for porta notes and F5/F6/F7/FC; in 4-voice songs 4-7 wrap to 0-3 (Monkey Island).
		const int voice = b2 & (seven_ ? 7 : 3);
		if (b0 < 0xC0) {
			// Note: 00-7F then fetch on; 80-BF then wait b3 rows.
			const int note = ((b0 & 0x3F) + tr.transpose) & 0x3F;
			note_on(b2 & 0x0F, note, b1, b2 >> 4, b0 < 0x80 ? s8(uint32_t(b3)) : 0);
			if (b0 >= 0x80) {
				tr.wait = b3;
				return false;
			}
			continue;
		}
		if (b0 < 0xF0) {
			// Portamento to a note: b1 = speed, b3 = rate. Doesn't restart the macro.
			const int n = voice;
			const int note = ((b0 & 0x3F) + tr.transpose) & 0x3F;
			if (after_update(n)) {
				// The voice was already updated this tick: it applies at the next update.
				Voice &v = voice_[n];
				v.queued_porta = true;
				v.queued_porta_note = note;
				v.queued_porta_speed = b1;
				v.queued_porta_rate = b3;
				v.queued_tick = tick_count_;
			} else {
				start_porta(n, note, b1, b3);
			}
			continue;
		}
		switch (b0) {
		case 0xF0:  // end: the whole trackstep advances
			advance_line();
			return true;
		case 0xF1:  // loop b1 times (0 = forever) back to step b2b3
			if (b1 == 0) {
				tr.step = uint32_t(b2 << 8 | b3);
			} else if (tr.loop < 0) {
				tr.loop = b1;
				tr.step = uint32_t(b2 << 8 | b3);
			} else if (--tr.loop > 0) {
				tr.step = uint32_t(b2 << 8 | b3);
			} else {
				tr.loop = -1;
			}
			break;
		case 0xF2:  // jump to pattern b1, step b2b3
			tr.pattern = m_->patterns[size_t(b1 & 0x7F)];
			tr.step = uint32_t(b2 << 8 | b3);
			if (!tr.pattern) {
				tr.active = false;
				return false;
			}
			break;
		case 0xF3:  // wait b1 rows
			tr.wait = b1;
			return false;
		case 0xF4:  // stop this track
		case 0xFE:
			tr.active = false;
			return false;
		case 0xF5:  // key up
			voice_[voice].key = false;
			break;
		case 0xF6: {  // vibrato
			Voice &v = voice_[voice];
			v.vib_speed = b1 & 0xFE;
			v.vib_delta = s8(uint32_t(b3));
			v.vib_counter = v.vib_speed >> 1;
			v.vib_offset = 0;
			break;
		}
		case 0xF7: {  // envelope on voice b2: as macro 0F, one step every (b2 >> 4) + 2 ticks
			// (older builds ignore the nibble and always step every 2 ticks).
			Voice &v = voice_[voice];
			v.env_step = b1;
			v.env_time = b_.f7_time ? (b2 >> 4) + 1 : 1;
			v.env_counter = v.env_time;
			v.env_target = b3;
			v.envelope = true;
			break;
		}
		case 0xF8:  // gosub
			tr.return_pattern = tr.pattern;
			tr.return_step = tr.step;
			tr.pattern = m_->patterns[size_t(b1 & 0x7F)];
			tr.step = uint32_t(b2 << 8 | b3);
			if (!tr.pattern) {
				tr.active = false;
				return false;
			}
			break;
		case 0xF9:  // return
			tr.pattern = tr.return_pattern;
			tr.step = tr.return_step;
			break;
		case 0xFA:  // master fade
			if (b_.fa_fade)
				start_fade(b1, b3);
			break;
		case 0xFB: {  // play pattern b1 on track b2 (the b3 transpose didn't show in traces)
			Track &o = tracks_[b2 & 7];
			o.pattern = m_->patterns[size_t(b1 & 0x7F)];
			o.active = o.pattern != 0;
			o.step = 0;
			o.wait = 0;
			o.loop = -1;
			break;
		}
		case 0xFC:  // lock a voice against new notes
			if (b1 && !b_.no_lock)
				voice_[voice].lock = b3 + 2;
			break;
		default:  // FD (cue), FF (no-op)
			break;
		}
	}
	return false;
}

void Replayer::note_on(int n, int note, int macro, int velocity, int finetune) {
	n &= seven_ ? 7 : 3;
	Voice &v = voice_[n];
	if (log)  // for tests: the note, macro, velocity and the track that played it
		log->push_back({n, PaulaWrite::NOTE, uint32_t(note << 16 | (macro & 0x7F) << 8 | velocity << 4 | (current_track_ & 15))});
	if (v.lock > 0)
		return;
	const uint32_t mac = m_->macros[size_t(macro & 0x7F)];
	if (!mac)
		return;
	if (after_update(n)) {
		// The voice was already updated this tick: the note takes over at its next update.
		v.queued = true;
		v.queued_note = note;
		v.queued_macro = macro;
		v.queued_velocity = velocity;
		v.queued_finetune = finetune;
		v.queued_tick = tick_count_;
		return;
	}
	// The macro first runs on the next tick. With tracks first, that means skipping this one.
	start_note(n, note, macro, velocity, finetune, b_.macros_first ? 0 : 1);
	v.skip_update = b_.volume_late;
	v.skip_effects = b_.note_skips_effects;
}

// Rock'n'Roll: pattern events (notes, porta notes, trackstep stops) reach a voice at its next
// update, after the voice's current macro has run once more. (In about one case in ten the
// original skips the voice's update instead; what decides that isn't known yet.)
bool Replayer::after_update(int /*n*/) const {
	return b_.interleaved;
}

// A porta note (pattern $C0-$EF): slide to 'note' without restarting the macro.
void Replayer::start_porta(int n, int note, int speed, int rate) {
	Voice &v = voice_[n];
	v.dest_period = period_of(note, v.finetune);
	// It also becomes the voice's note, which later note commands (08) build on.
	v.note = note;
	v.porta_speed = std::max(speed, 1);
	v.porta_rate = rate;
	v.porta_counter = 0;
	v.porta = true;
}

void Replayer::start_note(int n, int note, int macro, int velocity, int finetune, int delay) {
	Voice &v = voice_[n];
	const uint32_t mac = m_->macros[size_t(macro & 0x7F)];
	v.prev_note = v.note;
	v.note = note;
	v.velocity = velocity;
	v.finetune = finetune;
	v.key = true;
	v.macro = mac;
	v.step = 0;
	v.wait = 0;
	v.loop = -1;
	v.dma_wait = -1;
	v.has_return = false;
	v.macro_on = true;
	v.start_delay = delay;
}

// --- macros -------------------------------------------------------------------------------

void Replayer::set_begin(int n, uint32_t begin) {
	Voice &v = voice_[n];
	v.begin = begin & 0xFFFFFF;
	write(n, PaulaWrite::LC, v.begin);
}

void Replayer::set_len(int n, uint16_t len) {
	voice_[n].len = len;
	write(n, PaulaWrite::LEN, len);
}

void Replayer::tick() {
	if (ended_)
		return;
	tick_count_++;
	const int count = seven_ ? 8 : 4;
	const int *order = seven_ ? kOrder7 : kOrder4;

	// Deferred cuts (00/13 with b1 != 0) happen together at the start of the macro phase.
	auto deferred_cuts = [&] {
		for (int i = 0; i < count; i++) {
			Voice &v = voice_[order[i]];
			if (v.cut_next_tick) {
				v.cut_next_tick = false;
				if (v.sweep_stops_at_cut)
					v.addbegin = false;
				write(order[i], PaulaWrite::DMA_OFF, 0);
			}
		}
	};
	auto macro_phase = [&] {
		deferred_cuts();
		for (int i = 0; i < count; i++)
			run_voice(order[i]);
		// DMA starts are batched at the end, then every voice's period.
		for (int i = 0; i < count; i++) {
			Voice &v = voice_[order[i]];
			if (v.dma_on_request) {
				v.dma_on_request = false;
				write(order[i], PaulaWrite::DMA_ON, 0);
			}
		}
		for (int i = 0; i < count && !b_.volume_late; i++) {
			const Voice &v = voice_[order[i]];
			if (v.period)
				write(order[i], PaulaWrite::PER, uint32_t(v.period));
		}
	};
	auto track_phase = [&] {
		if (row_counter_ > 0) {
			row_counter_--;
			return;
		}
		// The row counter takes the speed after the rows, so a speed change in the trackstep
		// (EFFE 0002) already applies to the first row of the line after it.
		process_rows();
		row_counter_ = speed_;
	};

	if (b_.macros_first) {
		macro_phase();
		track_phase();
	} else {
		track_phase();
		macro_phase();
	}
	if (seven_) {
		// The tick lasts as long as the block playing, which was mixed last tick. A tempo change in
		// mid-song restarts the mixer: the block is mixed at the old length, as usual, but only 2 words
		// of it play, and then there's a silent gap as long as a new block.
		const int gap = mix_restart_ ? 2 * mix_words() : 0;
		const int bytes = mix_restart_ ? 4 : 2 * mix_words();
		paula_->mix_block(bytes, mix_period(), mix_restart_ ? 2 * next_words_ : bytes, gap);
		playing_words_ = next_words_ ? next_words_ : (bytes + gap) / 2;
		next_words_ = (bytes + gap) / 2;
		mix_restart_ = false;
	}
	for (Voice &v : voice_)
		if (v.lock > 0)
			v.lock--;

	// Jingles end by stopping every track (F4) instead of looping. Once nothing can play another
	// note, give the last sounds two seconds to ring out, then end.
	bool any = false;
	for (const Track &t : tracks_)
		any |= t.active;
	if (any) {
		idle_ticks_ = 0;
	} else if (++idle_ticks_ * tick_seconds() >= 2.0) {
		ended_ = true;
	}
	time_ += tick_seconds();
}

void Replayer::run_voice(int n) {
	Voice &v = voice_[n];
	auto write_volume = [&] {
		// Masterblazer: at full master volume, VOL is only written when the voice's volume changes,
		// so the last step of a fade up to 64 doesn't reach Paula until then.
		if (b_.volume_on_change && master_ == 64 && v.volume == v.written_volume)
			return;
		v.written_volume = v.volume;
		write(n, PaulaWrite::VOL, uint32_t(v.volume * master_ / 64));
		if (seven_ && n >= 4)
			paula_->channel(n).mix_volume = v.volume;
	};
	auto run_effects = [&] {
		if (v.skip_effects) {
			// Pro: the update in which a note's macro starts runs no effects.
			v.skip_effects = false;
			if (v.effects_next_tick) {
				v.effects_next_tick = false;
				v.effects = true;
			}
		} else if (v.effects_next_tick) {
			v.effects_next_tick = false;
			v.effects = true;
		} else if (v.effects) {
			effects(n);
		}
		// In 7V the fade steps for the seven real voices, not for hardware voice 3 (the mix).
		if (!(seven_ && n == 3))
			fade_step();
	};
	if (v.queued_stop && v.queued_tick != tick_count_) {
		v.queued_stop = false;
		write(n, PaulaWrite::DMA_OFF, 0);
		v.macro_on = false;
	}
	// Rock'n'Roll: the update in which a queued note's macro starts writes neither volume nor
	// period (the effects still run, and so does the new macro).
	bool writes = true;
	if (v.queued && v.queued_tick != tick_count_) {
		v.queued = false;
		start_note(n, v.queued_note, v.queued_macro, v.queued_velocity, v.queued_finetune, 0);
		writes = false;
	}
	if (v.queued_porta && v.queued_tick != tick_count_) {
		v.queued_porta = false;
		start_porta(n, v.queued_porta_note, v.queued_porta_speed, v.queued_porta_rate);
	}
	if (b_.volume_late && b_.macros_first && v.skip_update) {
		// GemX: the first update after a note runs only the new macro, with no effects and no
		// volume or period writes.
		v.skip_update = false;
		run_macro(n);
	} else if (b_.volume_late) {
		// In the tick a note arrives (tracks run first), the voice isn't updated at all.
		if (v.macro_on && v.start_delay > 0 && (b_.note_tick_skips || v.skip_update)) {
			v.skip_update = false;
			v.start_delay--;
			return;
		}
		// Older builds write the voice's volume and period, then run its macro, so a macro's
		// changes reach Paula a tick late; note commands write the period at once. The effects
		// run before both writes (1.5, X-Out), or between the volume and period writes
		// (Rock'n'Roll: an envelope's change reaches Paula a tick later than a vibrato's).
		if (b_.effects_first)
			run_effects();
		if (writes)
			write_volume();
		if (!b_.effects_first)
			run_effects();
		if (v.period && writes)
			write(n, PaulaWrite::PER, uint32_t(v.period));
		run_macro(n);
	} else {
		run_macro(n);
		run_effects();
	}
	if (v.sid)
		run_sid(n);
	if (!b_.volume_late)
		write_volume();
}

void Replayer::run_macro(int n) {
	Voice &v = voice_[n];
	if (!v.macro_on)
		return;
	if (v.start_delay > 0) {
		v.start_delay--;
		return;
	}
	if (v.wait > 0) {
		v.wait--;
		return;
	}
	if (v.dma_wait >= 0) {
		// 1A: wait for Paula block starts (audio interrupts) since the voice's interrupt was last
		// cleared, by DMA off or by an earlier 1A. The one raised at DMA start counts.
		const uint32_t starts = paula_->channel(n).block_starts;
		if (int(starts - v.dma_wait_mark) <= v.dma_wait)
			return;
		v.dma_wait_mark = starts;
		v.dma_wait = -1;
	}
	for (int i = 0; i < kMaxCommandsPerTick && v.macro_on; i++) {
		const uint32_t at = v.macro + 4 * v.step;
		if (at + 4 > m_->mdat.size()) {
			v.macro_on = false;
			return;
		}
		v.step++;
		if (exec(n, &m_->mdat[at]))
			return;
	}
}

bool Replayer::exec(int n, const uint8_t *c) {
	Voice &v = voice_[n];
	const int cmd = c[0], b1 = c[1], b2 = c[2], b3 = c[3];
	const uint32_t w23 = uint32_t(b2 << 8 | b3), l123 = uint32_t(b1 << 16 | b2 << 8 | b3);

	// Note commands end the tick, except the first one after a deferred DMA off.
	auto note_ends = [&] {
		if (v.note_free) {
			v.note_free = false;
			return false;
		}
		return true;
	};
	auto dma_off = [&](bool reset) {
		if (b1 && b_.dmaoff_defer) {
			// A DMAon earlier in this tick still starts the sample; the cut comes next tick.
			// The AddBegin sweep only stops with the cut, so it steps once more this tick; the
			// other effects reset at once.
			if (reset) {
				const bool sweep = v.addbegin;
				reset_effects(v);
				v.addbegin = sweep;
			}
			v.cut_next_tick = true;
			v.sweep_stops_at_cut = reset;
			v.note_free = true;
			return true;
		}
		v.dma_on_request = false;
		if (reset)
			reset_effects(v);
		write(n, PaulaWrite::DMA_OFF, 0);
		if (b1 && b_.note_after_b1_continues)
			v.note_free = true;
		return false;
	};

	switch (cmd) {
	case 0x00:  // DMA off and reset effects; some builds also use b2b3 as a volume
		if (w23 && b_.dmaoff_volume == DmaOffVolume::Voice) {
			// "flag / addset / vol": b2 = 0 adds b3 to velocity * 3, otherwise b3 is the volume.
			// It reaches Paula at once, even in builds that otherwise write the volume late.
			if (b3) {
				v.volume = b2 ? b3 : (v.velocity * 3 + b3) & 0xFF;
				write(n, PaulaWrite::VOL, uint32_t(v.volume * master_ / 64));
			}
		} else if (w23 && b_.dmaoff_volume == DmaOffVolume::Paula) {
			write(n, PaulaWrite::VOL, w23);
		}
		return dma_off(true);
	case 0x01:  // DMA on; b1 = effects: 0 next tick, 1-7F now, 80-FF off
		if (v.sid) {
			set_begin(n, 4 + 0x100u * unsigned(n));
			set_len(n, uint16_t(std::max(v.sid_length / 2, 1)));
		}
		v.dma_on_request = true;
		if (seven_ && n >= 4)
			paula_->mix_latch(n, v.period, v.volume);
		// Pro clears the voice's audio interrupt here, so a request still pending from an
		// earlier DMA start doesn't count for a later 1A (a fresh DMA start raises a new one).
		if (b_.dma_wait_from_command)
			v.dma_wait_mark = paula_->channel(n).block_starts;
		if (b1 >= 0x80) {
			v.effects = false;
		} else if (b1 == 0 && b_.dmaon_delays_effects && !(seven_ && n >= 4)) {
			v.effects = false;
			v.effects_next_tick = true;
		} else {
			v.effects = true;
		}
		return false;
	case 0x02:  // set sample begin
		v.addbegin = false;
		set_begin(n, l123);
		return false;
	case 0x03:  // set sample length (words)
		set_len(n, uint16_t(w23));
		return false;
	case 0x04:  // wait b2b3 ticks
		v.wait = int(w23);
		return note_ends();
	case 0x05:  // loop b1 times (0 = forever) to step b2b3
	case 0x10:  // the same, until key up
		// The counter is shared with 14's timeout, so a loop around a 14 never runs out, and a
		// "forever" loop reached with the counter at 0 falls through (Masterblazer loader).
		if (cmd == 0x10 && !v.key)
			return false;  // key up: fall through, leaving the counter alone
		if (v.loop == 0) {
			v.loop = -1;
			return false;
		}
		if (--v.loop < 0)
			v.loop = b1 - 1;
		v.step = w23;
		return false;
	case 0x06:  // continue with macro b1 at step b2b3
	case 0x15:  // gosub
		if (b_.no_macro_jumps)
			return false;
		if (cmd == 0x15) {
			v.return_macro = v.macro;
			v.return_step = v.step;
			v.has_return = true;
		}
		v.macro = m_->macros[size_t(b1 & 0x7F)];
		v.step = w23;
		v.loop = -1;
		if (!v.macro)
			v.macro_on = false;
		return false;
	case 0x16:  // return
		if (v.has_return && !b_.no_macro_jumps) {
			v.macro = v.return_macro;
			v.step = v.return_step;
			v.has_return = false;
		}
		return false;
	case 0x07:  // stop
		v.macro_on = false;
		return true;
	// Note commands: the detune is the note's finetune plus the signed word b2b3.
	case 0x08:  // add note: current note + b1
		set_period(n, period_of(v.note + s8(uint32_t(b1)), v.finetune + s16(w23)));
		return note_ends();
	case 0x09:  // set note b1
		set_period(n, period_of(b1, v.finetune + s16(w23)));
		return note_ends();
	case 0x1F:  // previous note + b1
		set_period(n, period_of(v.prev_note + s8(uint32_t(b1)), v.finetune + s16(w23)));
		return b_.setprevnote_ends ? note_ends() : false;
	case 0x0A:  // reset effects
		reset_effects(v);
		return false;
	case 0x0B:  // portamento from the current period toward the destination
		if (b_.interleaved) {
			// Rock'n'Roll: the slide starts from the last note's period (the previous slide's
			// destination), even if that slide hadn't got there.
			v.base = v.period = v.dest_period;
		}
		v.porta_speed = std::max(b1, 1);
		v.porta_rate = int(w23);
		v.porta_counter = 0;
		v.porta = true;
		return false;
	case 0x0C:  // vibrato
		// Hexuma's builds: it never reaches the hardware voices, but the mixer's voices have it.
		if (b_.no_macro_vibrato && !(seven_ && n >= 4))
			return false;
		v.vib_speed = b_.vibrato_even ? (b1 & 0xFE) : b1;
		v.vib_delta = s8(uint32_t(b3));
		v.vib_counter = v.vib_speed >> 1;
		v.vib_offset = 0;
		if (b_.volume_late && v.base) {
			// Older builds restart from the note's period and write it at once.
			v.period = v.base;
			write(n, PaulaWrite::PER, uint32_t(v.period));
		} else if (v.base && !v.porta) {
			// Pro restarts from the note's period too, so 0C 00 (vibrato off) drops the offset
			// (Turrican 3 level 1 #4); it reaches Paula with this tick's period write.
			v.period = v.base;
		}
		return false;
	case 0x0D:  // volume = velocity * 3 + b3, not capped at 64 but kept to a byte
		v.volume = (v.velocity * 3 + b3) & 0xFF;
		return false;
	case 0x0E:  // set volume
		v.volume = b3;
		return false;
	case 0x0F:  // envelope: step b1 every b2 + 1 ticks toward b3
		v.env_step = b1;
		v.env_time = b2;
		v.env_counter = b2;
		v.env_target = b3;
		v.envelope = true;
		return false;
	case 0x11:  // add to sample begin, once or as a sweep
		v.begin = (v.begin + uint32_t(s16(w23))) & 0xFFFFFF;
		write(n, PaulaWrite::LC, v.begin);
		if (b1 && b_.addbegin_sweep) {
			v.ab_count = v.ab_counter = b1;
			v.ab_delta = s16(w23);
			v.addbegin = true;
		}
		return false;
	case 0x12:  // add to length
		set_len(n, uint16_t(v.len + s16(w23)));
		return false;
	case 0x13:  // DMA off, keeping effects
		return dma_off(false);
	case 0x14:  // wait for key up, at most b3 ticks (0 = no limit); no wait if it's already up
		// The timeout counts on the loop counter (see 05), and the command runs again each tick.
		if (!v.key)
			return false;
		if (v.loop == 0) {
			v.loop = -1;
			return false;
		}
		if (--v.loop < 0)
			v.loop = b3 - 1;
		v.step--;
		return true;
	case 0x17:  // set period
		set_period(n, int(w23));
		return b_.setperiod_ends;
	case 0x18:  // sample loop: begin += b2b3, length -= b2b3 / 2
		set_begin(n, v.begin + (w23 & ~1u));
		set_len(n, uint16_t(v.len - (w23 >> 1)));
		return false;
	case 0x19:  // one-shot: the null sample at the start of smpl; it also stops an AddBegin sweep
		set_begin(n, 0);
		set_len(n, 1);
		v.addbegin = false;
		return false;
	case 0x1A: {  // wait for b2b3 + 1 Paula block starts (audio interrupts)
		const uint32_t starts = paula_->channel(n).block_starts;
		if (!b_.dma_wait_from_command && starts != v.dma_wait_mark) {
			// Older builds: an interrupt still pending (since DMA start or an unconsumed block
			// end) satisfies the wait at once, whatever the count; the tick still ends.
			v.dma_wait_mark = starts;
			return note_ends();
		}
		// Pro: an interrupt already pending (DMA start in an earlier tick, say) is served as soon
		// as the command enables it, and counts as the first of the b2b3 + 1.
		v.dma_wait = int(w23) - (starts != v.dma_wait_mark ? 1 : 0);
		v.dma_wait_mark = starts;
		return note_ends();
	}
	case 0x1C:  // split by note
		if (b_.split_jumps && v.note > b1)
			v.step = w23;
		return false;
	case 0x1D:  // split by volume
		if (b_.split_jumps && v.volume > b1)
			v.step = w23;
		return false;
	case 0x21:  // play macro b1 on voice b2 with the current note
		if (b_.play_macro) {
			const int other = b2 & (seven_ ? 7 : 3);
			const int note = v.note, vel = v.velocity;
			note_on(other, note, b1, vel, s8(uint32_t(b3)));
		}
		return false;
	// SID (§12)
	case 0x22:
		v.sid_source = l123;
		return false;
	case 0x23:
		v.sid_length = b1 ? b1 : 256;
		v.sid_mask = w23;
		return false;
	case 0x24:
		v.sid_step = w23 << 8;
		return false;
	case 0x25:
		v.sid_step_sweep.set(b1, s16(w23));
		return false;
	case 0x26:
		v.sid_accel = l123;
		return false;
	case 0x27:
		v.sid_accel_sweep.set(b1, s16(w23));
		return false;
	case 0x28:
		v.sid_filter = true;
		v.sid_limit = s8(uint32_t(b3)) * 16;
		v.sid_limit_sweep.set(b1, s8(uint32_t(b2)));
		return false;
	case 0x29:
		v.sid = b1 && b_.sid;
		if (v.sid) {
			v.sid_filter = false;
			v.sid_step_sweep = Sweep();
			v.sid_accel_sweep = Sweep();
			v.sid_limit_sweep = Sweep();
		}
		return false;
	default:  // 1B, 1E, 20, 30, 31 and unknown commands: no audible effect known
		return false;
	}
}

void Replayer::effects(int n) {
	Voice &v = voice_[n];
	if (v.addbegin) {
		v.begin = (v.begin + uint32_t(v.ab_delta)) & 0xFFFFFF;
		write(n, PaulaWrite::LC, v.begin);
		if (--v.ab_counter <= 0) {
			v.ab_counter = v.ab_count;
			v.ab_delta = -v.ab_delta;
		}
	}
	// Vibrato keeps running during a portamento, but only writes the period when there's none.
	if (v.vib_speed) {
		v.vib_offset += v.vib_delta;
		// The counter starts at speed / 2 and turns the vibrato when it reaches exactly 0, so
		// speed 1 (counter 0) never turns: the period keeps moving one way.
		if (--v.vib_counter == 0) {
			v.vib_counter = v.vib_speed;
			v.vib_delta = -v.vib_delta;
		}
	}
	if (v.vib_speed && !v.porta) {
		const int base = v.base;
		if (b_.scaled_effects) {
			const int scaled = base * v.vib_offset;
			v.period = base + (scaled >= 0 ? scaled / 2048 : -((-scaled + 2047) / 2048));
		} else {
			v.period = base + v.vib_offset;
		}
	}
	if (v.porta) {
		if (v.porta_counter > 0) {
			v.porta_counter--;
		} else {
			v.porta_counter = v.porta_speed - 1;
			// The slide moves the note's base period, from where it was before any vibrato.
			int &p = v.base;
			// Scaled builds multiply in 16 bits and keep 11 bits of the result, but check for
			// arrival on the whole product. So a rate above 256 wraps into odd periods (Turrican 2
			// aliens #9 uses $3776) and can pass the destination without stopping there for a tick
			// (Turrican 3 level 2 #7: 428 -> 376 -> 401).
			uint32_t full = 0;
			auto scale = [&](int factor) {
				full = uint32_t(p) * (uint32_t(factor) & 0xFFFF) >> 8;
				return int(full & 0x7FF);
			};
			if (p > v.dest_period) {
				p = b_.scaled_effects ? scale(256 - v.porta_rate) : p - v.porta_rate;
				if (b_.scaled_effects ? full <= uint32_t(v.dest_period) : p <= v.dest_period) {
					p = v.dest_period;
					v.porta = false;
				}
			} else if (p < v.dest_period) {
				p = b_.scaled_effects ? scale(256 + v.porta_rate) : p + v.porta_rate;
				if (b_.scaled_effects ? full >= uint32_t(v.dest_period) : p >= v.dest_period) {
					p = v.dest_period;
					v.porta = false;
				}
			} else {
				v.porta = false;
			}
			v.period = p;
		}
	}
	if (v.envelope && v.env_time) {
		// Time 0 never changed the volume in traces (300 ticks), so it's treated as off.
		if (v.env_counter > 0) {
			v.env_counter--;
		} else {
			v.env_counter = v.env_time;
			if (v.volume < v.env_target) {
				v.volume += v.env_step;
				if (v.volume >= v.env_target) {
					v.volume = v.env_target;
					v.envelope = false;
				}
			} else if (v.volume > v.env_target) {
				v.volume -= v.env_step;
				if (v.volume <= v.env_target) {
					v.volume = v.env_target;
					v.envelope = false;
				}
			} else {
				v.envelope = false;
			}
		}
	}
}

void Replayer::fade_step() {
	// One step per voice per tick at speed 1 (§7.5).
	if (!fading_)
		return;
	if (--fade_counter_ <= 0) {
		fade_counter_ = fade_speed_;
		master_ += master_ < fade_target_ ? 1 : master_ > fade_target_ ? -1 : 0;
	}
	if (master_ == fade_target_)
		fading_ = false;
}

void Replayer::run_sid(int n) {
	// §12: rebuild the voice's buffer from the source window, then advance the sweeps.
	Voice &v = voice_[n];
	std::vector<uint8_t> &s = m_->smpl;
	const uint32_t out = 4 + 0x100u * unsigned(n);
	uint32_t step = v.sid_step + uint32_t(v.sid_step_sweep.offset);
	const uint32_t accel = v.sid_accel + uint32_t(v.sid_accel_sweep.offset);
	uint32_t phase = 0;
	int y = 0;
	const int limit = int8_t(uint8_t((v.sid_limit + v.sid_limit_sweep.offset) >> 4));
	for (int i = 0; i < v.sid_length; i++) {
		step += accel;
		phase += step;
		const uint32_t src = v.sid_source + ((phase >> 16) & v.sid_mask);
		int x = src < s.size() ? int8_t(s[src]) : 0;
		if (v.sid_filter) {
			if (x > y) {
				const int next = y + limit;
				y = (next >= x || next > 127 || next < -128) ? x : next;
			} else if (x < y) {
				const int next = y - limit;
				y = (next <= x || next > 127 || next < -128) ? x : next;
			}
			x = y;
		}
		if (out + unsigned(i) < s.size())
			s[out + unsigned(i)] = uint8_t(x);
	}
	v.sid_step_sweep.run();
	v.sid_accel_sweep.run();
	v.sid_limit_sweep.run();
}

}  // namespace tfmx
