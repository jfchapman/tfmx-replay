// Fits each side of a recording from its two voices (left 0 + 3, right 1 + 2), each voice on its own
// lag, for one or more sets of single-voice renders (tfmxrender -V n), and compares the sets.
// node voicefit.js <recording.wav> <song start s> <name>=<voice files with %v> ... [--seconds n]
//   e.g. node voicefit.js rec.wav 269.5 old=lf-old-%v.wav new=lf-new-%v.wav
// The recording and renders are band-limited to 20 Hz - 5 kHz (§16.2 of the format doc). The game
// writes the voices in turn, so each has its own small time offset; one lag per side would break the
// fit wherever both of a side's voices play. The song start should be within 5 ms (locate.js).
'use strict';
const {read, band, lag, median} = require('./wav.js');
const args = process.argv.slice(2);
const opt = (name, d) => { const i = args.indexOf(name); return i < 0 ? d : +args.splice(i, 2)[1]; };
const seconds = opt('--seconds', Infinity);
if (args.length < 3) { console.error('usage: node voicefit.js <recording.wav> <song start s> <name>=<voice files with %v> ... [--seconds n]'); process.exit(1); }
const start = +args[1];
const sets = args.slice(2).map((a) => { const [name, pattern] = a.split('='); return {name, v: [0, 1, 2, 3].map((k) => band(read(pattern.replace('%v', k)), 20, 5000))}; });
const n = Math.min(sets[0].v[0].n, Math.round(seconds * sets[0].v[0].rate));
const pad = 1;
const cap = band(read(args[0], start - pad, n / sets[0].v[0].rate + 2 * pad), 20, 5000);
const R = cap.rate, base = Math.round(pad * R);
const sides = {L: [0, 3], R: [1, 2]};
const lags = [base, base, base, base];
const out = {L: sets.map(() => []), R: sets.map(() => [])};
for (let t = 1; t + 2 < n / R; t++) {
  const pos = t * R;
  for (const [side, ks] of Object.entries(sides)) {
    const y = cap[side];
    const live = ks.filter((k) => { let e = 0; for (let i = 0; i < R; i += 4) e += sets[0].v[k][side][pos + i] ** 2; return e / (R / 4) > 1e-6; });
    if (!live.length) continue;
    for (const k of live) lags[k] = lag(y, sets[0].v[k][side], pos, R, lags[k], R / 200);
    const ref = lags[live[0]];
    let yy = 0;
    for (let i = 0; i < R; i++) yy += y[ref + pos + i] ** 2;
    sets.forEach((set, si) => {
      const cols = live.map((k) => { const c = new Float32Array(R), d = lags[k] - ref; for (let i = 0; i < R; i++) c[i] = set.v[k][side][pos + i - d] || 0; return c; });
      const target = (i) => y[ref + pos + i];
      let g;
      if (cols.length === 1) {
        let a = 0, b = 0;
        for (let i = 0; i < R; i++) { a += cols[0][i] * target(i); b += cols[0][i] ** 2; }
        g = [b ? a / b : 0];
      } else {
        let aa = 0, bb = 0, ab = 0, ay = 0, by = 0;
        for (let i = 0; i < R; i++) { const p = cols[0][i], q = cols[1][i], r = target(i); aa += p * p; bb += q * q; ab += p * q; ay += p * r; by += q * r; }
        const det = aa * bb - ab * ab;
        g = det > 1e-9 * aa * bb ? [(ay * bb - by * ab) / det, (by * aa - ay * ab) / det] : [aa ? ay / aa : 0, 0];
      }
      let e = 0;
      for (let i = 0; i < R; i++) { let m = 0; cols.forEach((c, j) => { m += g[j] * c[i]; }); e += (target(i) - m) ** 2; }
      out[side][si].push(10 * Math.log10((e + 1e-12) / (yy + 1e-12)));
    });
  }
}
for (const side of ['L', 'R']) {
  const cols = sets.map((s, i) => `${s.name} ${median(out[side][i]).toFixed(1)} dB`).join(', ');
  let extra = '';
  if (sets.length > 1) {
    const a = out[side][0], b = out[side][1];
    extra = `; ${sets[1].name} better in ${b.filter((x, i) => x < a[i] - 0.1).length}, worse in ${b.filter((x, i) => x > a[i] + 0.1).length} of ${a.length} windows`;
  }
  console.log(`${side}: residual median ${cols}${extra}`);
}
