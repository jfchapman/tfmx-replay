/*
 * tfmxrender: renders a TFMX song to a WAV file, using only the public TFMXreplay API.
 *
 * Copyright (c) 2026 James Chapman. MIT licence: see LICENSE.
 *
 * Usage: tfmxrender [options] <mdat file | TFHD file> [smpl file]
 * Run without arguments for the options.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tfmxreplay.h"

static void usage(void) {
	fprintf(stderr,
	        "tfmxrender %s: render a TFMX song to WAV\n"
	        "\n"
	        "usage: tfmxrender [options] <mdat file | TFHD file> [smpl file]\n"
	        "\n"
	        "  -o <file>     output file (default: <song name>.wav, or <song name>-<n>.wav with -a)\n"
	        "  -s <n>        subsong index (default 0)\n"
	        "  -a            render every subsong to its own file\n"
	        "  -r <rate>     sample rate (default 44100)\n"
	        "  -l <n>        extra loops of a looping song (default 0)\n"
	        "  -i <mode>     interpolation: none, linear, cubic (default cubic)\n"
	        "  -p <percent>  stereo separation, 0-100 (default 100)\n"
	        "  -m <us>       volume ramping in microseconds, 0 = off (default 2000)\n"
	        "  -v <variant>  pro, 1.5, masterblazer, xout, rocknroll, gemx, turrican2, t3small, hexuma (default: detect)\n"
	        "  -n            print the song's information only\n"
	        "\n"
	        "If the smpl file isn't given, it's found from the mdat name: mdat.<name> -> smpl.<name>,\n"
	        "<name>.mdat -> <name>.smpl, or <name>.tfx -> <name>.sam (in either case). A TFHD file\n"
	        "holds both parts.\n",
	        tfmx_version());
}

static void *read_file(const char *path, size_t *size) {
	FILE *f = fopen(path, "rb");
	void *data = NULL;
	long length;
	if (!f)
		return NULL;
	if (fseek(f, 0, SEEK_END) == 0 && (length = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
		data = malloc(length ? (size_t)length : 1);
		if (data && fread(data, 1, (size_t)length, f) != (size_t)length) {
			free(data);
			data = NULL;
		}
		*size = (size_t)length;
	}
	fclose(f);
	return data;
}

static int same_text(const char *a, const char *b, size_t n) {
	size_t i;
	for (i = 0; i < n; i++) {
		char x = a[i], y = b[i];
		if (x >= 'A' && x <= 'Z')
			x = (char)(x - 'A' + 'a');
		if (y >= 'A' && y <= 'Z')
			y = (char)(y - 'A' + 'a');
		if (x != y)
			return 0;
	}
	return 1;
}

/* Copies 'to' over 'at', keeping the case of the original (all capitals or not). */
static void replace(char *at, const char *original, const char *to) {
	size_t i, n = strlen(to);
	int upper = original[0] >= 'A' && original[0] <= 'Z';
	for (i = 0; i < n; i++)
		at[i] = upper ? (char)(to[i] - 'a' + 'A') : to[i];
}

/*
 * The smpl file's name from the mdat's, in any case:
 *   mdat.<name> -> smpl.<name>   (Amiga style)
 *   <name>.mdat -> <name>.smpl   (extension style)
 *   <name>.tfx  -> <name>.sam    (DOS style)
 */
static char *smpl_name(const char *mdat) {
	size_t len = strlen(mdat);
	const char *base = mdat + len;
	char *out = (char *)malloc(len + 1);
	while (base > mdat && base[-1] != '/' && base[-1] != '\\')
		base--;
	memcpy(out, mdat, len + 1);
	if (strlen(base) > 5 && same_text(base, "mdat.", 5)) {
		replace(out + (base - mdat), base, "smpl");
		return out;
	}
	if (len > 5 && same_text(mdat + len - 5, ".mdat", 5)) {
		replace(out + len - 4, mdat + len - 4, "smpl");
		return out;
	}
	if (len > 4 && same_text(mdat + len - 4, ".tfx", 4)) {
		replace(out + len - 3, mdat + len - 3, "sam");
		return out;
	}
	free(out);
	return NULL;
}

