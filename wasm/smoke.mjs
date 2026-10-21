// Headless browser smoke test: load a ROM on the served page, run until N frames, screenshot the canvas.
// Usage: node wasm/smoke.mjs <rom path relative to site, e.g. roms/ex_hello-world.3dsx> [frames=120] [out.png] [keys]
// keys: optional "frame:key:down|up,..." script. key is a HID bitmask ("60:1:1,70:1:0"), a keyboard key name
// sent as a real DOM event ("60:l:1"), or "tap" with canvas coords as down value ("60:tap:200/360")
import { chromium } from 'playwright';

const [rom, framesArg = '120', out = 'smoke.png', keys = ''] = process.argv.slice(2);
const frames = Number(framesArg);
const keyScript = keys ? keys.split(',').map((k) => { const [at, key, down] = k.split(':'); return [Number(at), key, down]; }) : [];

// Reuse an already-downloaded headless shell if this playwright's pinned one is missing
const fs = await import('node:fs');
const os = await import('node:os');
const cache = `${os.homedir()}/Library/Caches/ms-playwright`;
const shells = fs.existsSync(cache) ? fs.readdirSync(cache).filter((d) => d.startsWith('chromium_headless_shell-')).sort().reverse() : [];
const executablePath = process.env.CHROME_PATH ?? shells.map((d) => `${cache}/${d}/chrome-mac/headless_shell`).concat(shells.map((d) => `${cache}/${d}/chrome-headless-shell-mac-arm64/chrome-headless-shell`)).find((p) => fs.existsSync(p));
const launchOptions = { headless: true, executablePath, args: ['--autoplay-policy=no-user-gesture-required', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] };
// SMOKE_PROFILE=<dir> keeps IndexedDB between runs, to test save persistence
const browser = process.env.SMOKE_PROFILE
  ? await chromium.launchPersistentContext(process.env.SMOKE_PROFILE, { ...launchOptions, viewport: { width: 700, height: 760 } })
  : await chromium.launch(launchOptions);
const page = process.env.SMOKE_PROFILE ? await browser.newPage() : await browser.newPage({ viewport: { width: 700, height: 760 } });
// Record peak output level of every ScriptProcessorNode so tests can tell audio is actually produced
await page.addInitScript(() => {
  window.pandaAudioPeak = 0;
  window.pandaAudioCalls = 0;
  const desc = Object.getOwnPropertyDescriptor(ScriptProcessorNode.prototype, 'onaudioprocess');
  Object.defineProperty(ScriptProcessorNode.prototype, 'onaudioprocess', {
    set(fn) {
      desc.set.call(this, (e) => {
        fn(e);
        window.pandaAudioCalls++;
        for (let c = 0; c < e.outputBuffer.numberOfChannels; c++) {
          const d = e.outputBuffer.getChannelData(c);
          for (let i = 0; i < d.length; i++) window.pandaAudioPeak = Math.max(window.pandaAudioPeak, Math.abs(d[i]));
        }
      });
    },
    get() { return desc.get.call(this); },
  });
});
const logs = [];
page.on('console', (m) => logs.push(m.text()));
page.on('pageerror', (e) => logs.push('PAGEERROR ' + e.message));

const t0 = Date.now();
// SMOKE_FILE=1: treat <rom> as a local path and load it through the page's file picker, like a user would
const viaPicker = !!process.env.SMOKE_FILE;
await page.goto(`http://127.0.0.1:8301/index.html?rom=${viaPicker ? '' : encodeURIComponent(rom)}&env=${encodeURIComponent(process.env.PANDA_ENV ?? '')}&sd=${encodeURIComponent(process.env.SMOKE_SD ?? '')}`);
try {
  await page.waitForFunction(() => window.pandaReady === true, null, { timeout: 60000 });
} catch (e) {
  console.log(logs.join('\n'));
  console.log('[smoke] page never became ready');
  await page.screenshot({ path: out });
  await browser.close();
  process.exit(1);
}

// SMOKE_PROFILE_CPU=1: sample the page with the V8 profiler and print the top self-time functions
const cdp = process.env.SMOKE_PROFILE_CPU ? await page.context().newCDPSession(page) : null;
if (cdp) {
  await cdp.send('Profiler.enable');
  await cdp.send('Profiler.setSamplingInterval', { interval: 200 });
  await cdp.send('Profiler.start');
}

