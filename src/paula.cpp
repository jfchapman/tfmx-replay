// TFMXreplay: Paula emulation.
//
// Paula plays 'len' words from 'lc', one byte per 'per' colour clocks. When a block ends it
// reloads whatever lc/len hold at that moment and carries on, so register changes made while a
// voice plays take effect at the next loop point. Starting DMA latches lc/len at once. Both
// events raise the voice's audio interrupt, which the replayer counts for macro 1A.
#include "paula.h"

#include <algorithm>
#include <cmath>

namespace tfmx {

namespace {

const int kMinPeriod = 113;

const double kPi = 3.14159265358979323846;

// Output filter corners in Hz: the commonly quoted measurements of the machines.
const double kA500Corner = 4900;
const double kA1200Corner = 28867;
const double kLedCorner = 3275;
const double kLedQ = 0.7071;  // Butterworth

uint32_t bytes_of(uint16_t len) { return (len ? uint32_t(len) : 0x10000u) * 2; }

}  // namespace

void Paula::reset(int channels) {
	ch_.assign(size_t(channels), Channel());
	tails_.assign(size_t(channels), Tail());
	seven_ = false;
	filter_[0] = filter_[1] = FilterState();
}

void Paula::set_output(int sample_rate, tfmx_interpolation interpolation, int ramp_us, int separation, float gain,
                       tfmx_amiga_model model, bool led_filter) {
	// One pole, matched to the analogue filter.
	const double corner = model == TFMX_AMIGA_A500 ? kA500Corner : model == TFMX_AMIGA_A1200 ? kA1200Corner : 0;
	fixed_coef_ = corner > 0 ? 1 - std::exp(-2 * kPi * corner / sample_rate) : 0;
	// Two poles, by the bilinear transform with the corner prewarped.
	led_ = led_filter;
	const double k = std::tan(kPi * kLedCorner / sample_rate);
	const double norm = 1 / (1 + k / kLedQ + k * k);
	led_b0_ = k * k * norm;
	led_a1_ = 2 * (k * k - 1) * norm;
	led_a2_ = (1 - k / kLedQ + k * k) * norm;

	rate_ = sample_rate;
	interp_ = interpolation;
	// A full-scale volume change (64) takes ramp_us microseconds.
	ramp_step_ = ramp_us > 0 ? float(64.0 * 1e6 / (double(ramp_us) * sample_rate)) : 0.0f;
	sep_ = std::clamp(separation, 0, 100) / 100.0f;
	gain_ = gain;
}

void Paula::dma_on(int n, double delay) {
	Channel &c = ch_[size_t(n)];
	if (c.dma)
		return;
	c.delay = delay;
	if (ramp_step_ > 0 && c.ramp_vol > 0.5f) {
		// The voice is restarting while still audible: let the old sound fade out separately.
		tails_[size_t(n)].c = c;
		tails_[size_t(n)].active = true;
	}
	c.dma = true;
	c.block_lc = c.lc;
	c.block_bytes = bytes_of(c.len);
	c.pos = 0;
	c.prev = 0;
	c.block_starts++;
	c.ramp_vol = float(std::min(c.vol, 64));  // the attack is part of the sound: no ramp in
}

void Paula::dma_off(int n) {
	Channel &c = ch_[size_t(n)];
	if (!c.dma)
		return;
	if (ramp_step_ > 0 && c.ramp_vol > 0.5f) {
		tails_[size_t(n)].c = c;
		tails_[size_t(n)].active = true;
	}
	c.dma = false;
	c.ramp_vol = 0;
}

void Paula::next_block(Channel &c) {
	c.prev = sample_at(c, c.block_bytes - 1);
	c.block_lc = c.lc;
	c.block_bytes = bytes_of(c.len);
	c.block_starts++;
}

float Paula::sample_at(const Channel &c, uint32_t index) const {
	// index counts bytes from the start of the current block; past its end, the next block
	// (whatever lc holds now) follows.
	uint32_t addr;
	if (index < c.block_bytes)
		addr = c.block_lc + index;
	else
		addr = c.lc + (index - c.block_bytes) % bytes_of(c.len);
	if (!smpl_ || addr >= smpl_->size())
		return 0;
	return float(int8_t((*smpl_)[addr])) / 128.0f;
}

void Paula::step(Channel &c, double samples) {
	c.pos += samples;
	while (c.pos >= c.block_bytes) {
		c.pos -= c.block_bytes;
		next_block(c);
	}
}

void Paula::advance(double seconds) {
	for (Channel &c : ch_) {
		if (!c.dma)
			continue;
		double t = seconds;
		if (c.delay > 0) {
			const double d = std::min(c.delay, t);
			c.delay -= d;
			t -= d;
		}
		step(c, t * kPaulaClock / std::max<int>(c.per, kMinPeriod));
	}
	for (Tail &t : tails_)
		t.active = false;
}

float Paula::voice_sample(Channel &c) {
	const uint32_t i = uint32_t(c.pos);
	const float s0 = sample_at(c, i);
	if (interp_ == TFMX_INTERPOLATION_NONE)
		return s0;
	const float t = float(c.pos - i);
	const float s1 = sample_at(c, i + 1);
	if (interp_ == TFMX_INTERPOLATION_LINEAR)
		return s0 + (s1 - s0) * t;
	// Cubic (Catmull-Rom) through the previous sample, s0, s1, s2.
	const float sm = i ? sample_at(c, i - 1) : c.prev, s2 = sample_at(c, i + 2);
	const float a = -0.5f * sm + 1.5f * s0 - 1.5f * s1 + 0.5f * s2;
	const float b = sm - 2.5f * s0 + 2.0f * s1 - 0.5f * s2;
	const float d = -0.5f * sm + 0.5f * s1;
	return ((a * t + b) * t + d) * t + s0;
}

float Paula::filter(int side, float x) {
	FilterState &f = filter_[side];
	double y = x;
	if (fixed_coef_ > 0) {
		f.fixed += fixed_coef_ * (y - f.fixed);
		y = f.fixed;
	}
	if (led_) {
		const double in = y;
		y = led_b0_ * in + f.led1;
		f.led1 = 2 * led_b0_ * in - led_a1_ * y + f.led2;
		f.led2 = led_b0_ * in - led_a2_ * y;
	}
	return float(y);
}

void Paula::render(float *out, int frames) {
	const float base = 0.5f * gain_;  // two full-scale voices per side reach 1.0
	for (int f = 0; f < frames; f++) {
		float side[2] = {0, 0};
		for (size_t n = 0; n < ch_.size(); n++) {
			// Hardware voices 0 and 3 are on the left, 1 and 2 on the right. In 7V mode voices
			// 4-7 replace voice 3, sharing its side.
			const int s = (n == 1 || n == 2) ? 1 : 0;
			const float scale = n >= 4 ? 0.5f : 1.0f;
			for (int pass = 0; pass < 2; pass++) {
				Channel &c = pass ? tails_[n].c : ch_[n];
				if (pass && !tails_[n].active)
					continue;
				if (!pass && !c.dma)
					continue;
				if (!pass && c.delay > 0) {
					c.delay -= 1.0 / rate_;  // DMA hasn't started yet
					continue;
				}
				float target = pass ? 0.0f : float(std::clamp(c.vol, 0, 64));
				if (ramp_step_ > 0) {
					if (c.ramp_vol < target)
						c.ramp_vol = std::min(target, c.ramp_vol + ramp_step_);
					else if (c.ramp_vol > target)
						c.ramp_vol = std::max(target, c.ramp_vol - ramp_step_);
				} else {
					c.ramp_vol = target;
				}
				if (c.ramp_vol > 0)
					side[s] += voice_sample(c) * c.ramp_vol / 64.0f * scale;
				step(c, kPaulaClock / std::max<int>(c.per, kMinPeriod) / rate_);
				if (pass && c.ramp_vol <= 0)
					tails_[n].active = false;
			}
		}
		// The filters are on the two analogue outputs, so they come before the stereo separation.
		const float l = filter(0, side[0] * base), r = filter(1, side[1] * base);
		const float mix = 0.5f + 0.5f * sep_, cross = 0.5f - 0.5f * sep_;
		out[2 * f] += l * mix + r * cross;
		out[2 * f + 1] += r * mix + l * cross;
	}
}

}  // namespace tfmx
