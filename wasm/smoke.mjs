// Headless browser smoke test: load a ROM on the served page, run until N frames, screenshot the canvas.
// Usage: node wasm/smoke.mjs <rom path relative to site, e.g. roms/ex_hello-world.3dsx> [frames=120] [out.png] [keys]
// keys: optional "frame:key:down|up,..." script, e.g. "60:1:1,70:1:0" (HID key bitmask values)
import { chromium } from 'playwright';

const [rom, framesArg = '120', out = 'smoke.png', keys = ''] = process.argv.slice(2);
const frames = Number(framesArg);
const keyScript = keys ? keys.split(',').map((k) => k.split(':').map(Number)) : [];

// Reuse an already-downloaded headless shell if this playwright's pinned one is missing
const fs = await import('node:fs');
const os = await import('node:os');
const cache = `${os.homedir()}/Library/Caches/ms-playwright`;
const shells = fs.existsSync(cache) ? fs.readdirSync(cache).filter((d) => d.startsWith('chromium_headless_shell-')).sort().reverse() : [];
const executablePath = process.env.CHROME_PATH ?? shells.map((d) => `${cache}/${d}/chrome-mac/headless_shell`).concat(shells.map((d) => `${cache}/${d}/chrome-headless-shell-mac-arm64/chrome-headless-shell`)).find((p) => fs.existsSync(p));
const browser = await chromium.launch({ headless: true, executablePath, args: ['--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
const page = await browser.newPage({ viewport: { width: 700, height: 760 } });
const logs = [];
page.on('console', (m) => logs.push(m.text()));
page.on('pageerror', (e) => logs.push('PAGEERROR ' + e.message));

const t0 = Date.now();
await page.goto(`http://127.0.0.1:8301/index.html?rom=${encodeURIComponent(rom)}&env=${encodeURIComponent(process.env.PANDA_ENV ?? '')}`);
try {
  await page.waitForFunction(() => window.pandaReady === true, null, { timeout: 60000 });
} catch (e) {
  console.log(logs.join('\n'));
  console.log('[smoke] page never became ready');
  await page.screenshot({ path: out });
  await browser.close();
  process.exit(1);
}

let lastFrame = -1, stalled = 0;
while (true) {
  const f = await page.evaluate(() => Module._panda_frames());
  for (const [at, key, down] of keyScript) {
    if (at > lastFrame && at <= f) await page.evaluate(([k, d]) => Module._panda_set_key(k, d), [key, down]);
  }
  if (f >= frames) break;
  stalled = f === lastFrame ? stalled + 1 : 0;
  if (stalled > 100) { logs.push(`STALLED at frame ${f}`); break; }
  lastFrame = f;
  await page.waitForTimeout(100);
}
const secs = (Date.now() - t0) / 1000;
const info = await page.evaluate(() => ({ frames: Module._panda_frames(), pc: Module._panda_pc().toString(16) }));
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
console.log(`[smoke] ${rom}: frames=${info.frames} pc=${info.pc} wall=${secs.toFixed(1)}s fps=${(info.frames / secs).toFixed(1)} canvasColors=${colors} -> ${out}`);
await browser.close();
