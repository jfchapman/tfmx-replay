// TFMXreplay: song file parsing.
#include "module.h"

#include <algorithm>
#include <cstring>

namespace tfmx {

namespace {

const size_t kHeaderSize = 0x200;

bool magic_of(const uint8_t *d, size_t size, Magic *magic) {
	if (size < kHeaderSize)
		return false;
	if (!memcmp(d, "TFMX-SONG", 9) || !memcmp(d, "TFMX_SONG", 9) || !memcmp(d, "TFMXSONG", 8)) {
		*magic = Magic::TfmxSong;
		return true;
	}
	if (!memcmp(d, "tfmxsong", 8)) {
		*magic = Magic::LowerCase;
		return true;
	}
	// The 1989 generation (Rock'n'Roll): "TFMX " followed by binary rather than text.
	if (!memcmp(d, "TFMX ", 5)) {
		*magic = Magic::Odd;
		return true;
	}
	return false;
}

// A trackstep line is a command (EFFE and a known command) or eight track words, each a pattern
// number (00-7F) or one of the codes 80, FE and FF (or anything from 90, which means idle).
bool plausible_line(const uint8_t *l) {
	if (be16(l) == 0xEFFE)
		return be16(l + 2) <= 4 || be16(l + 2) == 0xFF00;
	for (int t = 0; t < 8; t++) {
		const int hi = l[2 * t];
		if (hi > 0x80 && hi < 0x90)
			return false;
	}
	return true;
}

}  // namespace

bool probe_mdat(const uint8_t *data, size_t size) {
	Magic m;
	return data && magic_of(data, size, &m);
}

bool split_tfhd(const uint8_t *d, size_t size, const uint8_t **mdat, size_t *mdat_size,
                const uint8_t **smpl, size_t *smpl_size) {
	if (!d || size < 0x12 || memcmp(d, "TFHD", 4))
		return false;
	const uint32_t header = be32(d + 4), msize = be32(d + 0x0A), ssize = be32(d + 0x0E);
	if (header < 0x12 || header > size || msize > size - header || ssize > size - header - msize)
		return false;
	*mdat = d + header;
	*mdat_size = msize;
	*smpl = d + header + msize;
	*smpl_size = ssize;
	return true;
}

bool Module::load(const uint8_t *mdat_data, size_t mdat_size, const uint8_t *smpl_data, size_t smpl_size) {
	if (!mdat_data || !magic_of(mdat_data, mdat_size, &magic))
		return false;
	mdat.assign(mdat_data, mdat_data + mdat_size);
	smpl.assign(smpl_data, smpl_data + smpl_size);
	checksum = 14695981039346656037ull;  // FNV-1a
	for (const uint8_t byte : mdat)
		checksum = (checksum ^ byte) * 1099511628211ull;
	// Macros read a little past a sample's end at times (and SID writes buffers at the start), so
	// keep at least a few KB of zeros available.
	if (smpl.size() < 0x1000)
		smpl.resize(0x1000, 0);

	uint32_t ts = long_at(0x1D0), pt = long_at(0x1D4), mt = long_at(0x1D8);
	packed = ts || pt || mt;
	if (!packed) {
		// Unpacked layout: fixed table offsets.
		ts = 0x800;
		pt = 0x400;
		mt = 0x600;
	}
	if (ts >= mdat.size() || pt + 4 > mdat.size() || mt + 4 > mdat.size())
		return false;
	trackstep = ts;

	// Pointer tables: 128 entries each. Unused slots can hold junk, so validate each entry.
	auto read_table = [&](uint32_t at, std::vector<uint32_t> &out) {
		out.assign(128, 0);
		for (int i = 0; i < 128; i++) {
			const uint32_t entry = at + 4u * unsigned(i);
			if (entry + 4 > mdat.size())
				break;
			const uint32_t off = be32(&mdat[entry]);
			if (off >= kHeaderSize && off + 4 <= mdat.size() && !(off & 1))
				out[size_t(i)] = off;
		}
	};
	read_table(pt, patterns);
	read_table(mt, macros);

	// Trackstep lines run from the trackstep toward the song data after it. Unused table slots in
	// unpacked files can hold junk pointers, so only the tables bound it, not the patterns; each
	// subsong's lines are checked below instead.
	uint32_t ts_end = uint32_t(mdat.size());
	if (pt > ts)
		ts_end = std::min(ts_end, pt);
	if (mt > ts)
		ts_end = std::min(ts_end, mt);
	trackstep_lines = int((ts_end - ts) / 16);
	if (trackstep_lines <= 0)
		return false;

	// Subsongs: in use if the start or end is non-zero and both lie inside the trackstep.
	// Slot 31 often holds an editor placeholder (start = end = 0x1FF), which this also skips.
	subsongs.clear();
	for (int i = 0; i < 32; i++) {
		const int start = word_at(0x100 + 2 * i), end = word_at(0x140 + 2 * i), tempo = word_at(0x180 + 2 * i);
		if ((!start && !end && i) || (start == 0x1FF && end == 0x1FF))
			continue;
		if (start >= trackstep_lines || end >= trackstep_lines || end < start)
			continue;
		// Editors fill unused slots with a line that only stops the song (EFFE0000), and the
		// placeholder in slot 31 can point past the trackstep into other data.
		const uint8_t *first = &mdat[trackstep + 16u * unsigned(start)];
		if (be16(first) == 0xEFFE && be16(first + 2) == 0)
			continue;
		if (!plausible_line(first))
			continue;
		subsongs.push_back({i, start, end, tempo});
	}
	if (subsongs.empty())
		return false;

	// Text: 6 lines of 40 characters at 0x10 (not in the odd 1989 header).
	text.clear();
	if (magic != Magic::Odd) {
		for (int line = 0; line < 6; line++) {
			std::string s;
			for (int c = 0; c < 40; c++) {
				const uint8_t ch = mdat[0x10 + 40 * line + c];
				s += (ch >= 0x20 && ch < 0x7F) ? char(ch) : ' ';
			}
			s.erase(s.find_last_not_of(' ') + 1);
			if (!text.empty() || !s.empty())
				text += (text.empty() ? "" : "\n") + s;
		}
		text.erase(text.find_last_not_of("\n ") + 1);
	}

	// Feature scan.
	for (uint32_t p : patterns) {
		for (uint32_t o = p; p && o + 4 <= mdat.size() && o < p + 4096; o += 4) {
			const uint8_t c = mdat[o];
			if (c >= 0x80 && c < 0xC0)
				has_note_wait = true;
			if (c == 0xF0 || c == 0xF4 || c == 0xFE)
				break;
		}
	}
	std::vector<uint32_t> seen;
	for (uint32_t p : macros) {
		if (!p || std::find(seen.begin(), seen.end(), p) != seen.end())
			continue;
		seen.push_back(p);
		for (uint32_t o = p; o + 4 <= mdat.size() && o < p + 1024 && mdat[o] != 0x07; o += 4) {
			if (mdat[o] == 0x00 && (mdat[o + 2] || mdat[o + 3]))
				dmaoff_volumes++;
		}
	}
	for (int line = 0; line < trackstep_lines; line++) {
		const uint8_t *l = trackstep_line(line);
		if (be16(l) == 0xEFFE && be16(l + 2) == 3 && !(be16(l + 6) & 0x8000))
			has_7v = true;
	}
	return true;
}

}  // namespace tfmx
