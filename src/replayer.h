// TFMXreplay: the sequencer and macro engine (docs/TFMX-format.md, sections 3-8 and 15).
#pragma once

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "module.h"
#include "paula.h"
#include "variant.h"

namespace tfmx {

// One write to Paula, as the original replayer would make it (for tests against traces).
struct PaulaWrite {
	enum Reg { LC, LEN, PER, VOL, DMA_OFF, DMA_ON, NOTE };  // NOTE: a note started (log only)
	int voice;
	Reg reg;
	uint32_t value;
};

class Replayer {
public:
	void init(Module *module, const Behaviour &behaviour, Paula *paula);
	void start(const Subsong &subsong);
	void tick();

	double tick_seconds() const;  // length of the next tick
	int voices() const { return seven_ ? 7 : 4; }
	int mix_words() const;
	int mix_period() const;
	// 0 = the setting the variant table gives the song.
	void set_mix_setting(int setting) { mix_setting_ = setting ? setting : b_.mix_setting; }
	bool ended() const { return ended_; }
	int loops() const { return loops_; }  // times the song has reached its end and looped
	int line() const { return line_; }    // current trackstep line
	double time() const { return time_; }  // seconds played by the ticks so far
	// The first loop: the time the song came back to, and when it did. -1 until it has looped; the
	// start stays -1 if the song jumped to a line it hadn't played.
	double loop_start() const { return loop_start_; }
	double loop_end() const { return loop_end_; }

	std::vector<PaulaWrite> *log = nullptr;  // if set, every Paula write is appended

private:
	struct Track {
		bool active = false;
		uint32_t pattern = 0;   // file offset of the pattern
		uint32_t step = 0;
		int wait = 0;
		int loop = -1;          // F1 counter, -1 = not looping
		int transpose = 0;
		uint32_t return_pattern = 0, return_step = 0;
	};

	struct Sweep {  // a triangle sweep: value += delta every tick, reversing every 'count' ticks
		int32_t offset = 0, delta = 0;
		int count = 0, counter = 0;
		void set(int c, int32_t d) { count = counter = c; delta = d; offset = 0; }
		void run() {
			offset += delta;
			if (count && --counter <= 0) {
				counter = count;
				delta = -delta;
			}
		}
	};

	struct Voice {
		// Macro
		bool macro_on = false;
		uint32_t macro = 0, step = 0;
		int wait = 0;
		int loop = -1;          // 05/10 counter, also 14's timeout; -1 = idle
		uint32_t return_macro = 0, return_step = 0;
		bool has_return = false;
		int start_delay = 0;      // ticks before a new note's macro runs
		bool queued = false;      // a note waiting for the voice's next update (Rock'n'Roll)
		int queued_note = 0, queued_macro = 0, queued_velocity = 0, queued_finetune = 0;
		uint32_t queued_tick = 0;
		bool skip_update = false; // a note arrived before this tick's update (Rock'n'Roll)
		bool queued_stop = false;  // a trackstep FE stop waiting for the next update (Rock'n'Roll)
		bool queued_porta = false; // a porta note waiting for the next update (Rock'n'Roll)
		int queued_porta_note = 0, queued_porta_speed = 0, queued_porta_rate = 0;
		int written_volume = -1;  // the volume behind the last VOL write (Behaviour::volume_on_change)
		int dma_wait = -1;        // 1A: block starts still to come, -1 = not waiting
		uint32_t dma_wait_mark = 0;
		// Note
		int note = 0, prev_note = 0, velocity = 0, finetune = 0;
		bool key = false;
		// DMA
		bool cut_next_tick = false;  // 00/13 with b1 != 0 (deferred)
		bool sweep_stops_at_cut = false;  // the deferred cut is a 00's: it stops the AddBegin sweep
		bool note_free = false;      // the next note command doesn't end the tick
		bool dma_on_request = false;
		// Sample
		uint32_t begin = 0;
		uint16_t len = 1;
		// Pitch and volume. 'base' is the note's period, which portamento moves toward 'dest';
		// 'period' is what Paula gets (base plus vibrato).
		int period = 0, base = 0, dest_period = 0;
		int volume = 0;
		// Effects
		bool effects = true;
		bool effects_next_tick = false;
		bool skip_effects = false;  // a note's macro starts this update (note_skips_effects)
		int vib_speed = 0, vib_delta = 0, vib_counter = 0, vib_offset = 0;
		bool porta = false;
		int porta_speed = 1, porta_rate = 0, porta_counter = 0;		bool envelope = false;
		int env_step = 0, env_time = 0, env_counter = 0, env_target = 0;
		bool addbegin = false;
		int ab_count = 0, ab_counter = 0;
		int32_t ab_delta = 0;
		int lock = 0;
		// SID
		bool sid = false;
		uint32_t sid_source = 0;
		int sid_length = 0;
		uint32_t sid_mask = 0;
		uint32_t sid_step = 0, sid_accel = 0;
		Sweep sid_step_sweep, sid_accel_sweep, sid_limit_sweep;
		bool sid_filter = false;
		int32_t sid_limit = 0;  // in 1/16
	};

	// Trackstep and patterns
	void load_line();
	void advance_line();
	void mark_loop(double target);
	void process_rows();
	bool run_track(int t);
	void note_on(int voice, int note, int macro, int velocity, int finetune);
	void start_note(int n, int note, int macro, int velocity, int finetune, int delay);
	void start_porta(int n, int note, int speed, int rate);
	bool after_update(int n) const;
	// Macros and effects
	void run_voice(int n);
	void run_macro(int n);
	bool exec(int n, const uint8_t *c);  // returns true if the command ends the tick
	void effects(int n);
	void fade_step();
	void run_sid(int n);
	void reset_effects(Voice &v);
	void set_period(int n, int period);
	void start_fade(int speed, int target);
	int period_of(int note, int finetune) const;
	void write(int voice, PaulaWrite::Reg reg, uint32_t value);
	void set_begin(int n, uint32_t begin);
	void set_len(int n, uint16_t len);

	Module *m_ = nullptr;
	Paula *paula_ = nullptr;
	Behaviour b_{};
	Subsong sub_{};
	Track tracks_[8];
	Voice voice_[8];

	int line_ = 0;
	int trackstep_loop_ = -1;
	int speed_ = 5;
	int row_counter_ = 0;
	int bpm_ = 0;               // 0 = the default 50 Hz tick
	bool seven_ = false;
	int seven_adjust_ = 0;
	int mix_setting_ = 18;
	int master_ = 64;
	uint32_t tick_count_ = 0;
	int current_track_ = 0;     // the track being processed (for the test log)
	int fade_target_ = 64, fade_speed_ = 0, fade_counter_ = 0;
	bool fading_ = false;
	bool ended_ = false;
	int loops_ = 0;
	int restarts_ = 0;          // F0 reloads within one tick (guards against empty loops)
	int idle_ticks_ = 0;        // ticks since every track stopped
	std::unordered_map<uint64_t, double> seen_;  // sequencer states since the last loop, with their times
	std::vector<double> line_time_;  // when each trackstep line was first reached, -1 = not yet
	double time_ = 0;                // when the current tick starts
	double loop_start_ = -1, loop_end_ = -1;
};

}  // namespace tfmx
