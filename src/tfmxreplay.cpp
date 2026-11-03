// TFMXreplay: the public C API.
#define TFMX_BUILDING
#include "tfmxreplay.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <vector>

#include "module.h"
#include "paula.h"
#include "replayer.h"
#include "variant.h"

using namespace tfmx;

struct tfmx_module {
	Module module;
	tfmx_settings settings;
	tfmx_variant variant = TFMX_VARIANT_PRO;
	Paula paula;
	Replayer replayer;
	int subsong = 0;
	double tick_left = 0;      // output frames still to render before the next tick
	uint64_t frames_played = 0;
	bool finished = false;
	std::vector<double> durations;  // per subsong, < -1.5 = not computed yet
	std::vector<float> mix;
};

namespace {

const double kMaxDuration = 30 * 60;

bool valid(const tfmx_settings &s) {
	return s.sample_rate >= 8000 && s.sample_rate <= 192000 && s.stereo_separation >= 0 &&
	       s.stereo_separation <= 100 && s.interpolation >= TFMX_INTERPOLATION_NONE &&
	       s.interpolation <= TFMX_INTERPOLATION_CUBIC && s.volume_ramping >= 0 && s.loop_count >= -1 &&
	       s.variant >= TFMX_VARIANT_AUTO && s.variant <= TFMX_VARIANT_HEXUMA && s.gain >= 0;
}

void apply_output(tfmx_module *m) {
	const tfmx_settings &s = m->settings;
	m->paula.set_output(s.sample_rate, s.interpolation, s.volume_ramping, s.stereo_separation, s.gain);
}

void restart(tfmx_module *m) {
	m->variant = m->settings.variant == TFMX_VARIANT_AUTO ? detect_variant(m->module) : m->settings.variant;
	m->paula.set_memory(&m->module.smpl);
	apply_output(m);
	m->replayer.init(&m->module, behaviour_of(m->variant, m->module), &m->paula);
	m->replayer.start(m->module.subsongs[size_t(m->subsong)]);
	m->tick_left = 0;
	m->frames_played = 0;
	m->finished = false;
}

// The song is over once it has ended, or looped more often than asked for. A song whose end
// can't be found stops after kMaxDuration, unless it was asked to play forever.
bool song_over(const tfmx_module *m) {
	const Replayer &r = m->replayer;
	if (r.ended())
		return true;
	if (m->settings.loop_count < 0)
		return false;
	return r.loops() > m->settings.loop_count ||
	       double(m->frames_played) / m->settings.sample_rate >= kMaxDuration;
}

tfmx_module *open(const uint8_t *mdat, size_t mdat_size, const uint8_t *smpl, size_t smpl_size,
                  const tfmx_settings *settings, tfmx_error *error) {
	auto fail = [&](tfmx_error e) -> tfmx_module * {
		if (error)
			*error = e;
		return nullptr;
	};
	tfmx_settings s;
	tfmx_default_settings(&s);
	if (settings)
		s = *settings;
	if (!mdat || (!smpl && smpl_size) || !valid(s))
		return fail(TFMX_ERROR_ARGUMENT);
	tfmx_module *m = new (std::nothrow) tfmx_module;
	if (!m)
		return fail(TFMX_ERROR_MEMORY);
	try {
		if (!m->module.load(mdat, mdat_size, smpl, smpl_size)) {
			delete m;
			return fail(TFMX_ERROR_FORMAT);
		}
		m->settings = s;
		m->durations.assign(m->module.subsongs.size(), -2.0);
		restart(m);
	} catch (const std::bad_alloc &) {
		delete m;
		return fail(TFMX_ERROR_MEMORY);
	}
	if (error)
		*error = TFMX_OK;
	return m;
}

template <typename Out>
size_t render(tfmx_module *m, size_t frames, Out out) {
	if (!m)
		return 0;
	size_t done = 0;
	const double rate = m->settings.sample_rate;
	while (done < frames) {
		if (m->tick_left < 1) {
			if (m->finished || song_over(m)) {
				m->finished = true;
				break;
			}
			m->replayer.tick();
			m->tick_left += m->replayer.tick_seconds() * rate;
			continue;
		}
		const size_t n = std::min(frames - done, size_t(m->tick_left));
		m->mix.assign(2 * n, 0.0f);
		m->paula.render(m->mix.data(), int(n));
		out(done, m->mix.data(), n);
		m->tick_left -= double(n);
		done += n;
	}
	m->frames_played += done;
	return done;
}

}  // namespace

