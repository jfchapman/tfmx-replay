// TFMXreplay: the Amiga's sound chip, Paula, as far as TFMX uses it (docs/TFMX-format.md, section 8).
#pragma once

#include <cstdint>
#include <vector>

#include "tfmxreplay.h"

namespace tfmx {

const double kPaulaClock = 3546895.0;  // PAL colour clock, Hz

struct Channel {
	// Registers, as the replayer writes them. lc is a byte offset into smpl.
	uint32_t lc = 0;
	uint16_t len = 1;
	uint16_t per = 428;
	int vol = 0;
	bool dma = false;

	// Playback: the block Paula latched, and the position in it (in samples, i.e. bytes).
	uint32_t block_lc = 0;
	uint32_t block_bytes = 2;
	double pos = 0;
	double delay = 0;           // seconds until a DMA start takes effect (7V: after the mixing)
	uint32_t block_starts = 0;  // counts DMA starts and block ends (the audio interrupt)

	// The last sample of the previous block (for interpolation across a loop point).
	float prev = 0;

	// 7V: voices 4-7 as the software mixer sees them. Its own block, its position in 21.11 fixed
	// point, and what it latched from the registers.
	uint32_t mix_lc = 0;
	uint32_t mix_bytes = 2;
	uint32_t mix_pos = 0;
	uint32_t mix_next_lc = 0;
	uint16_t mix_next_len = 1;
	int mix_per = 428;
	bool mix_on = false, mix_held = false;
	int mix_vol = 0;
	int mix_volume = 0;  // the voice's volume, without the master volume

	// Anti-click ramp: the volume actually applied, moving toward vol.
	float ramp_vol = 0;
};

class Paula {
public:
	void reset(int channels);
	void set_memory(const std::vector<uint8_t> *smpl) { smpl_ = smpl; }
	void set_output(int sample_rate, tfmx_interpolation interpolation, int ramp_us, int separation, float gain,
	                tfmx_amiga_model model, bool led_filter);

	Channel &channel(int n) { return ch_[size_t(n)]; }
	int channels() const { return int(ch_.size()); }

	// 'delay' seconds pass before the sample starts playing (the replayer's DMA write comes that far
	// into the tick).
	void dma_on(int n, double delay = 0);
	void dma_off(int n);

	// Advances every channel by 'seconds' without producing sound (for fast runs).
	void advance(double seconds);

	// Mixes 'frames' stereo frames, added into out (left, right interleaved).
	void render(float *out, int frames);

	// 7V: hardware voice 3 plays a software mix of voices 4-7 instead.
	void set_7v(bool on) { seven_ = on; }

	// 7V: a macro's DMA on: a new note reaches the mixer as it is at this point in the macro.
	void mix_latch(int n, int period, int volume);

	// 7V, once a tick: the block mixed last tick starts playing on hardware voice 3, and the next one
	// is mixed from the voices as they are now: 'mixed' bytes at Paula period 'period', of which the
	// first 'bytes' are played, then 'silence' bytes of nothing.
	void mix_block(int bytes, int period, int mixed, int silence = 0);

	// The block just mixed (for tests).
	const std::vector<int8_t> &mix_next() const { return mix_back_; }

private:
	float sample_at(const Channel &c, uint32_t index) const;
	void next_block(Channel &c);
	void step(Channel &c, double samples);
	float voice_sample(Channel &c);
	float filter(int side, float x);
	float mix_sample(double pos) const;

	std::vector<Channel> ch_;
	struct Tail {
		Channel c;
		bool active = false;
	};
	std::vector<Tail> tails_;  // cut voices fading out (anti-click)
	const std::vector<uint8_t> *smpl_ = nullptr;
	int rate_ = 44100;
	tfmx_interpolation interp_ = TFMX_INTERPOLATION_NONE;
	float ramp_step_ = 0;  // volume units per output frame, 0 = no ramping
	float sep_ = 1.0f;
	float gain_ = 1.0f;
	bool seven_ = false;

	// The 7V mix: the block before, the block playing, the next, and the position in the middle one.
	std::vector<int8_t> mix_old_, mix_front_, mix_back_;
	double mix_play_ = 0;
	int mix_period_ = 199;

	// The output filters, per side: the fixed one-pole low-pass, then the two-pole LED filter.
	double fixed_coef_ = 0;  // 0 = off
	bool led_ = false;
	double led_b0_ = 0, led_a1_ = 0, led_a2_ = 0;
	struct FilterState {
		double fixed = 0, led1 = 0, led2 = 0;
	};
	FilterState filter_[2];
};

}  // namespace tfmx
