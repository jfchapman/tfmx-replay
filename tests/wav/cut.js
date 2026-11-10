// Copies a stretch of a 16-bit stereo WAV file, optionally band-limited.
// node cut.js <in.wav> <out.wav> <from seconds> <seconds> [--hp Hz] [--lp Hz]
// Only the stretch is read, so it works on long recordings. Use seconds = 0 for the rest of the file.
'use strict';
const {read, write, band} = require('./wav.js');
const args = process.argv.slice(2);
const opt = (name) => { const i = args.indexOf(name); return i < 0 ? 0 : +args.splice(i, 2)[1]; };
const hp = opt('--hp'), lp = opt('--lp');
if (args.length < 4) { console.error('usage: node cut.js <in.wav> <out.wav> <from seconds> <seconds> [--hp Hz] [--lp Hz]'); process.exit(1); }
const [inp, out, from, secs] = args;
const w = band(read(inp, +from, +secs || Infinity), hp, lp);
write(out, w.L, w.R, w.rate);
console.log(`${out}: ${(w.n / w.rate).toFixed(2)} s`);
