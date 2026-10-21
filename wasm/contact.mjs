// Tile every PNG in a directory into one labelled contact sheet. Usage: node wasm/contact.mjs <dir> <out.png>
import { chromium } from 'playwright';
import fs from 'node:fs';
import path from 'node:path';
import os from 'node:os';

const [dir, out] = process.argv.slice(2);
const files = fs.readdirSync(dir).filter((f) => f.endsWith('.png') && !f.startsWith('contact')).sort();
const cells = files.map((f) => `<figure><img src="data:image/png;base64,${fs.readFileSync(path.join(dir, f)).toString('base64')}"><figcaption>${f}</figcaption></figure>`).join('');
const html = `<body style="margin:0;background:#222;color:#eee;font:12px sans-serif;display:flex;flex-wrap:wrap;gap:8px;padding:8px;width:${5 * 208}px">
<style>figure{margin:0;width:200px}img{width:200px;height:240px;display:block}</style>${cells}</body>`;
const cache = `${os.homedir()}/Library/Caches/ms-playwright`;
const shell = fs.readdirSync(cache).filter((d) => d.startsWith('chromium_headless_shell-')).sort().reverse()[0];
const browser = await chromium.launch({ executablePath: process.env.CHROME_PATH ?? `${cache}/${shell}/chrome-headless-shell-mac-arm64/chrome-headless-shell` });
const page = await browser.newPage({ viewport: { width: 5 * 208 + 16, height: 600 } });
await page.setContent(html);
await page.screenshot({ path: out, fullPage: true });
await browser.close();
