(() => {
  'use strict';
  const bands = ['mix', 'bass', 'mid', 'high'];
  function filter(rate, frequency, highpass) {
    const w = 2 * Math.PI * Math.min(frequency, rate * .45) / rate;
    const c = Math.cos(w), alpha = Math.sin(w) / (2 * .707), a0 = 1 + alpha;
    const b0 = (1 + (highpass ? c : -c)) / 2 / a0;
    const b1 = (highpass ? -(1 + c) : 1 - c) / a0;
    const a1 = -2 * c / a0, a2 = (1 - alpha) / a0;
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    return x => { const y = b0 * x + b1 * x1 + b0 * x2 - a1 * y1 - a2 * y2; x2 = x1; x1 = x; y2 = y1; y1 = y; return y; };
  }
  async function analyze(buffer, cancelled, mixOnly = false) {
    const count = Math.min(buffer.length, 2000);
    const result = Object.fromEntries(bands.map(key => [key, new Float32Array(count)]));
    const channels = Array.from({ length: buffer.numberOfChannels }, (_, i) => buffer.getChannelData(i));
    const filters = channels.map(() => [filter(buffer.sampleRate, 250, false), filter(buffer.sampleRate, 250, true), filter(buffer.sampleRate, 2000, false), filter(buffer.sampleRate, 2000, true)]);
    let lastYield = performance.now();
    for (let bin = 0; bin < count; bin++) {
      const from = Math.floor(bin * buffer.length / count), to = Math.floor((bin + 1) * buffer.length / count);
      let mix = 0, bass = 0, mid = 0, high = 0;
      for (let channel = 0; channel < channels.length; channel++) {
        const samples = channels[channel], f = filters[channel];
        for (let i = from; i < to; i++) {
          const x = Number.isFinite(samples[i]) ? samples[i] : 0;
          mix += Math.abs(x); if (mixOnly) continue; bass += Math.abs(f[0](x)); mid += Math.abs(f[2](f[1](x))); high += Math.abs(f[3](x));
        }
      }
      const divisor = (to - from) * channels.length;
      result.mix[bin] = mix / divisor; result.bass[bin] = bass / divisor; result.mid[bin] = mid / divisor; result.high[bin] = high / divisor;
      if (performance.now() - lastYield > 12) {
        await new Promise(resolve => setTimeout(resolve, 0));
        if (cancelled()) throw new DOMException('Superseded', 'AbortError');
        lastYield = performance.now();
      }
    }
    for (const layer of Object.values(result)) { let max = 0; for (const value of layer) max = Math.max(max, value); if (max > 0) for (let i = 0; i < layer.length; i++) layer[i] /= max; }
    return result;
  }
  function surface(canvas) {
    const width = canvas.clientWidth, height = canvas.clientHeight, dpr = Math.min(devicePixelRatio || 1, 2);
    if (canvas.width !== Math.round(width * dpr) || canvas.height !== Math.round(height * dpr)) { canvas.width = Math.round(width * dpr); canvas.height = Math.round(height * dpr); }
    const context = canvas.getContext('2d'); context.setTransform(dpr, 0, 0, dpr, 0, 0); context.clearRect(0, 0, width, height);
    return { context, width, height };
  }
  function gradient(context, width) { const color = context.createLinearGradient(0, 0, width, 0); color.addColorStop(0, '#00d4ff'); color.addColorStop(.5, '#7b2ff7'); color.addColorStop(1, '#e62e9b'); return color; }
  function wave(canvas, data, mode, progress = 0) {
    const { context: c, width: w, height: h } = surface(canvas);
    if (!data || !w) return;
    const n = Math.min(data.mix.length, Math.max(1, Math.ceil(w)));
    function layer(values, color, alpha = 1, minimum = .5) {
      c.beginPath(); c.strokeStyle = color; c.globalAlpha = alpha; c.lineWidth = 1;
      for (let i = 0; i < n; i++) {
        let value = 0;
        for (let j = Math.floor(i * values.length / n); j < Math.floor((i + 1) * values.length / n); j++) value = Math.max(value, values[j]);
        const x = n === 1 ? w / 2 : i * w / (n - 1), amplitude = Math.max(minimum, value * h * .4);
        c.moveTo(x, h / 2 - amplitude); c.lineTo(x, h / 2 + amplitude);
      }
      c.stroke(); c.globalAlpha = 1;
    }
    function pass(played) {
      if (mode === 'frequency') {
        layer(data.mix, '#7a8898', played ? .45 : .18);
        layer(data.bass, 'rgb(170,55,55)', (played ? 158 : 55) / 255, 0);
        layer(data.mid, 'rgb(55,140,55)', (played ? 148 : 52) / 255, 0);
        layer(data.high, 'rgb(55,90,145)', (played ? 133 : 46) / 255, 0);
      } else layer(data.mix, mode === 'solid' ? (played ? '#d27722' : '#9098a6') : (played ? gradient(c, w) : '#00b4a0'));
    }
    pass(false);
    c.save(); c.beginPath(); c.rect(0, 0, w * progress, h); c.clip(); pass(true); c.restore();
    if (progress > 0 && progress < 1) { c.fillStyle = '#edfaff'; c.fillRect(w * progress, 0, 1, h); }
  }
  // Actual output samples -> Hann-window FFT512 -> the player's mirrored 128-column spectrum.
  class Spectrum {
    constructor() { this.real = new Float64Array(512); this.imag = new Float64Array(512); this.engine = new Float32Array(128); this.shown = new Float32Array(128); this.held = new Float32Array(128); }
    update(samples, dt) {
      const re = this.real, im = this.imag, n = 512;
      for (let i = 0; i < n; i++) { re[i] = samples[i] * (.5 - .5 * Math.cos(2 * Math.PI * i / (n - 1))); im[i] = 0; }
      for (let i = 1, j = 0; i < n; i++) { let bit = n >> 1; for (; j & bit; bit >>= 1) j ^= bit; j ^= bit; if (i < j) [re[i], re[j]] = [re[j], re[i]]; }
      for (let size = 2; size <= n; size <<= 1) {
        const half = size >> 1;
        for (let base = 0; base < n; base += size) for (let j = 0; j < half; j++) {
          const angle = -2 * Math.PI * j / size, cos = Math.cos(angle), sin = Math.sin(angle), a = base + j, b = a + half;
          const tr = cos * re[b] - sin * im[b], ti = sin * re[b] + cos * im[b];
          re[b] = re[a] - tr; im[b] = im[a] - ti; re[a] += tr; im[a] += ti;
        }
      }
      for (let i = 0; i < 128; i++) {
        const magnitude = 4 * Math.hypot(re[i + 1], im[i + 1]) / n;
        const target = Math.min(1, Math.log1p(12 * magnitude) / Math.log(13));
        this.engine[i] = target + (this.engine[i] - target) * (target > this.engine[i] ? .25 : .82);
      }
      for (let i = 0; i < 128; i++) {
        const mirrored = i < 64 ? i : 127 - i;
        const target = Math.min(1, Math.sqrt(this.engine[mirrored * 2]) * 1.35);
        const alpha = 1 - Math.exp(-dt / (target > this.shown[i] ? .012 : .075));
        this.shown[i] += (target - this.shown[i]) * alpha;
        this.held[i] = target >= this.held[i] ? target : target + (this.held[i] - target) * Math.exp(-dt / .75);
      }
    }
    draw(canvas) {
      const { context: c, width: w, height: h } = surface(canvas);
      const n = Math.max(1, Math.min(1024, Math.ceil(w * Math.min(devicePixelRatio || 1, 2) / 7))), step = w / n;
      c.fillStyle = gradient(c, w);
      for (let i = 0; i < n; i++) {
        const position = i * 127 / Math.max(1, n - 1), lo = Math.floor(position), hi = Math.min(127, lo + 1), fraction = position - lo;
        const value = this.shown[lo] + (this.shown[hi] - this.shown[lo]) * fraction;
        const held = this.held[lo] + (this.held[hi] - this.held[lo]) * fraction;
        const envelope = .6 + .4 * Math.sin(Math.PI * i / Math.max(1, n - 1));
        const scale = Math.min(h - 8, 96) * envelope, bar = Math.pow(value, .58) * scale, peak = Math.pow(held, .58) * scale, barWidth = Math.min(5, step * 5 / 7);
        c.fillRect(i * step + (step - barWidth) / 2, h - 4 - bar, barWidth, Math.max(1, bar));
        if (peak > 2) c.fillRect(i * step + (step - barWidth) / 2, h - 7 - peak, barWidth, 1);
      }
    }
  }
  window.AgWaveform = { analyze, wave, Spectrum };
  // Animated style illustrations, isolated from the selected track's real audio data.
  const demo = Object.fromEntries(bands.map(key => [key, new Float32Array(320)]));
  function animateEnvelope(phase) {
    for (let i = 0; i < 320; i++) {
      const u = i / 319;
      // Reference-style traveling oscillations and changing beat amplitude.
      const envelope = .28 + .55 * Math.sin(u * Math.PI) + .16 * Math.sin(u * Math.PI * 6);
      const low = Math.sin(i * .071 + phase * 1.7) * .2 + Math.sin(i * .021 - phase * .3) * .12;
      const mid = Math.sin(i * .13 - phase * .8) * .28;
      const high = Math.sin(i * .37 + phase) * .34;
      const beat = Math.pow(Math.abs(Math.sin(i * .038 + phase)), 6) * .32;
      demo.mix[i] = Math.min(1, Math.abs(low + mid + high) * envelope + beat + .06);
      demo.bass[i] = Math.min(1, Math.abs(low) * 2 * envelope + beat);
      demo.mid[i] = Math.min(1, Math.abs(mid) * 2.5 * envelope + beat * .5);
      demo.high[i] = Math.min(1, Math.abs(high) * 2 * envelope + beat * .25);
    }
  }
  const reduced = matchMedia('(prefers-reduced-motion: reduce)');
  const previews = [...document.querySelectorAll('.wave-preview')].map(canvas => ({ canvas, visible: false, spectrum: new Spectrum() }));
  let frame = 0, last = 0;
  function renderPreview(item, now) {
    const phase = reduced.matches ? 0 : now / 1000;
    if (item.canvas.dataset.preview === 'spectrum') {
      for (let i = 0; i < 128; i++) {
        const value = .08 + .8 * Math.abs(Math.sin(i * .13 + phase * 1.7)) * Math.sin(Math.PI * i / 127);
        item.spectrum.shown[i] = value;
        item.spectrum.held[i] = Math.min(1, value + .05);
      }
      item.spectrum.draw(item.canvas);
    } else {
      animateEnvelope(phase * 1.55);
      // Fully colored demonstration: moving geometry, no progress cursor.
      wave(item.canvas, demo, item.canvas.dataset.preview, 1);
    }
  }
  function tick(now) {
    frame = 0;
    if (document.hidden || reduced.matches || !previews.some(item => item.visible)) return;
    if (now - last >= 50) { last = now; previews.filter(item => item.visible).forEach(item => renderPreview(item, now)); }
    frame = requestAnimationFrame(tick);
  }
  function sync() {
    cancelAnimationFrame(frame); frame = 0;
    previews.filter(item => item.visible).forEach(item => renderPreview(item, performance.now()));
    if (!document.hidden && !reduced.matches && previews.some(item => item.visible)) frame = requestAnimationFrame(tick);
  }
  const observer = new IntersectionObserver(entries => { for (const entry of entries) previews.find(item => item.canvas === entry.target).visible = entry.isIntersecting; sync(); });
  previews.forEach(item => { observer.observe(item.canvas); new ResizeObserver(() => renderPreview(item, performance.now())).observe(item.canvas); renderPreview(item, performance.now()); });
  document.addEventListener('visibilitychange', sync); reduced.addEventListener('change', sync);
})();
