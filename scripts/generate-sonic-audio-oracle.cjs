// Test-only: execute the pinned upstream source, never a second handwritten port.
// node scripts/generate-sonic-audio-oracle.cjs <upstream checkout> <esbuild module> <output.json>
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const [root, bundler, output] = process.argv.slice(2);
const build = require(path.resolve(bundler)).buildSync({
  entryPoints: [path.join(root, 'src/lib/AudioEngine.ts')], bundle: true,
  platform: 'node', format: 'cjs', write: false,
});
let now = 0;
global.Audio = class { addEventListener() {} };
global.performance = { now: () => now };
const sourceModule = { exports: {} };
new Function('module', 'exports', build.outputFiles[0].text)(sourceModule, sourceModule.exports);
const { AudioEngine } = sourceModule.exports;
const cases = [];
for (const hz of [30, 60, 120]) {
  now = 0;
  const engine = new AudioEngine();
  let spectrum = new Uint8Array(512), events = {};
  engine.analyser = { getByteFrequencyData: target => target.set(spectrum) };
  engine.onFreqTrigger = (strength, type, action) => { events[action] = strength; };
  const frames = [];
  for (let frame = 0; frame < hz * 8; ++frame) {
    const dt = 1 / hz;
    now += dt * 1000;
    spectrum = new Uint8Array(512);
    const phase = frame % Math.round(hz * .25);
    const hit = phase < 2;
    // Separate maxima, sustained low tones, bright transients and silence.
    if (frame < hz * 6) {
      spectrum[1] = hit ? 240 : 50;
      spectrum[2] = hit ? 230 : 100;
      spectrum[8] = hit ? 255 : 0;
      for (let b = 40; b < 200; ++b)
        spectrum[b] = (frame % 39 < 3) ? (b * 7) % 230 : 0;
    }
    engine.isPlaying = !(frame >= hz * 4 && frame < hz * 5);
    engine.visualReleaseUntil = hz * 4 <= frame && frame < hz * 5 ? now + 1600 : 0;
    engine.currentFrameId++;
    events = {};
    const data = engine.getAudioData();
    frames.push({ dt, playing: engine.isPlaying, releasing: engine.visualReleaseUntil > now,
      spectrum: Buffer.from(spectrum).toString('base64'), data, events,
      band: [engine.pulseTrigger.bandStart, engine.pulseTrigger.bandEnd] });
  }
  cases.push({ hz, frames });
}
const themeBuild = require(path.resolve(bundler)).buildSync({
  entryPoints: [path.join(root, 'src/lib/themes.ts')], bundle: true,
  platform: 'node', format: 'cjs', write: false,
  alias: {three: path.resolve(bundler, '../three/build/three.cjs')},
});
const themeModule = {exports:{}};
new Function('module','exports',themeBuild.outputFiles[0].text)(themeModule,themeModule.exports);
const {themes,BUILT_IN_THEME_IDS} = themeModule.exports;
const palettes=Object.fromEntries(BUILT_IN_THEME_IDS.map(id=>[id,Object.fromEntries(
  Object.entries(themes[id]).filter(([k])=>k.startsWith('u')).map(([k,v])=>[k,v?.isColor?v.toArray():v]))]));
const files = ['AudioEngine.ts', 'beatDetector.ts', 'kickEnvelope.ts', 'themes.ts'];
const provenance = Object.fromEntries(files.map(name => [name, crypto.createHash('sha256')
  .update(fs.readFileSync(path.join(root, 'src/lib', name))).digest('hex')]));
fs.mkdirSync(path.dirname(output), { recursive: true });
fs.writeFileSync(output, JSON.stringify({ upstream: 'ec8ecbaec0c9c5094b6b1480df0d6b2d32d6349b', provenance, palettes, cases }));
console.log(`Wrote ${cases.reduce((n,c) => n+c.frames.length,0)} original AudioEngine frames to ${output}`);
