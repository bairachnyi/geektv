#!/usr/bin/env node
import { mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';

const base = new URL(process.argv[2] || 'http://192.168.1.141/');
if (!['http:', 'https:'].includes(base.protocol)) throw new Error('Expected an http(s) device URL.');

const output = resolve(process.argv[3] || 'research/stock-v9.0.51/raw');
const pages = ['network.html', 'weather.html', 'time.html', 'image.html', 'settings.html'];
const assets = [
  'css/style.css',
  'css/cropper.min.css',
  'js/settings.js',
  'js/jquery.min.js',
  'js/cropper.min.js',
];
const jsonEndpoints = [
  'v.json', 'brt.json', 'delay.json', 'app.json', 'timebrt.json', 'theme_list.json',
  'config.json', 'city.json', 'w_i.json', 'unit.json', 'key.json', 'hour12.json',
  'ntp.json', 'day.json', 'timecolor.json', 'font.json', 'colon.json',
  'daytimer.json', 'tz.json', 'album.json', 'space.json',
];
const sensitiveKeys = new Set([
  'a', 'p', 'pwd', 'password', 'ssid', 'key', 'fkey', 'api', 'forecastapi',
  'cd', 'cd1', 'cd2', 'ct', 'loc', 'city', 'location',
]);
const execFileAsync = promisify(execFile);

async function get(path) {
  const url = new URL(path, base);
  try {
    const response = await fetch(url, { headers: { Accept: '*/*', 'User-Agent': 'smalltv-stock-snapshot' } });
    if (!response.ok) throw new Error(`${url.pathname}: HTTP ${response.status}`);
    return { url: url.toString(), contentType: response.headers.get('content-type') || '', body: await response.text() };
  } catch (error) {
    // Some V9.0.51 file-list responses are accepted by browsers/curl but do not
    // satisfy Node's strict HTTP parser. Curl is a read-only compatibility fallback.
    if (!String(path).startsWith('filelist?')) throw error;
    const { stdout } = await execFileAsync('curl', ['-sS', '--compressed', '--max-time', '15', url.toString()], { maxBuffer: 2 * 1024 * 1024 });
    return { url: url.toString(), contentType: 'text/html', body: stdout };
  }
}

function redact(value) {
  if (Array.isArray(value)) return value.map(redact);
  if (!value || typeof value !== 'object') return value;
  return Object.fromEntries(Object.entries(value).map(([key, item]) => [
    key,
    sensitiveKeys.has(key.toLowerCase()) ? '[REDACTED]' : redact(item),
  ]));
}

await mkdir(output, { recursive: true });
const manifest = {
  capturedAt: new Date().toISOString(),
  source: base.origin,
  readOnly: true,
  pages: [],
  assets: [],
  state: [],
  fileLists: [],
};

for (const path of [...pages, ...assets]) {
  const result = await get(path);
  const destination = resolve(output, path);
  await mkdir(resolve(destination, '..'), { recursive: true });
  await writeFile(destination, result.body);
  const entry = { path, bytes: Buffer.byteLength(result.body), contentType: result.contentType };
  (pages.includes(path) ? manifest.pages : manifest.assets).push(entry);
}

const state = {};
for (const path of jsonEndpoints) {
  try {
    const result = await get(path);
    const parsed = JSON.parse(result.body);
    state[path] = redact(parsed);
    manifest.state.push({ path, ok: true });
  } catch (error) {
    manifest.state.push({ path, ok: false, error: error.message });
  }
}
await writeFile(resolve(output, 'state-sanitized.json'), `${JSON.stringify(state, null, 2)}\n`);

for (const directory of ['/image/', '/gif']) {
  try {
    // The stock ESP8266 handler expects literal slashes in this query value.
    const result = await get(`filelist?dir=${directory}`);
    const name = directory.replaceAll('/', '') || 'root';
    await writeFile(resolve(output, `file-list-${name}.html`), result.body);
    manifest.fileLists.push({ directory, ok: true, bytes: Buffer.byteLength(result.body) });
  } catch (error) {
    manifest.fileLists.push({ directory, ok: false, error: error.message });
  }
}

await writeFile(resolve(output, 'manifest.json'), `${JSON.stringify(manifest, null, 2)}\n`);
console.log(`Saved read-only stock snapshot to ${output}`);
console.log(`Pages: ${manifest.pages.length}; assets: ${manifest.assets.length}; state endpoints: ${manifest.state.filter(x => x.ok).length}/${manifest.state.length}`);
