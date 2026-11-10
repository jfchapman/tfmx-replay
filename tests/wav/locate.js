// Finds where a render plays in a recording, then fits 1 s windows along it, per side: lag, gain
// and residual. Further renders of the same song (another build, say) are fitted on the same lags.
// node locate.js <recording.wav> <render.wav>[,<render2.wav>...] [--near seconds] [--hp Hz] [--lp Hz]
// --near limits the search to 20 s around that point. A 20 Hz high-pass and a 5 kHz low-pass keep
// DC offsets and the emulators' differing high frequencies out of the fit (§16.2 of the format doc).
'use strict';
const {read, band, lag, fit, locate, median} = require('./wav.js');
const args = process.argv.slice(2);
const opt = (name, d) => { const i = args.indexOf(name); return i < 0 ? d : +args.splice(i, 2)[1]; };
const near = opt('--near', undefined), hp = opt('--hp', 0), lp = opt('--lp', 0);
if (args.length < 2) { console.error('usage: node locate.js <recording.wav> <render.wav>[,<render2.wav>...] [--near s] [--hp Hz] [--lp Hz]'); process.exit(1); }
const renders = args[1].split(',').map((f) => band(read(f), hp, lp));
const o = renders[0];
// Coarse position from the whole recording's envelope, then the stretch the song covers.
const whole = read(args[0]);
const {at, corr} = locate(whole, o, near);
const pad = 2;
const from = Math.max(0, at - pad);
const cap = band(read(args[0], from, o.n / o.rate + 2 * pad), hp, lp);
const R = cap.rate;
console.log(`found at ${at.toFixed(3)} s (envelope correlation ${corr.toFixed(3)})`);
let guess = Math.round((at - from) * R);
const rows = renders.map(() => ({L: [], R: [], gL: [], gR: []}));
let first = null, last = null, windows = 0;
for (let t = 1; t < o.n / R - 2; t += 2) {
  const pos = Math.round(t * R), len = R;
  const lagL = lag(cap.L, o.L, pos, len, guess, R / 100), lagR = lag(cap.R, o.R, pos, len, guess, R / 100);
  const fl = fit(cap.L, o.L, pos, len, lagL), fr = fit(cap.R, o.R, pos, len, lagR);
  if (Math.min(fl.residual, fr.residual) > -6) continue;  // not this song here
  const use = fl.residual < fr.residual ? lagL : lagR;
  guess = use; windows++;
  first ??= use; last = use;
  renders.forEach((r, i) => {
    const a = fit(cap.L, r.L, pos, len, lagL), b = fit(cap.R, r.R, pos, len, lagR);
    if (a.rms > 0.003) { rows[i].L.push(a.residual); rows[i].gL.push(a.gain); }
    if (b.rms > 0.003) { rows[i].R.push(b.residual); rows[i].gR.push(b.gain); }
  });
}
console.log(`${windows} windows matched, timing drift ${(((last ?? 0) - (first ?? 0)) / R * 1000).toFixed(1)} ms`);
rows.forEach((r, i) => console.log(`render ${i + 1}: residual median L ${median(r.L).toFixed(1)} R ${median(r.R).toFixed(1)} dB, worst L ${Math.max(...r.L).toFixed(1)} R ${Math.max(...r.R).toFixed(1)}, gain L ${median(r.gL).toFixed(3)} R ${median(r.gR).toFixed(3)}`));
