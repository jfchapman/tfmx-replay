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

namespace {

// What a run of one play-through found out about a subsong.
struct Analysis {
	bool done = false;
	double duration = -1;    // -1 = no end found
	double loop_start = -1;  // -1 = doesn't loop
	double loop_length = 0;
	bool audible = false;
	bool looped = false;     // loops, and doesn't end in silence
	int repeat_of = -1;
};

}  // namespace

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
	bool looped = false;       // the current subsong plays on past its loop point
	bool fading = false;
	double fade_left = 0, fade_total = 0;  // in output frames
	std::vector<Analysis> analysis;        // per subsong
	bool repeats_found = false;
	std::vector<float> mix;
};

namespace {

const double kMaxDuration = 30 * 60;

// The analysis listens to a play-through at a low rate, in windows of a quarter of a second.
const int kAnalysisRate = 11025;
const int kAnalysisWindow = kAnalysisRate / 4;
const double kSilentPower = 1e-6;      // -60 dB: a subsong quieter than this overall has no music
const double kActivePower = 1e-8;      // -80 dB: windows quieter than this don't count as playing
const double kLoopedEndPower = 1e-3;   // -30 dB: how far under the song's usual level the end may be
const double kDefaultMargin = 0.9;     // an earlier subsong this close to the longest is preferred

bool valid(const tfmx_settings &s) {
	return s.sample_rate >= 8000 && s.sample_rate <= 192000 && s.stereo_separation >= 0 &&
	       s.stereo_separation <= 100 && s.interpolation >= TFMX_INTERPOLATION_NONE &&
	       s.interpolation <= TFMX_INTERPOLATION_CUBIC && s.volume_ramping >= 0 && s.loop_count >= -1 &&
	       s.variant >= TFMX_VARIANT_AUTO && s.variant <= TFMX_VARIANT_DANGERFREAK && s.gain >= 0 &&
	       s.amiga_model >= TFMX_AMIGA_A500 && s.amiga_model <= TFMX_AMIGA_NONE && s.fade_seconds >= 0 &&
	       (s.mix_setting == 0 || (s.mix_setting >= 8 && s.mix_setting <= 22));
}

void apply_output(tfmx_module *m) {
	const tfmx_settings &s = m->settings;
	m->paula.set_output(s.sample_rate, s.interpolation, s.volume_ramping, s.stereo_separation, s.gain, s.amiga_model,
	                    s.led_filter != 0);
}

tfmx_variant variant_of(const tfmx_module *m) {
	return m->settings.variant == TFMX_VARIANT_AUTO ? detect_variant(m->module) : m->settings.variant;
}

// Plays one pass of a subsong on its own replayer, mixing it at a low rate to hear how it ends:
// the sequencer can loop a song that has finished, with nothing but silence before the loop.
const Analysis &analyse(tfmx_module *m, int index) {
	Analysis &a = m->analysis[size_t(index)];
	if (a.done)
		return a;
	Paula paula;
	paula.set_memory(&m->module.smpl);
	paula.set_output(kAnalysisRate, TFMX_INTERPOLATION_NONE, 0, 100, 1.0f, TFMX_AMIGA_NONE, false);
	Replayer r;
	r.init(&m->module, behaviour_of(variant_of(m), m->module), &paula);
	r.set_mix_setting(m->settings.mix_setting);
	r.start(m->module.subsongs[size_t(index)]);

	std::vector<float> mix;
	std::vector<double> windows;
	double sum = 0, previous = 0, total = 0, frames_left = 0;
	int filled = 0;
	uint64_t frames = 0;
	while (r.time() < kMaxDuration && !r.ended()) {
		r.tick();
		if (r.loops() > 0)
			break;
		frames_left += r.tick_seconds() * kAnalysisRate;
		const int n = int(frames_left);
		frames_left -= n;
		mix.assign(2 * size_t(n), 0.0f);
		paula.render(mix.data(), n);
		for (int i = 0; i < n; i++) {
			const double power = double(mix[2 * size_t(i)]) * mix[2 * size_t(i)] +
			                     double(mix[2 * size_t(i) + 1]) * mix[2 * size_t(i) + 1];
			sum += power;
			total += power;
			if (++filled == kAnalysisWindow) {
				windows.push_back(sum / (2 * kAnalysisWindow));
				previous = sum;
				sum = 0;
				filled = 0;
			}
		}
		frames += uint64_t(n);
	}

	const bool loops = r.loops() > 0;
	const double end = loops ? r.loop_end() : r.time();
	a.duration = end < kMaxDuration ? end : -1;
	if (loops && r.loop_start() >= 0 && r.loop_end() > r.loop_start()) {
		a.loop_start = r.loop_start();
		a.loop_length = r.loop_end() - r.loop_start();
	}
	a.audible = frames > 0 && total / (2 * double(frames)) > kSilentPower;
	std::vector<double> active;
	for (double w : windows)
		if (w > kActivePower)
			active.push_back(w);
	if (!active.empty() && a.loop_start >= 0) {
		std::nth_element(active.begin(), active.begin() + std::ptrdiff_t(active.size() / 2), active.end());
		const double usual = active[active.size() / 2];
		const double end_power = (previous + sum) / (2.0 * (kAnalysisWindow + filled));
		a.looped = end_power >= usual * kLoopedEndPower;
	}
	a.done = true;
	return a;
}

// Finds the subsongs that replay another's music: the same range of the trackstep, or one that
// overlaps it. The longest of them is the one kept.
void find_repeats(tfmx_module *m) {
	if (m->repeats_found)
		return;
	const int count = int(m->module.subsongs.size());
	std::vector<int> order;
	for (int i = 0; i < count; i++)
		if (analyse(m, i).audible && analyse(m, i).duration >= 0)
			order.push_back(i);
	std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
		return m->analysis[size_t(a)].duration > m->analysis[size_t(b)].duration;
	});
	std::vector<int> kept;
	for (int i : order) {
		const Subsong &s = m->module.subsongs[size_t(i)];
		for (int k : kept) {
			const Subsong &o = m->module.subsongs[size_t(k)];
			if (s.end >= o.start && s.start <= o.end) {
				m->analysis[size_t(i)].repeat_of = k;
				break;
			}
		}
		if (m->analysis[size_t(i)].repeat_of < 0)
			kept.push_back(i);
	}
	m->repeats_found = true;
}

