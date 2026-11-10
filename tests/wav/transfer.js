// How a recording relates to a render, by frequency band: gain, phase and coherence (how much of the
// recording the render explains in that band). A flat gain near 0 dB with coherence near 1 means the
// two match there; low coherence shows where they differ (DC, high-frequency stepping, wrong notes).
// node transfer.js <recording.wav> <render.wav> <song start s> [L|R] [from s] [seconds]
'use strict';
const {read, fft, lag} = require('./wav.js');
const args = process.argv.slice(2);
if (args.length < 3) { console.error('usage: node transfer.js <recording.wav> <render.wav> <song start s> [L|R] [from s] [seconds]'); process.exit(1); }
const [recPath, renderPath, startArg, side = 'L', fromArg = '10', secsArg = '30'] = args;
const o = read(renderPath), R = o.rate, N = 8192;
const from = +fromArg, secs = Math.min(+secsArg, o.n / R - from - 1);
const cap = read(recPath, +startArg - 1, from + secs + 2);
const sxx = new Float64Array(N / 2), syy = new Float64Array(N / 2), sxr = new Float64Array(N / 2), sxi = new Float64Array(N / 2);
let guess = R;
for (let t = Math.round(from * R); t + N < (from + secs) * R; t += N / 2) {
  guess = lag(cap[side], o[side], t, N, guess, R / 100);
  const xr = new Float64Array(N), xi = new Float64Array(N), yr = new Float64Array(N), yi = new Float64Array(N);
  for (let i = 0; i < N; i++) { const w = 0.5 - 0.5 * Math.cos(2 * Math.PI * i / N); xr[i] = o[side][t + i] * w; yr[i] = cap[side][guess + t + i] * w; }
  fft(xr, xi); fft(yr, yi);
  for (let k = 0; k < N / 2; k++) {
    sxx[k] += xr[k] ** 2 + xi[k] ** 2; syy[k] += yr[k] ** 2 + yi[k] ** 2;
    sxr[k] += yr[k] * xr[k] + yi[k] * xi[k]; sxi[k] += yi[k] * xr[k] - yr[k] * xi[k];
  }
}
const edges = [1, 10, 20, 40, 80, 160, 320, 640, 1300, 2500, 4000, 6000, 8000, 12000, 16000, R / 2];
for (let b = 0; b + 1 < edges.length; b++) {
  let xx = 0, yy = 0, cr = 0, ci = 0;
  for (let k = Math.ceil(edges[b] * N / R); k < Math.min(N / 2, edges[b + 1] * N / R); k++) { xx += sxx[k]; yy += syy[k]; cr += sxr[k]; ci += sxi[k]; }
  if (!xx || !yy) continue;
  const h = Math.hypot(cr, ci) / xx, ph = Math.atan2(ci, cr) * 180 / Math.PI, coh = (cr * cr + ci * ci) / (xx * yy);
  console.log(`${String(edges[b]).padStart(5)}-${String(edges[b + 1]).padEnd(5)} Hz  gain ${(20 * Math.log10(h)).toFixed(1).padStart(6)} dB  phase ${ph.toFixed(0).padStart(5)}  coherence ${coh.toFixed(2)}`);
}
