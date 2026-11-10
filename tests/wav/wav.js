// What the WAV tools share: reading and writing 16-bit stereo WAV files, filters, and the
// alignment and fitting used to compare TFMXreplay's output with recordings.
'use strict';
const fs = require('fs');

// Finds the 'data' chunk: {offset, bytes, rate}.
function layout(head) {
  if (head.toString('ascii', 0, 4) !== 'RIFF' || head.toString('ascii', 8, 12) !== 'WAVE') throw new Error('not a WAV file');
  let p = 12, rate = 0, channels = 0, bits = 0;
  while (p + 8 <= head.length) {
    const id = head.toString('ascii', p, p + 4), n = head.readUInt32LE(p + 4);
    if (id === 'fmt ') { channels = head.readUInt16LE(p + 10); rate = head.readUInt32LE(p + 12); bits = head.readUInt16LE(p + 22); }
    if (id === 'data') {
      if (channels !== 2 || bits !== 16) throw new Error('only 16-bit stereo WAV files are supported');
      return {offset: p + 8, bytes: n, rate};
    }
    p += 8 + n + (n & 1);
  }
  throw new Error('no data chunk');
}

// {L, R, n, rate}, samples as floats in -1..1. With from/seconds, only that stretch is read, so a long
// recording needn't fit in memory.
function read(path, from = 0, seconds = Infinity) {
  const fd = fs.openSync(path, 'r');
  try {
    const head = Buffer.alloc(65536);
    fs.readSync(fd, head, 0, head.length, 0);
    const {offset, bytes, rate} = layout(head);
    const size = Math.min(bytes, fs.fstatSync(fd).size - offset);
    const first = Math.max(0, Math.round(from * rate));
    const n = Math.max(0, Math.min(Math.floor(size / 4) - first, isFinite(seconds) ? Math.round(seconds * rate) : Infinity));
    const body = Buffer.alloc(n * 4);
    fs.readSync(fd, body, 0, body.length, offset + first * 4);
    const L = new Float32Array(n), R = new Float32Array(n);
    for (let i = 0; i < n; i++) { L[i] = body.readInt16LE(4 * i) / 32768; R[i] = body.readInt16LE(4 * i + 2) / 32768; }
    return {L, R, n, rate};
  } finally {
    fs.closeSync(fd);
  }
}

