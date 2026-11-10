// Blind listening test: one file made of random stretches of two or more versions of a song (a
// recording and renders, say), so you can listen for the joins. The first file is the timeline;
// the others are lined up with it stretch by stretch, and each join is a 20 ms crossfade, so the
// join itself makes no click (a DC offset in one version would otherwise give it away).
// node splice.js <out.wav> <key.txt> <name>=<file> <name>=<file> ... [--min s] [--max s] [--seed n]
// The key lists each stretch's start and source. Listen before reading it.
'use strict';
const fs = require('fs');
const {read, write, lag} = require('./wav.js');
const args = process.argv.slice(2);
const opt = (name, d) => { const i = args.indexOf(name); return i < 0 ? d : +args.splice(i, 2)[1]; };
const minS = opt('--min', 3), maxS = opt('--max', 9);
let seed = opt('--seed', Date.now() % 2147483648);
if (args.length < 4) { console.error('usage: node splice.js <out.wav> <key.txt> <name>=<file> <name>=<file> ... [--min s] [--max s] [--seed n]'); process.exit(1); }
const [out, keyPath, ...srcArgs] = args;
const srcs = srcArgs.map((a) => { const [name, f] = a.split('='); return {name, w: read(f)}; });
const R = srcs[0].w.rate, FADE = Math.round(0.02 * R);
const n = Math.min(...srcs.map((s) => s.w.n)) - R;
const rnd = () => { seed = (seed * 1103515245 + 12345) % 2147483648; return seed / 2147483648; };
const parts = [];
for (let pos = 0, last = -1; pos < n;) {
  const len = Math.min(n - pos, Math.round((minS + rnd() * (maxS - minS)) * R));
  let k;
  do { k = Math.floor(rnd() * srcs.length); } while (k === last);
  parts.push({pos, len, k}); last = k; pos += len;
}
const lags = srcs.map(() => 0);
for (const p of parts) {
  p.lag = 0;
  if (p.k === 0) continue;
  const at = Math.max(0, Math.min(p.pos + (p.len >> 1), n - R) - (R >> 1));
  p.lag = lags[p.k] = lag(srcs[0].w.L, srcs[p.k].w.L, at, R, lags[p.k], R / 100);
}
const at = (k, shift, side, i) => { const x = srcs[k].w[side], j = i - shift; return j >= 0 && j < x.length ? x[j] : 0; };
const L = new Float32Array(n), Rt = new Float32Array(n);
parts.forEach((p, idx) => {
  const prev = idx ? parts[idx - 1] : null;
  for (let i = p.pos; i < p.pos + p.len; i++) {
    const t = i - p.pos, g = prev && t < FADE ? t / FADE : 1;
    L[i] = g * at(p.k, p.lag, 'L', i) + (g < 1 ? (1 - g) * at(prev.k, prev.lag, 'L', i) : 0);
    Rt[i] = g * at(p.k, p.lag, 'R', i) + (g < 1 ? (1 - g) * at(prev.k, prev.lag, 'R', i) : 0);
  }
});
write(out, L, Rt, R);
const mmss = (s) => `${Math.floor(s / 60)}:${(s % 60).toFixed(1).padStart(4, '0')}`;
fs.writeFileSync(keyPath, parts.map((p) => `${mmss(p.pos / R)}  ${srcs[p.k].name}`).join('\n') + '\n');
console.log(`${out}: ${parts.length} stretches, ${(n / R).toFixed(1)} s; key in ${keyPath}`);