extern "C" {

const char *tfmx_version(void) { return "0.1.0"; }

const char *tfmx_error_string(tfmx_error error) {
	switch (error) {
	case TFMX_OK: return "no error";
	case TFMX_ERROR_ARGUMENT: return "invalid argument";
	case TFMX_ERROR_FORMAT: return "not a TFMX song, or damaged";
	case TFMX_ERROR_MEMORY: return "out of memory";
	}
	return "unknown error";
}

void tfmx_default_settings(tfmx_settings *s) {
	if (!s)
		return;
	s->sample_rate = 44100;
	s->stereo_separation = 100;
	s->interpolation = TFMX_INTERPOLATION_CUBIC;
	s->volume_ramping = 2000;
	s->loop_count = 0;
	s->variant = TFMX_VARIANT_AUTO;
	s->gain = 1.0f;
}

int tfmx_probe(const void *data, size_t size) {
	const uint8_t *d = static_cast<const uint8_t *>(data);
	if (!d)
		return 0;
	if (size >= 4 && !memcmp(d, "TFHD", 4))
		return 1;
	return probe_mdat(d, size) ? 1 : 0;
}

tfmx_module *tfmx_open(const void *mdat, size_t mdat_size, const void *smpl, size_t smpl_size,
                       const tfmx_settings *settings, tfmx_error *error) {
	return open(static_cast<const uint8_t *>(mdat), mdat_size, static_cast<const uint8_t *>(smpl), smpl_size,
	            settings, error);
}

tfmx_module *tfmx_open_single(const void *data, size_t size, const tfmx_settings *settings, tfmx_error *error) {
	const uint8_t *mdat, *smpl;
	size_t mdat_size, smpl_size;
	if (!split_tfhd(static_cast<const uint8_t *>(data), size, &mdat, &mdat_size, &smpl, &smpl_size)) {
		if (error)
			*error = data ? TFMX_ERROR_FORMAT : TFMX_ERROR_ARGUMENT;
		return nullptr;
	}
	return open(mdat, mdat_size, smpl, smpl_size, settings, error);
}

void tfmx_close(tfmx_module *module) { delete module; }

int tfmx_subsong_count(const tfmx_module *m) { return m ? int(m->module.subsongs.size()) : 0; }

int tfmx_subsong_number(const tfmx_module *m, int index) {
	if (!m || index < 0 || index >= tfmx_subsong_count(m))
		return -1;
	return m->module.subsongs[size_t(index)].number;
}

double tfmx_subsong_duration(tfmx_module *m, int index) {
	if (!m || index < 0 || index >= tfmx_subsong_count(m))
		return -1;
	double &cached = m->durations[size_t(index)];
	if (cached > -1.5)
		return cached;
	// A fast run on a separate replayer, without mixing.
	Paula paula;
	paula.set_memory(&m->module.smpl);
	Replayer r;
	const tfmx_variant variant = m->settings.variant == TFMX_VARIANT_AUTO ? detect_variant(m->module) : m->settings.variant;
	r.init(&m->module, behaviour_of(variant, m->module), &paula);
	r.start(m->module.subsongs[size_t(index)]);
	const int loops = std::max(m->settings.loop_count, 0);
	double t = 0;
	while (t < kMaxDuration && !r.ended() && r.loops() <= loops) {
		r.tick();
		const double dt = r.tick_seconds();
		paula.advance(dt);
		t += dt;
	}
	cached = t < kMaxDuration ? t : -1;
	return cached;
}

const char *tfmx_text(const tfmx_module *m) { return m ? m->module.text.c_str() : ""; }

tfmx_variant tfmx_get_variant(const tfmx_module *m) { return m ? m->variant : TFMX_VARIANT_AUTO; }

const char *tfmx_variant_name(tfmx_variant v) {
	switch (v) {
	case TFMX_VARIANT_AUTO: return "auto";
	case TFMX_VARIANT_PRO: return "TFMX Pro";
	case TFMX_VARIANT_15: return "TFMX 1.5";
	case TFMX_VARIANT_MASTERBLAZER: return "TFMX (Masterblazer)";
	case TFMX_VARIANT_XOUT: return "TFMX (X-Out)";
	case TFMX_VARIANT_ROCKNROLL: return "TFMX (Rock'n'Roll)";
	case TFMX_VARIANT_GEMX: return "TFMX (GemX)";
	case TFMX_VARIANT_TURRICAN2: return "TFMX Pro (Turrican 2)";
	case TFMX_VARIANT_TURRICAN3_SMALL: return "TFMX Pro (Turrican 3 small build)";
	case TFMX_VARIANT_HEXUMA: return "TFMX Pro (Hexuma)";
	}
	return "unknown";
}

int tfmx_voice_count(const tfmx_module *m) { return m ? m->replayer.voices() : 0; }

int tfmx_select_subsong(tfmx_module *m, int index) {
	if (!m || index < 0 || index >= tfmx_subsong_count(m))
		return -1;
	m->subsong = index;
	restart(m);
	return 0;
}

size_t tfmx_render_float(tfmx_module *m, float *buffer, size_t frames) {
	if (!buffer)
		return 0;
	return render(m, frames, [&](size_t at, const float *mix, size_t n) {
		memcpy(buffer + 2 * at, mix, 2 * n * sizeof(float));
	});
}

size_t tfmx_render_s16(tfmx_module *m, int16_t *buffer, size_t frames) {
	if (!buffer)
		return 0;
	return render(m, frames, [&](size_t at, const float *mix, size_t n) {
		for (size_t i = 0; i < 2 * n; i++) {
			const float v = std::clamp(mix[i] * 32768.0f, -32768.0f, 32767.0f);
			buffer[2 * at + i] = int16_t(std::lrint(v));
		}
	});
}

double tfmx_position(const tfmx_module *m) {
	return m ? double(m->frames_played) / m->settings.sample_rate : 0;
}

double tfmx_seek(tfmx_module *m, double seconds) {
	if (!m)
		return 0;
	restart(m);
	// Run the replayer without mixing up to the target, part way into its tick.
	const double rate = m->settings.sample_rate;
	double t = 0;
	while (seconds > 0 && !song_over(m)) {
		m->replayer.tick();
		const double dt = m->replayer.tick_seconds();
		if (t + dt > seconds) {
			m->paula.advance(seconds - t);
			m->tick_left = (t + dt - seconds) * rate;
			t = seconds;
			break;
		}
		m->paula.advance(dt);
		t += dt;
	}
	m->finished = song_over(m) && m->tick_left < 1;
	m->frames_played = uint64_t(t * rate);
	return t;
}

int tfmx_set_settings(tfmx_module *m, const tfmx_settings *s) {
	if (!m || !s || !valid(*s))
		return -1;
	const bool keep = s->sample_rate == m->settings.sample_rate && s->variant == m->settings.variant;
	m->settings = *s;
	if (keep) {
		apply_output(m);
	} else {
		std::fill(m->durations.begin(), m->durations.end(), -2.0);
		restart(m);
	}
	return 0;
}

}  // extern "C"
