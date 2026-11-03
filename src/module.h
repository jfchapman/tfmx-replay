// TFMXreplay: song file parsing (docs/TFMX-format.md, sections 1-4).
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tfmx {

inline uint16_t be16(const uint8_t *p) { return uint16_t(p[0] << 8 | p[1]); }
inline uint32_t be32(const uint8_t *p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

enum class Magic { TfmxSong, LowerCase, Odd };

struct Subsong {
	int number;   // 0-31 in the file
	int start;    // first trackstep line
	int end;      // last trackstep line
	int tempo;    // < 16: speed; >= 16: BPM
};

struct Module {
	std::vector<uint8_t> mdat;
	std::vector<uint8_t> smpl;       // writable: SID synthesises into the start of it
	Magic magic = Magic::TfmxSong;
	bool packed = false;             // the offsets at 0x1D0 are used
	uint32_t trackstep = 0;          // file offset of trackstep line 0
	int trackstep_lines = 0;
	std::vector<uint32_t> patterns;  // 128 file offsets, 0 = missing
	std::vector<uint32_t> macros;    // 128 file offsets, 0 = missing
	std::vector<Subsong> subsongs;   // playable ones only
	std::string text;

	// Features found by scanning the song data (for variant detection).
	bool has_note_wait = false;      // pattern notes $80-$BF
	bool has_7v = false;             // trackstep EFFE0003 turning 7V on
	int dmaoff_volumes = 0;          // 00 commands with a volume (b2b3 != 0), up to each macro's 07

	// Returns false if the data isn't a usable TFMX song.
	bool load(const uint8_t *mdat_data, size_t mdat_size, const uint8_t *smpl_data, size_t smpl_size);

	uint32_t long_at(uint32_t offset) const { return offset + 4 <= mdat.size() ? be32(&mdat[offset]) : 0; }
	uint16_t word_at(uint32_t offset) const { return offset + 2 <= mdat.size() ? be16(&mdat[offset]) : 0; }
	const uint8_t *trackstep_line(int line) const { return &mdat[trackstep + 16u * unsigned(line)]; }
};

bool probe_mdat(const uint8_t *data, size_t size);

// Splits a TFHD single file into its parts. Returns false if it isn't one.
bool split_tfhd(const uint8_t *data, size_t size, const uint8_t **mdat, size_t *mdat_size,
                const uint8_t **smpl, size_t *smpl_size);

}  // namespace tfmx
