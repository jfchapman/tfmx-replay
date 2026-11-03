/*
 * TFMXreplay: a renderer for Chris Huelsbeck's TFMX music format (Amiga).
 *
 * Copyright (c) 2026 James Chapman. MIT licence: see LICENSE.
 *
 * A plain C API. Typical use:
 *
 *     tfmx_settings settings;
 *     tfmx_default_settings(&settings);
 *     settings.sample_rate = 48000;
 *
 *     tfmx_error error;
 *     tfmx_module *module = tfmx_open(mdat, mdat_size, smpl, smpl_size, &settings, &error);
 *     if (!module) { ... tfmx_error_string(error) ... }
 *
 *     int count = tfmx_subsong_count(module);
 *     tfmx_select_subsong(module, 0);
 *
 *     int16_t buffer[2 * 1024];
 *     size_t frames;
 *     while ((frames = tfmx_render_s16(module, buffer, 1024)) > 0) {
 *         ... write 'frames' stereo frames ...
 *     }
 *     tfmx_close(module);
 *
 * Thread safety: a module may be used from one thread at a time. Different modules are independent.
 */
#ifndef TFMXREPLAY_H
#define TFMXREPLAY_H

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(TFMX_SHARED)
#  if defined(TFMX_BUILDING)
#    define TFMX_API __declspec(dllexport)
#  else
#    define TFMX_API __declspec(dllimport)
#  endif
#elif defined(__GNUC__) && defined(TFMX_SHARED)
#  define TFMX_API __attribute__((visibility("default")))
#else
#  define TFMX_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define TFMX_VERSION_MAJOR 0
#define TFMX_VERSION_MINOR 1
#define TFMX_VERSION_PATCH 0

/* Returns the library version as "major.minor.patch". */
TFMX_API const char *tfmx_version(void);

typedef struct tfmx_module tfmx_module;

typedef enum tfmx_error {
	TFMX_OK = 0,
	TFMX_ERROR_ARGUMENT,       /* a null pointer or an invalid setting */
	TFMX_ERROR_FORMAT,         /* not a TFMX song, or damaged */
	TFMX_ERROR_MEMORY          /* out of memory */
} tfmx_error;

TFMX_API const char *tfmx_error_string(tfmx_error error);

typedef enum tfmx_interpolation {
	TFMX_INTERPOLATION_NONE = 0, /* nearest sample, like the Amiga (bright, with aliasing) */
	TFMX_INTERPOLATION_LINEAR,
	TFMX_INTERPOLATION_CUBIC     /* default */
} tfmx_interpolation;

/*
 * Replayer behaviour. TFMX was never standardised: each game shipped its own build of the
 * replayer, and the builds differ in timing and effect details. AUTO picks one from the song.
 */
typedef enum tfmx_variant {
	TFMX_VARIANT_AUTO = 0,
	TFMX_VARIANT_PRO,          /* TFMX Pro and 7V (Apidya, B.C. Kid, most later games) */
	TFMX_VARIANT_15,           /* TFMX 1.5 (Turrican 1) */
	TFMX_VARIANT_MASTERBLAZER, /* Masterblazer, Z-Out */
	TFMX_VARIANT_XOUT,         /* X-Out, Fly Harder, Abandoned Places */
	TFMX_VARIANT_ROCKNROLL,    /* Rock'n'Roll (1989) */
	TFMX_VARIANT_GEMX,         /* GemX, Monkey Island: Pro-like, with older volume timing */
	TFMX_VARIANT_TURRICAN2,    /* Turrican 2's two builds: for 7V songs (the title) Pro, but a new
	                              note's first update also runs the effects; for 4-voice songs
	                              (loading, levels) GemX-like, deferring 00's cut as Pro does.
	                              Never picked by AUTO */
	TFMX_VARIANT_TURRICAN3_SMALL, /* Turrican 3's small 4-voice build (its loading music): Pro
	                                 without the AddBegin sweep, 06/15/16, 1C/1D and FC. The game's
	                                 other builds (7V and in-game) are plain Pro. Never picked by
	                                 AUTO */
	TFMX_VARIANT_HEXUMA         /* Hexuma's in-game build: Pro, but macro vibrato (0C) does
	                                 nothing. Never picked by AUTO */
} tfmx_variant;

