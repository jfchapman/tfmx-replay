// TFMXreplay: the Amiga's sound chip, Paula, as far as TFMX uses it (docs/TFMX-format.md, 5.4).
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

	// Anti-click ramp: the volume actually applied, moving toward vol.
	float ramp_vol = 0;
};

class Paula {
public:
	void reset(int channels);
	void set_memory(const std::vector<uint8_t> *smpl) { smpl_ = smpl; }
	void set_output(int sample_rate, tfmx_interpolation interpolation, int ramp_us, int separation, float gain);

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

	// Voices 4-7 (7V mode) are mixed into hardware voice 3's side at this scale.
	void set_7v(bool on) { seven_ = on; }

private:
	float sample_at(const Channel &c, uint32_t index) const;
	void next_block(Channel &c);
	void step(Channel &c, double samples);
	float voice_sample(Channel &c);

	std::vector<Channel> ch_;
	struct Tail {
		Channel c;
		bool active = false;
	};
	std::vector<Tail> tails_;  // cut voices fading out (anti-click)
	const std::vector<uint8_t> *smpl_ = nullptr;
	int rate_ = 44100;
	tfmx_interpolation interp_ = TFMX_INTERPOLATION_CUBIC;
	float ramp_step_ = 0;  // volume units per output frame, 0 = no ramping
	float sep_ = 1.0f;
	float gain_ = 1.0f;
	bool seven_ = false;
};

}  // namespace tfmx
