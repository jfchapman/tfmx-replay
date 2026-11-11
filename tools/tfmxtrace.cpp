// tfmxtrace: prints the Paula register writes the replayer makes, tick by tick.
//
// Copyright (c) 2026 James Chapman. MIT licence: see LICENSE.
//
// For testing against traces of the original replayers (tests/compare.py). Uses the library's
// internal classes, not the public API.
//
// Usage: tfmxtrace <mdat> <smpl> [song number] [ticks] [variant] [mix [setting]]
//   variant: pro, 1.5, masterblazer, xout, rocknroll, gemx (default: detect)
//   mix: print each tick's 7V mix block in hex, instead of the register writes
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

#include "module.h"
#include "paula.h"
#include "replayer.h"
#include "variant.h"

using namespace tfmx;

static std::vector<uint8_t> read_file(const char *path) {
	std::ifstream f(path, std::ios::binary);
	return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
}

int main(int argc, char **argv) {
	if (argc < 3) {
		fprintf(stderr, "usage: tfmxtrace <mdat> <smpl> [song number] [ticks] [variant] [mix]\n");
		return 1;
	}
	const std::vector<uint8_t> mdat = read_file(argv[1]), smpl = read_file(argv[2]);
	const int number = argc > 3 ? atoi(argv[3]) : 0;
	const int ticks = argc > 4 ? atoi(argv[4]) : 500;
	tfmx_variant variant = TFMX_VARIANT_AUTO;
	if (argc > 5) {
		const char *v = argv[5];
		variant = !strcmp(v, "pro")            ? TFMX_VARIANT_PRO
		          : !strcmp(v, "1.5")          ? TFMX_VARIANT_15
		          : !strcmp(v, "masterblazer") ? TFMX_VARIANT_MASTERBLAZER
		          : !strcmp(v, "xout")         ? TFMX_VARIANT_XOUT
		          : !strcmp(v, "rocknroll")    ? TFMX_VARIANT_ROCKNROLL
		          : !strcmp(v, "gemx")         ? TFMX_VARIANT_GEMX
		          : !strcmp(v, "turrican2")    ? TFMX_VARIANT_TURRICAN2
		          : !strcmp(v, "t3small")      ? TFMX_VARIANT_TURRICAN3_SMALL
		          : !strcmp(v, "hexuma")       ? TFMX_VARIANT_HEXUMA
		          : !strcmp(v, "logical")      ? TFMX_VARIANT_LOGICAL
		                                       : TFMX_VARIANT_AUTO;
	}
	const bool show_mix = argc > 6 && !strcmp(argv[6], "mix");
	const bool show_sid = argc > 6 && !strcmp(argv[6], "sid");
	Module m;
	if (!m.load(mdat.data(), mdat.size(), smpl.data(), smpl.size())) {
		fprintf(stderr, "not a TFMX song\n");
		return 1;
	}
	// The argument is the song's own number (0-31), as the original replayer's start call takes it.
	int found = -1;
	for (size_t i = 0; i < m.subsongs.size(); i++)
		if (m.subsongs[i].number == number)
			found = int(i);
	if (found < 0) {
		fprintf(stderr, "no subsong %d\n", number);
		return 1;
	}
	const size_t index = size_t(found);
	if (variant == TFMX_VARIANT_AUTO)
		variant = detect_variant(m);
	Paula paula;
	paula.set_memory(&m.smpl);
	Replayer r;
	std::vector<PaulaWrite> log;
	r.init(&m, behaviour_of(variant, m), &paula);
	r.log = &log;
	r.set_mix_setting(argc > 7 ? atoi(argv[7]) : 0);
	r.start(m.subsongs[index]);
	static const char *names[] = {"LC", "LEN", "PER", "VOL", "DMA_OFF", "DMA_ON", "NOTE"};
	printf("variant %d subsong %d\n", int(variant), m.subsongs[index].number);
	printf("start\n");
	for (const PaulaWrite &w : log)
		printf(" %d %s %X\n", w.voice, names[w.reg], w.value);
	for (int t = 0; t < ticks && !r.ended(); t++) {
		log.clear();
		r.tick();
		paula.advance(r.tick_seconds());
		if (show_sid) {
			// The four SID buffers (smpl + 4, $100 bytes per voice) after the tick, as hex.
			for (size_t i = 4; i < 0x404 && i < m.smpl.size(); i++)
				printf("%02x", unsigned(m.smpl[i]));
			printf("\n");
			continue;
		}
		if (show_mix) {
			for (int8_t b : paula.mix_next())
				printf("%02x", unsigned(uint8_t(b)));
			printf("\n");
			continue;
		}
		printf("tick %d line %d blocks %u %u %u %u\n", t, r.line(), paula.channel(0).block_starts,
		       paula.channel(1).block_starts, paula.channel(2).block_starts, paula.channel(3).block_starts);
		for (const PaulaWrite &w : log)
			printf(" %d %s %X\n", w.voice, names[w.reg], w.value);
	}
	return 0;
}