typedef struct tfmx_settings {
	int sample_rate;                  /* output rate in Hz, 8000-192000 (default 44100) */
	int stereo_separation;            /* 0 = mono, 100 = hard Amiga panning (default 100) */
	tfmx_interpolation interpolation; /* default TFMX_INTERPOLATION_CUBIC */
	int volume_ramping;               /* anti-click ramp in microseconds, 0 = off (default 2000) */
	int loop_count;                   /* extra times to play a looping song: 0 = play once (default), -1 = forever */
	tfmx_variant variant;             /* default TFMX_VARIANT_AUTO */
	float gain;                       /* output gain, 1.0 = default */
} tfmx_settings;

/* Fills settings with the defaults. */
TFMX_API void tfmx_default_settings(tfmx_settings *settings);

/*
 * Opens a song from its two files: mdat (the song data) and smpl (the samples). The data is
 * copied, so the caller may free its buffers afterwards. settings may be NULL for the defaults,
 * and error may be NULL. Subsong 0 (the first in tfmx_subsong_count's list) is selected.
 */
TFMX_API tfmx_module *tfmx_open(const void *mdat, size_t mdat_size, const void *smpl, size_t smpl_size,
                                const tfmx_settings *settings, tfmx_error *error);

/* Opens a single-file song: TFHD (header + mdat + smpl), as used by some players and archives. */
TFMX_API tfmx_module *tfmx_open_single(const void *data, size_t size, const tfmx_settings *settings, tfmx_error *error);

/* Returns 1 if the data looks like an mdat or a TFHD file. Only the first 512 bytes are needed. */
TFMX_API int tfmx_probe(const void *data, size_t size);

TFMX_API void tfmx_close(tfmx_module *module);

/* --- song information --- */

/* Number of playable subsongs (the empty slots of the 32 in the file are skipped). */
TFMX_API int tfmx_subsong_count(const tfmx_module *module);

/* The song file's own number (0-31) for a subsong index. */
TFMX_API int tfmx_subsong_number(const tfmx_module *module, int index);

/*
 * Length of a subsong in seconds, up to its end or the point where it loops, plus loop_count
 * extra loops. Computed by a fast run of the replayer, so it costs a little time on first call.
 * Returns -1 if it couldn't be determined (no end found within 30 minutes).
 */
TFMX_API double tfmx_subsong_duration(tfmx_module *module, int index);

/*
 * The song's text block: up to 6 lines of 40 characters (title, composer, date...), as written
 * by the editor, with trailing spaces removed and lines separated by '\n'. Often empty or "(Empty)".
 */
TFMX_API const char *tfmx_text(const tfmx_module *module);

/* The variant in use (after AUTO detection) and a name for it. */
TFMX_API tfmx_variant tfmx_get_variant(const tfmx_module *module);
TFMX_API const char *tfmx_variant_name(tfmx_variant variant);

/* Number of voices: 4, or 7 for a 7-voice (7V) song. Only known once the subsong has started. */
TFMX_API int tfmx_voice_count(const tfmx_module *module);

/* --- playback --- */

/* Selects and restarts a subsong (an index below tfmx_subsong_count). Returns 0 on success. */
TFMX_API int tfmx_select_subsong(tfmx_module *module, int index);

/*
 * Renders up to 'frames' stereo frames (interleaved left, right). Returns the number rendered,
 * which is less than 'frames' only at the end of the song (0 once it has ended).
 */
TFMX_API size_t tfmx_render_s16(tfmx_module *module, int16_t *buffer, size_t frames);
TFMX_API size_t tfmx_render_float(tfmx_module *module, float *buffer, size_t frames);

/* Seeks within the current subsong, in seconds. Returns the new position. */
TFMX_API double tfmx_seek(tfmx_module *module, double seconds);

/* Current position in the current subsong, in seconds. */
TFMX_API double tfmx_position(const tfmx_module *module);

/*
 * Changes settings on an open module. sample_rate and variant take effect from the next
 * tfmx_select_subsong; the others take effect at once.
 */
TFMX_API int tfmx_set_settings(tfmx_module *module, const tfmx_settings *settings);

#ifdef __cplusplus
}
#endif

#endif
