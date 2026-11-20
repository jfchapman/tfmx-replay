// Entry points for the WebAssembly build, for callers that work in plain numbers: JavaScript can't fill in a
// tfmx_settings without knowing how the compiler laid it out. The rest of the API is exported as it is.
#include "tfmxreplay.h"

static tfmx_settings make_settings(int sample_rate, int stereo_separation, int interpolation, int volume_ramping, int loop_count,
                                   int amiga_model, int led_filter) {
	tfmx_settings settings;
	tfmx_default_settings(&settings);
	settings.sample_rate = sample_rate;
	settings.stereo_separation = stereo_separation;
	settings.interpolation = static_cast<tfmx_interpolation>(interpolation);
	settings.volume_ramping = volume_ramping;
	settings.loop_count = loop_count;
	settings.amiga_model = static_cast<tfmx_amiga_model>(amiga_model);
	settings.led_filter = led_filter;
	return settings;
}

extern "C" {

// Opens a single-file (TFHD) song at this rate with the default settings, but without the fixed Amiga output filter
// (as libopenmpt plays without its Amiga emulation). Returns null if it can't.
tfmx_module *tfmx_wasm_open(const void *data, size_t size, int sample_rate) {
	const tfmx_settings settings = make_settings(sample_rate, 100, TFMX_INTERPOLATION_NONE, 2000, 0, TFMX_AMIGA_NONE, 0);
	return tfmx_open_single(data, size, &settings, nullptr);
}

// Changes the settings of an open song (amiga_model is a tfmx_amiga_model; led_filter 1 = on), which take effect at
// once. Returns 0 on success.
int tfmx_wasm_configure(tfmx_module *module, int sample_rate, int stereo_separation, int interpolation, int volume_ramping, int loop_count,
                        int amiga_model, int led_filter) {
	const tfmx_settings settings = make_settings(sample_rate, stereo_separation, interpolation, volume_ramping, loop_count, amiga_model, led_filter);
	return tfmx_set_settings(module, &settings);
}

}