if (viaPicker) {
  await page.locator('#file').setInputFiles(rom);
  await page.waitForFunction(() => document.getElementById('status').textContent.startsWith('running') || document.getElementById('status').textContent.startsWith('failed'), null, { timeout: 300000 });
  logs.push('[smoke] picker load took ' + ((Date.now() - t0) / 1000).toFixed(1) + 's: ' + (await page.locator('#status').textContent()));
}

let lastFrame = -1, stalled = 0;
while (true) {
  const f = await page.evaluate(() => Module._panda_frames());
  for (const [at, key, down] of keyScript) {
    if (!(at > lastFrame && at <= f)) continue;
    if (key === 'tap') {
      const [x, y] = down.split('/').map(Number);
      const box = await page.locator('#canvas').boundingBox();
      await page.mouse.click(box.x + x, box.y + y, { delay: 100 });
    } else if (isNaN(Number(key))) {
      await page.locator('#canvas').focus();
      await (down === '1' ? page.keyboard.down(key) : page.keyboard.up(key));
    } else {
      await page.evaluate(([k, d]) => Module._panda_set_key(k, d), [Number(key), Number(down)]);
    }
  }
  if (f >= frames) break;
  stalled = f === lastFrame ? stalled + 1 : 0;
  if (stalled > 100) { logs.push(`STALLED at frame ${f}`); break; }
  lastFrame = f;
  await page.waitForTimeout(100);
}
const secs = (Date.now() - t0) / 1000;
if (cdp) {
  const { profile } = await cdp.send('Profiler.stop');
  const self = new Map();
  const byId = new Map(profile.nodes.map((n) => [n.id, n]));
  const dt = profile.timeDeltas;
  profile.samples.forEach((id, i) => {
    const n = byId.get(id);
    const name = n.callFrame.functionName || '(anon)';
    self.set(name, (self.get(name) || 0) + (dt[i] || 0));
  });
  const total = [...self.values()].reduce((a, b) => a + b, 0);
  const top = [...self.entries()].sort((a, b) => b[1] - a[1]).slice(0, 30);
  fs.writeFileSync(out.replace(/\.png$/, '') + '.profile.txt', top.map(([n, t]) => `${((100 * t) / total).toFixed(1).padStart(5)}%  ${n}`).join('\n'));
}
await page.evaluate(() => window.pandaSync());
const info = await page.evaluate(() => ({ panics: Module._panda_panics(), emuMs: Module._panda_emu_ms(), frames: Module._panda_frames(), pc: Module._panda_pc().toString(16), audioPeak: window.pandaAudioPeak.toFixed(3), audioCalls: window.pandaAudioCalls, audioState: [...(window.pandaAudioContexts || [])].map((c) => c.state).join('/') }));
await page.locator('#canvas').screenshot({ path: out });
const colors = await page.evaluate(async () => {
  const c = document.getElementById('canvas');
  const img = new Image();
  img.src = c.toDataURL();
  await img.decode();
  const cv = new OffscreenCanvas(c.width, c.height);
  const ctx = cv.getContext('2d');
  ctx.drawImage(img, 0, 0);
  const d = ctx.getImageData(0, 0, c.width, c.height).data;
  const set = new Set();
  for (let i = 0; i < d.length; i += 4) set.add((d[i] << 16) | (d[i + 1] << 8) | d[i + 2]);
  return set.size;
});
fs.writeFileSync(out.replace(/\.png$/, '') + '.log', logs.join('\n'));
console.log(logs.filter((l) => !l.includes('GPU stall')).slice(-25).join('\n'));
console.log(`[smoke] ${rom}: frames=${info.frames} panics=${info.panics} pc=${info.pc} wall=${secs.toFixed(1)}s fps=${(info.frames / secs).toFixed(1)} emuFps=${(info.frames / (info.emuMs / 1000)).toFixed(1)} canvasColors=${colors} audio=${info.audioPeak}/${info.audioCalls}cb/${info.audioState} -> ${out}`);
await browser.close();