/* The song's name for output files: the mdat's file name without "mdat.", ".mdat" or ".tfx". */
static void song_name(const char *path, char *out, size_t size) {
	const char *base = path + strlen(path);
	size_t len;
	while (base > path && base[-1] != '/' && base[-1] != '\\')
		base--;
	if (strlen(base) > 5 && same_text(base, "mdat.", 5))
		base += 5;
	len = strlen(base);
	if (len > 5 && same_text(base + len - 5, ".mdat", 5))
		len -= 5;
	else if (len > 4 && same_text(base + len - 4, ".tfx", 4))
		len -= 4;
	if (len >= size)
		len = size - 1;
	memcpy(out, base, len);
	out[len] = 0;
}

static void put16(unsigned char *p, unsigned v) {
	p[0] = (unsigned char)v;
	p[1] = (unsigned char)(v >> 8);
}

static void put32(unsigned char *p, unsigned long v) {
	put16(p, (unsigned)(v & 0xFFFF));
	put16(p + 2, (unsigned)(v >> 16));
}

/* Writes a 16-bit stereo WAV file. Returns 0 on success. */
static int render_wav(tfmx_module *module, const char *path, int rate) {
	unsigned char header[44];
	int16_t buffer[2 * 4096];
	unsigned char bytes[4 * 4096];
	unsigned long frames = 0;
	size_t n, i;
	FILE *f = fopen(path, "wb");
	if (!f) {
		fprintf(stderr, "can't write %s\n", path);
		return 1;
	}
	fwrite(header, 1, sizeof header, f); /* filled in at the end */
	while ((n = tfmx_render_s16(module, buffer, 4096)) > 0) {
		for (i = 0; i < 2 * n; i++)
			put16(bytes + 2 * i, (unsigned)(uint16_t)buffer[i]);
		fwrite(bytes, 4, n, f);
		frames += (unsigned long)n;
	}
	memcpy(header, "RIFF", 4);
	put32(header + 4, 36 + frames * 4);
	memcpy(header + 8, "WAVEfmt ", 8);
	put32(header + 16, 16);
	put16(header + 20, 1); /* PCM */
	put16(header + 22, 2);
	put32(header + 24, (unsigned long)rate);
	put32(header + 28, (unsigned long)rate * 4);
	put16(header + 32, 4);
	put16(header + 34, 16);
	memcpy(header + 36, "data", 4);
	put32(header + 40, frames * 4);
	fseek(f, 0, SEEK_SET);
	fwrite(header, 1, sizeof header, f);
	fclose(f);
	printf("%s: %.1f seconds\n", path, (double)frames / rate);
	return 0;
}

