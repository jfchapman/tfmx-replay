// Compares a recording with renders, song by song, by sound rather than by waveform: each tick's
// level and spectrum, so timing differences of a fraction of a millisecond (which two emulators'
// Paulas always have, and which make a plain subtraction look far worse than it sounds) don't count.
// node specfit.js <recording.wav> <first start s> <render.wav>:<seconds> [<render.wav>:<seconds> ...] [--bands]
// Renders must have the recording's sample rate, and no filter or interpolation the recording doesn't
// have. The first song is found within 0.2 s of <first start s>; each next one near where the one
// before should end (its start + its <seconds>). A song is compared until the next one starts (the last
// for its <seconds>), so give the length actually played, and compare a song on its own, with a shorter
// <seconds>, if the recording goes on to something else (a game fading the music, a stopped player
// leaving a looped sample sounding).
// Per tick (one PAL frame) and side: the level, and 1/3-octave band levels from 25 Hz to 5 kHz (a
// 2048-point Hann window centred on the tick), after one gain per song and side. The alignment is
// refreshed every second. It prints each side's gain (recording against render), the median and
// 95th percentile of the tick level error and of the band error (the energy-weighted mean difference
// over bands within 40 dB of the tick's loudest), and the worst ticks. --bands also prints each band's
// mean difference, which shows any tilt in tone. Matching recordings give gains within a few
// hundredths of a dB and median band errors well under 2 dB.
'use strict';
const {read, band, fft, lag, locate, median} = require('./wav.js');
const args = process.argv.slice(2);
const bandsOpt = args.includes('--bands');
const pos = args.filter((a) => a !== '--bands');
if (pos.length < 3) {
  console.error('usage: node specfit.js <recording.wav> <first start s> <render.wav>:<seconds> [<render.wav>:<seconds> ...] [--bands]');
  process.exit(1);
}
const recPath = pos[0];
let start = +pos[1];
const songs = pos.slice(2).map((a) => { const i = a.lastIndexOf(':'); return {path: a.slice(0, i), min: +a.slice(i + 1)}; });
const RATE = read(recPath, 0, 0.01).rate;
const TICK = RATE * 313 * 227 / 3546895;  // samples per PAL frame
const N = 2048;
const hann = Float64Array.from({length: N}, (_, i) => 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N));
const bands = [];
for (let f = 25; f <= 5000; f *= Math.pow(2, 1 / 3)) bands.push([Math.floor(f / Math.pow(2, 1 / 6) * N / RATE), Math.ceil(f * Math.pow(2, 1 / 6) * N / RATE), f]);
function spectrum(x, centre) {
  const re = new Float64Array(N), im = new Float64Array(N);
  for (let i = 0; i < N; i++) { const p = Math.round(centre) - N / 2 + i; re[i] = (x[p] || 0) * hann[i]; }
  fft(re, im);
  return bands.map(([a, b]) => { let e = 0; for (let k = Math.max(1, a); k <= Math.min(b, N / 2 - 1); k++) e += re[k] * re[k] + im[k] * im[k]; return e; });
}
const rms = (x, from, n) => { let s = 0; for (let i = 0; i < n; i++) { const v = x[Math.round(from) + i] || 0; s += v * v; } return Math.sqrt(s / n); };
const db = (v) => 10 * Math.log10(v + 1e-12);

// Where each song starts.
const found = [];
for (let s = 0; s < songs.length; s++) {
  const song = songs[s];
  const lib = band(read(song.path), 20, 0);
  if (lib.rate !== RATE) { console.error(`${song.path}: ${lib.rate} Hz, the recording is ${RATE} Hz`); process.exit(1); }
  // After the first, search around the expected start with the render's first 20 s.
  if (s > 0) {
    const clip = {L: lib.L.subarray(0, 20 * RATE), R: lib.R.subarray(0, 20 * RATE), n: 20 * RATE, rate: RATE};
    const from = start - 10;
    const win = band(read(recPath, from, 90), 20, 0);
    let best = {corr: -1};
    for (let near = 10; near < 70; near += 30) { const r = locate(win, clip, near); if (r.corr > best.corr) best = r; }
    start = from + best.at;
  }
  // Then to the sample: the median of the lags of 1 s windows (within 200 ms, both sides) over the song's
  // first 10 s. One window alone can lock a tick or two off where a synth's waveform differs.
  const rec = band(read(recPath, start - 1, song.min + 62), 20, 0);
  const lags = [];
  for (let t = 1; t <= 10; t++) for (const side of ['L', 'R'])
    if (rms(lib[side], t * RATE, RATE) > 0.005) lags.push(lag(rec[side], lib[side], t * RATE, RATE, RATE, RATE / 5));
  const off = median(lags);  // the recording's index of the render's sample 0
  const exact = start - 1 + off / RATE;
  found.push({song, exact, rec, lib, off});
  start = exact + song.min;
}