function write(path, L, R, rate) {
  const n = Math.min(L.length, R.length), b = Buffer.alloc(44 + n * 4);
  b.write('RIFF', 0); b.writeUInt32LE(36 + n * 4, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16);
  b.writeUInt16LE(1, 20); b.writeUInt16LE(2, 22); b.writeUInt32LE(rate, 24); b.writeUInt32LE(rate * 4, 28);
  b.writeUInt16LE(4, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(n * 4, 40);
  const q = (x) => Math.max(-32768, Math.min(32767, Math.round(x * 32768)));
  for (let i = 0; i < n; i++) { b.writeInt16LE(q(L[i]), 44 + 4 * i); b.writeInt16LE(q(R[i]), 46 + 4 * i); }
  fs.writeFileSync(path, b);
}

// Second-order Butterworth, run forwards then backwards so the result has no phase shift.
function butterworth(x, rate, fc, type) {
  const k = Math.tan(Math.PI * fc / rate), q = Math.SQRT1_2, norm = 1 / (1 + k / q + k * k);
  const low = type === 'low';
  const b0 = low ? k * k * norm : norm, b1 = low ? 2 * b0 : -2 * norm, b2 = b0;
  const a1 = 2 * (k * k - 1) * norm, a2 = (1 - k / q + k * k) * norm;
  const y = Float32Array.from(x);
  const pass = (start, end, dir) => {
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (let i = start; i !== end; i += dir) {
      const v = y[i], out = b0 * v + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
      x2 = x1; x1 = v; y2 = y1; y1 = out; y[i] = out;
    }
  };
  pass(0, y.length, 1);
  pass(y.length - 1, -1, -1);
  return y;
}

// Keeps hp..lp Hz of both channels (either may be 0 for none).
function band(w, hp, lp) {
  let {L, R} = w;
  if (hp > 0) { L = butterworth(L, w.rate, hp, 'high'); R = butterworth(R, w.rate, hp, 'high'); }
  if (lp > 0) { L = butterworth(L, w.rate, lp, 'low'); R = butterworth(R, w.rate, lp, 'low'); }
  return {L, R, n: w.n, rate: w.rate};
}

function fft(re, im, inverse) {
  const n = re.length;
  for (let i = 1, j = 0; i < n; i++) {
    let b = n >> 1;
    for (; j & b; b >>= 1) j ^= b;
    j ^= b;
    if (i < j) { [re[i], re[j]] = [re[j], re[i]]; [im[i], im[j]] = [im[j], im[i]]; }
  }
  for (let len = 2; len <= n; len <<= 1) {
    const a = 2 * Math.PI / len * (inverse ? 1 : -1), wr = Math.cos(a), wi = Math.sin(a);
    for (let i = 0; i < n; i += len) {
      let cr = 1, ci = 0;
      for (let j = 0; j < len / 2; j++) {
        const u = i + j, v = u + len / 2, xr = re[v] * cr - im[v] * ci, xi = re[v] * ci + im[v] * cr;
        re[v] = re[u] - xr; im[v] = im[u] - xi; re[u] += xr; im[u] += xi;
        const t = cr * wr - ci * wi; ci = cr * wi + ci * wr; cr = t;
      }
    }
  }
  if (inverse) for (let i = 0; i < n; i++) { re[i] /= n; im[i] /= n; }
}

// The lag (in samples) at which y[pos .. pos+len) best matches x, searched within +-range of 'guess':
// x[pos + lag + i] lines up with y[pos + i].
function lag(x, y, pos, len, guess, range) {
  const N = 1 << Math.ceil(Math.log2(len + 2 * range + 2));
  const ar = new Float64Array(N), ai = new Float64Array(N), br = new Float64Array(N), bi = new Float64Array(N);
  const start = pos + guess - range;
  for (let i = 0; i < len + 2 * range && start + i < x.length; i++) if (start + i >= 0) ar[i] = x[start + i];
  for (let i = 0; i < len; i++) br[i] = y[pos + i] || 0;
  fft(ar, ai); fft(br, bi);
  for (let i = 0; i < N; i++) { const r = ar[i] * br[i] + ai[i] * bi[i], m = ai[i] * br[i] - ar[i] * bi[i]; ar[i] = r; ai[i] = m; }
  fft(ar, ai, true);
  let best = -Infinity, at = 0;
  for (let i = 0; i <= 2 * range; i++) if (ar[i] > best) { best = ar[i]; at = i; }
  return at - range + guess;
}

// Least-squares gain of y against x at a lag, and what's left: {gain, residual (dB of x), rms of x}.
function fit(x, y, pos, len, at) {
  let xy = 0, yy = 0, xx = 0;
  for (let i = 0; i < len; i++) { const a = x[pos + at + i] || 0, b = y[pos + i] || 0; xy += a * b; yy += b * b; xx += a * a; }
  const gain = yy ? xy / yy : 0;
  let e = 0;
  for (let i = 0; i < len; i++) { const d = (x[pos + at + i] || 0) - gain * (y[pos + i] || 0); e += d * d; }
  return {gain, residual: 10 * Math.log10((e + 1e-12) / (xx + 1e-12)), rms: Math.sqrt(xx / len)};
}

// Where y starts within x (seconds), from 10 ms envelopes of both channels: {at, corr}.
// 'near' limits the search to within 20 s of that time.
function locate(x, y, near) {
  const hop = Math.round(x.rate / 100);
  const env = (s) => { const m = Math.floor(s.length / hop), e = new Float32Array(m); for (let i = 0; i < m; i++) { let a = 0; for (let j = 0; j < hop; j++) a += Math.abs(s[i * hop + j]); e[i] = a; } return e; };
  const xe = [env(x.L), env(x.R)], ye = [env(y.L), env(y.R)];
  const W = Math.min(4000, ye[0].length);
  const from = near === undefined ? 0 : Math.max(0, Math.floor((near - 20) * 100));
  const to = near === undefined ? Infinity : Math.floor((near + 20) * 100);
  let best = -1, off = 0;
  for (let k = from; k + W < xe[0].length && k < to; k++) {
    let s = 0, sa = 0, sb = 0;
    for (let side = 0; side < 2; side++) for (let i = 0; i < W; i += 2) { const a = xe[side][k + i], b = ye[side][i]; s += a * b; sa += a * a; sb += b * b; }
    const c = s / (Math.sqrt(sa * sb) + 1e-9);
    if (c > best) { best = c; off = k; }
  }
  return {at: off * hop / x.rate, corr: best};
}

const median = (a) => { const s = [...a].sort((p, q) => p - q); return s.length ? s[s.length >> 1] : NaN; };

module.exports = {read, write, band, butterworth, fft, lag, fit, locate, median};