void forget_analysis(tfmx_module *m) {
	m->analysis.assign(m->module.subsongs.size(), Analysis());
	m->repeats_found = false;
}

// Only a song that really loops plays on, so playing on needs the analysis.
void update_looped(tfmx_module *m) {
	m->looped = (m->settings.loop_count != 0 || m->settings.fade_seconds > 0) && analyse(m, m->subsong).looped;
}

void restart(tfmx_module *m) {
	m->variant = variant_of(m);
	m->paula.set_memory(&m->module.smpl);
	apply_output(m);
	m->replayer.init(&m->module, behaviour_of(m->variant, m->module), &m->paula);
	m->replayer.set_mix_setting(m->settings.mix_setting);
	m->replayer.start(m->module.subsongs[size_t(m->subsong)]);
	m->tick_left = 0;
	m->frames_played = 0;
	m->finished = false;
	m->fading = false;
	update_looped(m);
}

// Before a tick: the song has ended. A song whose end can't be found stops after kMaxDuration,
// unless it was asked to play forever.
bool stopped(const tfmx_module *m) {
	return m->replayer.ended() ||
	       (m->settings.loop_count >= 0 && double(m->frames_played) / m->settings.sample_rate >= kMaxDuration);
}

// After a tick: has the song come to the end of the loops asked for? Then it stops at once, as that
// tick is the start of another pass, or begins its fade-out. Returns true if it stops.
bool reached_end(tfmx_module *m) {
	const Replayer &r = m->replayer;
	if (m->fading || r.loops() == 0)
		return false;
	if (m->looped && (m->settings.loop_count < 0 || r.loops() <= m->settings.loop_count))
		return false;
	if (m->looped && m->settings.fade_seconds > 0) {
		m->fading = true;
		m->fade_total = m->fade_left = double(m->settings.fade_seconds) * m->settings.sample_rate;
		return false;
	}
	return true;
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
		forget_analysis(m);
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
		if (m->finished)
			break;
		if (m->tick_left < 1) {
			if (stopped(m)) {
				m->finished = true;
				break;
			}
			m->replayer.tick();
			if (reached_end(m)) {
				m->finished = true;
				break;
			}
			m->tick_left += m->replayer.tick_seconds() * rate;
			continue;
		}
		size_t n = std::min(frames - done, size_t(m->tick_left));
		if (m->fading)
			n = std::min(n, std::max<size_t>(size_t(m->fade_left), 1));
		m->mix.assign(2 * n, 0.0f);
		m->paula.render(m->mix.data(), int(n));
		if (m->fading) {
			for (size_t i = 0; i < n; i++) {
				const float gain = float(std::max(m->fade_left - double(i), 0.0) / m->fade_total);
				m->mix[2 * i] *= gain;
				m->mix[2 * i + 1] *= gain;
			}
			m->fade_left -= double(n);
			if (m->fade_left < 1)
				m->finished = true;
		}
		out(done, m->mix.data(), n);
		m->tick_left -= double(n);
		done += n;
	}
	m->frames_played += done;
	return done;
}

}  // namespace