int main(int argc, char **argv) {
	tfmx_settings settings;
	const char *out = NULL, *mdat_path = NULL, *smpl_path = NULL;
	int subsong = 0, all = 0, info = 0, i, result = 0;
	void *mdat, *smpl = NULL;
	size_t mdat_size, smpl_size = 0;
	char *derived = NULL;
	tfmx_module *module;
	tfmx_error error;

	tfmx_default_settings(&settings);
	for (i = 1; i < argc; i++) {
		const char *a = argv[i];
		const char *value = i + 1 < argc ? argv[i + 1] : NULL;
		if (a[0] != '-' || !a[1]) {
			if (!mdat_path)
				mdat_path = a;
			else
				smpl_path = a;
			continue;
		}
		if (!strcmp(a, "-a")) {
			all = 1;
		} else if (!strcmp(a, "-n")) {
			info = 1;
		} else if (value && strchr("osrlipmv", a[1]) && !a[2]) {
			i++;
			switch (a[1]) {
			case 'o': out = value; break;
			case 's': subsong = atoi(value); break;
			case 'r': settings.sample_rate = atoi(value); break;
			case 'l': settings.loop_count = atoi(value); break;
			case 'p': settings.stereo_separation = atoi(value); break;
			case 'm': settings.volume_ramping = atoi(value); break;
			case 'i':
				settings.interpolation = !strcmp(value, "none")     ? TFMX_INTERPOLATION_NONE
				                         : !strcmp(value, "linear") ? TFMX_INTERPOLATION_LINEAR
				                                                    : TFMX_INTERPOLATION_CUBIC;
				break;
			case 'v':
				settings.variant = !strcmp(value, "pro")            ? TFMX_VARIANT_PRO
				                   : !strcmp(value, "1.5")          ? TFMX_VARIANT_15
				                   : !strcmp(value, "masterblazer") ? TFMX_VARIANT_MASTERBLAZER
				                   : !strcmp(value, "xout")         ? TFMX_VARIANT_XOUT
				                   : !strcmp(value, "rocknroll")    ? TFMX_VARIANT_ROCKNROLL
				                   : !strcmp(value, "gemx")         ? TFMX_VARIANT_GEMX
				                   : !strcmp(value, "turrican2")    ? TFMX_VARIANT_TURRICAN2
				                   : !strcmp(value, "t3small")      ? TFMX_VARIANT_TURRICAN3_SMALL
				                   : !strcmp(value, "hexuma")       ? TFMX_VARIANT_HEXUMA
				                                                    : TFMX_VARIANT_AUTO;
				break;
			}
		} else {
			usage();
			return 1;
		}
	}
	if (!mdat_path) {
		usage();
		return 1;
	}

	mdat = read_file(mdat_path, &mdat_size);
	if (!mdat) {
		fprintf(stderr, "can't read %s\n", mdat_path);
		return 1;
	}
	if (mdat_size >= 4 && !memcmp(mdat, "TFHD", 4)) {
		module = tfmx_open_single(mdat, mdat_size, &settings, &error);
	} else {
		if (!smpl_path)
			smpl_path = derived = smpl_name(mdat_path);
		if (smpl_path)
			smpl = read_file(smpl_path, &smpl_size);
		if (!smpl) {
			fprintf(stderr, "can't read the smpl file %s\n", smpl_path ? smpl_path : "(give its name)");
			free(mdat);
			free(derived);
			return 1;
		}
		module = tfmx_open(mdat, mdat_size, smpl, smpl_size, &settings, &error);
	}
	free(mdat);
	free(smpl);
	free(derived);
	if (!module) {
		fprintf(stderr, "%s: %s\n", mdat_path, tfmx_error_string(error));
		return 1;
	}

	if (info) {
		const char *text = tfmx_text(module);
		printf("variant:  %s\n", tfmx_variant_name(tfmx_get_variant(module)));
		printf("subsongs: %d\n", tfmx_subsong_count(module));
		for (i = 0; i < tfmx_subsong_count(module); i++) {
			double d = tfmx_subsong_duration(module, i);
			printf("  %2d: song %2d, ", i, tfmx_subsong_number(module, i));
			if (d < 0)
				printf("no end found\n");
			else
				printf("%d:%04.1f\n", (int)(d / 60), d - 60 * (int)(d / 60));
		}
		if (*text)
			printf("text:\n%s\n", text);
	} else {
		char name[512], path[600];
		int first = all ? 0 : subsong, last = all ? tfmx_subsong_count(module) - 1 : subsong;
		song_name(mdat_path, name, sizeof name);
		for (i = first; i <= last && !result; i++) {
			if (tfmx_select_subsong(module, i)) {
				fprintf(stderr, "no subsong %d (the song has %d)\n", i, tfmx_subsong_count(module));
				result = 1;
				break;
			}
			if (out && !all)
				snprintf(path, sizeof path, "%s", out);
			else if (all || tfmx_subsong_count(module) > 1)
				snprintf(path, sizeof path, "%s-%d.wav", out ? out : name, i);
			else
				snprintf(path, sizeof path, "%s.wav", name);
			result = render_wav(module, path, settings.sample_rate);
		}
	}
	tfmx_close(module);
	return result;
}