for (let s = 0; s < found.length; s++) {
  const {song, exact, rec, lib} = found[s];
  let {off} = found[s];
  const end = s + 1 < found.length ? found[s + 1].exact : exact + song.min;
  const length = Math.min(end - exact, lib.n / RATE) - 0.1;
  const ticks = Math.floor(length * RATE / TICK);
  const raw = {L: [], R: []};
  for (let t = 1; t < ticks - 1; t++) {
    const p = t * TICK;
    if (t % 50 === 0 && p + RATE < lib.n) {
      const a = lag(rec.L, lib.L, Math.round(p), RATE, off, Math.round(RATE / 500)), b = lag(rec.R, lib.R, Math.round(p), RATE, off, Math.round(RATE / 500));
      off = rms(lib.L, p, RATE) >= rms(lib.R, p, RATE) ? a : b;
    }
    for (const side of ['L', 'R']) {
      const x = rec[side], y = lib[side];
      const lr = rms(x, p + off, Math.round(TICK)), ll = rms(y, p, Math.round(TICK));
      if (ll < 0.002 && lr < 0.002) continue;
      raw[side].push({t, lr, ll, sr: spectrum(x, p + off + TICK / 2), sl: spectrum(y, p + TICK / 2)});
    }
  }
  console.log(`${song.path.split(/[\\/]/).pop()}: starts ${exact.toFixed(3)} s, compared ${length.toFixed(1)} s`);
  for (const side of ['L', 'R']) {
    const g = median(raw[side].filter((r) => r.ll > 0.01).map((r) => db(r.lr * r.lr) - db(r.ll * r.ll)));
    const levErr = raw[side].map((r) => Math.abs(db(r.lr * r.lr) - db(r.ll * r.ll) - g)).sort((a, b) => a - b);
    const specErr = raw[side].map((r) => {
      const a = r.sr.map(db), b = r.sl.map((v) => db(v) + g), top = Math.max(...b);
      let w = 0, e = 0;
      for (let k = 0; k < a.length; k++) if (b[k] > top - 40) { const wt = Math.pow(10, (b[k] - top) / 20); w += wt; e += wt * Math.abs(a[k] - b[k]); }
      return w ? e / w : 0;
    });
    const sorted = [...specErr].sort((a, b) => a - b);
    const worst = raw[side].map((r, i) => ({sec: r.t * TICK / RATE, e: specErr[i]})).sort((a, b) => b.e - a.e).slice(0, 3);
    console.log(`  ${side}: gain ${g.toFixed(2)} dB; tick level error median ${levErr[levErr.length >> 1].toFixed(2)} dB, 95% ${levErr[Math.floor(levErr.length * 0.95)].toFixed(2)};` +
      ` band error median ${sorted[sorted.length >> 1].toFixed(2)} dB, 95% ${sorted[Math.floor(sorted.length * 0.95)].toFixed(2)};` +
      ` worst at ${worst.map((w) => w.sec.toFixed(2) + ' s (' + w.e.toFixed(1) + ')').join(', ')}`);
    if (bandsOpt) {
      const sum = new Float64Array(bands.length), cnt = new Float64Array(bands.length);
      for (const r of raw[side]) { const b = r.sl.map((v) => db(v) + g), top = Math.max(...b); r.sr.forEach((v, k) => { if (b[k] > top - 40) { sum[k] += db(v) - b[k]; cnt[k]++; } }); }
      console.log(`    mean band difference (recording - render, dB): ` + bands.map((bd, k) => Math.round(bd[2]) + ' Hz ' + (cnt[k] ? (sum[k] / cnt[k]).toFixed(1) : '-')).join(', '));
    }
  }
}