extern "C" {

const char *tfmx_version(void) { return "0.2.0"; }

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
	s->interpolation = TFMX_INTERPOLATION_NONE;
	s->volume_ramping = 2000;
	s->loop_count = 0;
	s->variant = TFMX_VARIANT_AUTO;
	s->gain = 1.0f;
	s->amiga_model = TFMX_AMIGA_A500;
	s->led_filter = 0;
	s->fade_seconds = 0;
	s->mix_setting = 0;
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
	const Analysis &a = analyse(m, index);
	const tfmx_settings &s = m->settings;
	if (a.duration < 0 || !a.looped)
		return a.duration;
	return a.duration + std::max(s.loop_count, 0) * a.loop_length + (s.loop_count >= 0 ? double(s.fade_seconds) : 0.0);
}

int tfmx_subsong_index(const tfmx_module *m, int number) {
	for (int i = 0; i < tfmx_subsong_count(m); i++)
		if (m->module.subsongs[size_t(i)].number == number)
			return i;
	return -1;
}

int tfmx_get_subsong_info(tfmx_module *m, int index, tfmx_subsong_info *info) {
	if (!m || !info || index < 0 || index >= tfmx_subsong_count(m))
		return -1;
	find_repeats(m);
	const Analysis &a = analyse(m, index);
	info->number = m->module.subsongs[size_t(index)].number;
	info->duration = a.duration;
	info->loop_start = a.loop_start;
	info->loop_length = a.loop_length;
	info->looped = a.looped ? 1 : 0;
	info->audible = a.audible ? 1 : 0;
	info->repeat_of = a.repeat_of;
	return 0;
}

int tfmx_default_subsong(tfmx_module *m) {
	if (!m || m->analysis.empty())
		return -1;
	find_repeats(m);
	const int count = tfmx_subsong_count(m);
	auto main_song = [&](int i) {
		const Analysis &a = m->analysis[size_t(i)];
		return a.audible && a.duration >= 0 && a.repeat_of < 0;
	};
	double longest = -1;
	for (int i = 0; i < count; i++)
		if (main_song(i))
			longest = std::max(longest, m->analysis[size_t(i)].duration);
	for (int i = 0; i < count; i++)
		if (main_song(i) && m->analysis[size_t(i)].duration >= longest * kDefaultMargin)
			return i;
	return 0;
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
	case TFMX_VARIANT_LOGICAL: return "TFMX (Logical)";
	case TFMX_VARIANT_DANGERFREAK: return "TFMX (Danger Freak)";
	}
	return "unknown";
}

int tfmx_voice_count(const tfmx_module *m) { return m ? m->replayer.voices() : 0; }

int tfmx_set_voice_mask(tfmx_module *m, unsigned mask) {
	if (!m)
		return -1;
	m->paula.set_voice_mask(mask & 0xFF);
	return 0;
}

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

double tfmx_song_position(const tfmx_module *m) {
	if (!m)
		return 0;
	const Replayer &r = m->replayer;
	const double t = tfmx_position(m), start = r.loop_start(), end = r.loop_end();
	if (start < 0 || end <= start || t < end)
		return t;
	return start + std::fmod(t - start, end - start);
}

double tfmx_seek(tfmx_module *m, double seconds) {
	if (!m)
		return 0;
	restart(m);
	// Plays up to the target and throws the audio away, so what follows is the same, sample for
	// sample, as playing there from the start.
	uint64_t left = seconds > 0 ? uint64_t(seconds * m->settings.sample_rate) : 0;
	while (left > 0) {
		const size_t done = render(m, size_t(std::min<uint64_t>(left, 4096)), [](size_t, const float *, size_t) {});
		if (!done)
			break;
		left -= done;
	}
	return tfmx_position(m);
}

int tfmx_set_settings(tfmx_module *m, const tfmx_settings *s) {
	if (!m || !s || !valid(*s))
		return -1;
	const bool keep = s->sample_rate == m->settings.sample_rate && s->variant == m->settings.variant;
	m->settings = *s;
	if (keep) {
		apply_output(m);
		update_looped(m);
	} else {
		forget_analysis(m);
		restart(m);
	}
	return 0;
}

}  // extern "C"
