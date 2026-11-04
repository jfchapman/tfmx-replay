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
	TFMX_INTERPOLATION_NONE = 0, /* nearest sample, like the Amiga (bright, with aliasing): default */
	TFMX_INTERPOLATION_LINEAR,
	TFMX_INTERPOLATION_CUBIC
} tfmx_interpolation;

/*
 * The fixed low-pass filter on the Amiga's audio output, always in the path whatever the power
 * LED shows. The separate LED filter is tfmx_settings.led_filter.
 */
typedef enum tfmx_amiga_model {
	TFMX_AMIGA_A500 = 0,  /* 6 dB/octave at about 4.9 kHz: default */
	TFMX_AMIGA_A1200,     /* 6 dB/octave at about 29 kHz: close to flat */
	TFMX_AMIGA_NONE       /* no fixed filter */
} tfmx_amiga_model;

/*
 * Replayer behaviour. TFMX was never standardised: each game shipped its own build of the
 * replayer, and the builds differ in timing and effect details. AUTO picks one from the song's
 * contents, or from a table of known songs for those that can't be told apart that way (the
 * variants marked "known songs" below are only picked from the table).
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
	                              Known songs */
	TFMX_VARIANT_TURRICAN3_SMALL, /* Turrican 3's small 4-voice build (its loading music): Pro
	                                 without the AddBegin sweep, 06/15/16, 1C/1D and FC. The game's
	                                 other builds (7V and in-game) are plain Pro. Known songs */
	TFMX_VARIANT_HEXUMA         /* Hexuma's in-game build: Pro, but macro vibrato (0C) does
	                                 nothing. Known songs */
} tfmx_variant;

typedef struct tfmx_settings {
	int sample_rate;                  /* output rate in Hz, 8000-192000 (default 44100) */
	int stereo_separation;            /* 0 = mono, 100 = hard Amiga panning (default 100) */
	tfmx_interpolation interpolation; /* default TFMX_INTERPOLATION_NONE */
	int volume_ramping;               /* anti-click ramp in microseconds, 0 = off (default 2000) */
	int loop_count;                   /* extra times to play a looped song (see tfmx_subsong_info):
	                                     0 = play once (default), -1 = forever */
	tfmx_variant variant;             /* default TFMX_VARIANT_AUTO */
	float gain;                       /* output gain, 1.0 = default */
	tfmx_amiga_model amiga_model;     /* the fixed output filter, default TFMX_AMIGA_A500 */
	int led_filter;                   /* 1 = the "LED" filter (12 dB/octave at about 3.3 kHz), as with the
	                                     power LED bright. 0 = off (default). Songs can't switch it:
	                                     TFMX has no command for it, and the replayers turn it off */
	float fade_seconds;               /* after its loops, a looped song plays on, fading out over this
	                                     many seconds. 0 = stop at the loop point (default) */
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

/* The subsong index for a song file number (0-31), or -1 if the file has no such subsong. */
TFMX_API int tfmx_subsong_index(const tfmx_module *module, int number);

/*
 * Playing length of a subsong in seconds with the current settings: up to its end or the point
 * where it loops, plus any loop_count extra loops and fade_seconds if it's a looped song.
 * Returns -1 if it couldn't be determined (no end found within 30 minutes).
 */
TFMX_API double tfmx_subsong_duration(tfmx_module *module, int index);

/* What's known about a subsong, whatever the settings. */
typedef struct tfmx_subsong_info {
	int number;          /* the song file's own number (0-31) */
	double duration;     /* one play-through in seconds, -1 if no end was found */
	double loop_start;   /* where the song continues from at the end of a play-through, in seconds; -1 if it doesn't */
	double loop_length;  /* seconds from loop_start to the end of the play-through, 0 if it doesn't loop */
	int looped;          /* 1 if the song is heard to loop: it loops, and doesn't end in silence first. Other
	                        songs stop at the end of a play-through, whatever loop_count and fade_seconds say */
	int audible;         /* 0 for a silent subsong (or an empty one, such as a sound effects slot) */
	int repeat_of;       /* the index of a longer subsong that plays the same music, or -1. Different start
	                        points into one song count as the same music */
} tfmx_subsong_info;

/*
 * Fills info for a subsong. Returns 0 on success. The first call listens to every subsong of the
 * song, a few milliseconds each on a desktop; the results are kept until the variant changes.
 */
TFMX_API int tfmx_get_subsong_info(tfmx_module *module, int index, tfmx_subsong_info *info);

/*
 * The subsong to play when only one will do: the longest that's audible and not a repeat,
 * or an earlier one within 10% of that length. It's 0 if nothing qualifies. -1 on error.
 */
TFMX_API int tfmx_default_subsong(tfmx_module *module);

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

/*
 * Seeks by time played since the start of the subsong, in seconds (a time past the first
 * play-through is in the later loops or the fade-out). Returns the new position. It renders from
 * the start and discards the audio, so the sound is exact and the time taken grows with the position.
 */
TFMX_API double tfmx_seek(tfmx_module *module, double seconds);

/* Time played since the start of the subsong, in seconds: it keeps rising through loops. */
TFMX_API double tfmx_position(const tfmx_module *module);

/*
 * Where the song is now, in seconds: like tfmx_position until the song loops, then back in the
 * range from loop_start to duration. For a seek bar on a looping song.
 */
TFMX_API double tfmx_song_position(const tfmx_module *module);

/*
 * Changes settings on an open module. sample_rate and variant take effect from the next
 * tfmx_select_subsong; the others take effect at once.
 */
TFMX_API int tfmx_set_settings(tfmx_module *module, const tfmx_settings *settings);

#ifdef __cplusplus
}
#endif

#endif
