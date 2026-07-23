#!/usr/bin/env node
import { mkdir } from 'node:fs/promises';
import { resolve } from 'node:path';
import { createRequire } from 'node:module';
import { execFile } from 'node:child_process';
import { promisify } from 'node:util';

const requireFromDocs = createRequire(new URL('../docs/package.json', import.meta.url));
const sharp = requireFromDocs('sharp');
const execFileAsync = promisify(execFile);

const args = process.argv.slice(2);
const demo = args.includes('--demo');
const feedArg = args.find((arg, index) => !arg.startsWith('--') && args[index - 1] !== '--output')
  || 'http://localhost:8788/api/github';
const outputArg = args.find((arg, index) => args[index - 1] === '--output');
const output = resolve(outputArg || 'research/stock-v9.0.51/raw/github-album');
const framesPerScene = 16;
const fps = 4;

const activeDemo = [
  event('ananas-it/customer-portal-production', 'deployment', 'Deploy production with database migrations', 'feature/github-deployment-dashboard', 'in_progress', '', '23 JUL 14:41', true, 51),
  event('ananas-it/mobile', 'pull_request', 'PR #82 checks and quality gates', 'feature/login-redesign', 'queued', '', '23 JUL 14:40', false, 18),
];
const historyDemo = [
  event('bairachnyi/smalltv-ultra', 'action', 'Build firmware and emulator', 'main', 'completed', 'success', '23 JUL 14:39', true),
  event('ananas-it/api', 'action', 'Tests and production build', 'release/v4', 'completed', 'failure', '23 JUL 14:37'),
  event('ananas-it/infrastructure', 'deployment', 'Deploy staging environment', 'main', 'completed', 'success', '23 JUL 14:31'),
  event('ananas-it/web', 'release', 'Release v3.8.0', 'main', 'completed', 'success', '23 JUL 14:22'),
  event('ananas-it/mobile', 'pull_request', 'PR #79 checks', 'fix/push-notifications', 'completed', 'failure', '23 JUL 14:11'),
  event('bairachnyi/portfolio', 'deployment', 'Deploy production', 'main', 'completed', 'success', '23 JUL 13:58'),
];

function event(repo, type, workflow, branch, status, conclusion, when, latest = false, age = 0) {
  return { repo, type, workflow, branch, status, conclusion, when, latest, age };
}

function xml(value) {
  return String(value || '').replace(/[&<>"']/g, char => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&apos;',
  })[char]);
}

function isActive(item) {
  return ['in_progress', 'running', 'queued', 'waiting', 'pending'].includes(item.status);
}

function state(item) {
  if (['in_progress', 'running'].includes(item.status)) return { color: '#39e7ff', label: 'RUN' };
  if (['queued', 'waiting', 'pending'].includes(item.status)) return { color: '#ffb627', label: 'WAIT' };
  if (item.conclusion === 'success' || item.status === 'success') return { color: '#59ef9a', label: 'PASS' };
  if (item.conclusion === 'failure' || item.status === 'failure') return { color: '#ff5d68', label: 'FAIL' };
  return { color: '#718096', label: 'STOP' };
}

function typeLabel(type) {
  return ({ action: 'ACT', deployment: 'DEP', pull_request: 'PR', release: 'REL' })[type] || 'ACT';
}

function marquee(value, visible, phase) {
  const text = String(value || '');
  if (text.length <= visible) return text;
  const gap = '   ';
  const loop = text + gap;
  const hold = 4;
  const offset = phase < hold ? 0 : (phase - hold) % loop.length;
  let out = '';
  for (let i = 0; i < visible; i++) out += loop[(offset + i) % loop.length];
  return out;
}

function elapsed(item, phase) {
  if (!isActive(item)) return state(item).label;
  const seconds = Math.max(0, Number(item.age || 0) + Math.floor(phase / fps));
  return `${String(Math.floor(seconds / 60)).padStart(2, '0')}:${String(seconds % 60).padStart(2, '0')}`;
}

function iconSvg(item, y, phase) {
  const current = state(item);
  const cx = 20;
  const cy = y + 19;
  if (current.label === 'PASS') {
    return `<path d="M12 ${cy} l5 5 l10 -12" fill="none" stroke="${current.color}" stroke-width="3.2" stroke-linecap="round" stroke-linejoin="round"/>`;
  }
  if (current.label === 'FAIL') {
    return `<circle cx="${cx}" cy="${cy}" r="10" fill="${current.color}"/><path d="M20 ${cy - 5}v7 M20 ${cy + 6}h.1" stroke="#050a12" stroke-width="3" stroke-linecap="round"/>`;
  }
  const dash = current.label === 'WAIT' ? '3 4' : '35 25';
  const angle = (phase * 42) % 360;
  return `<g transform="rotate(${angle} ${cx} ${cy})"><circle cx="${cx}" cy="${cy}" r="10" fill="none" stroke="#294657" stroke-width="3"/><circle cx="${cx}" cy="${cy}" r="10" fill="none" stroke="${current.color}" stroke-width="3" stroke-dasharray="${dash}"/></g>`;
}

function counts(items) {
  const result = { run: 0, pass: 0, fail: 0 };
  for (const item of items) {
    const label = state(item).label;
    if (label === 'RUN' || label === 'WAIT') result.run++;
    if (label === 'PASS') result.pass++;
    if (label === 'FAIL') result.fail++;
  }
  return result;
}

function render(scene, phase) {
  const summary = counts(scene.summaryItems);
  const rows = scene.items.map((item, row) => {
    const current = state(item);
    const y = 55 + row * 90;
    const repo = marquee(String(item.repo || '').split('/').pop(), 10, phase + row * 2);
    const workflow = marquee(item.workflow, 18, phase + row * 2);
    const branch = marquee(item.branch, 18, phase + row * 2);
    const latestStroke = item.latest ? '#39e7ff' : '#193047';
    return `
      <rect x="4" y="${y}" width="232" height="84" rx="7" fill="#0d1828" stroke="${latestStroke}" stroke-width="${item.latest ? 2 : 1}"/>
      ${item.latest ? `<rect x="7" y="${y + 3}" width="226" height="78" rx="5" fill="none" stroke="#39e7ff" stroke-opacity=".35"/>` : ''}
      <rect x="4" y="${y + 7}" width="4" height="70" rx="2" fill="${current.color}"/>
      ${iconSvg(item, y, phase)}
      <text x="39" y="${y + 21}" class="primary">${xml(repo)}</text>
      <text x="230" y="${y + 21}" text-anchor="end" class="status" fill="${current.color}">${xml(elapsed(item, phase))}</text>
      <text x="12" y="${y + 43}" class="secondary">${xml(workflow)}</text>
      <text x="12" y="${y + 63}" class="secondary dim">${xml(branch)}</text>
      <rect x="9" y="${y + 65}" width="39" height="16" rx="3" fill="#193047"/>
      <text x="28.5" y="${y + 78}" text-anchor="middle" class="secondary" fill="${current.color}">${typeLabel(item.type)}</text>
      <text x="232" y="${y + 78}" text-anchor="end" class="secondary">${xml(item.when || '-- --- --:--')}</text>`;
  }).join('');

  const focusColor = scene.focus ? '#ffb627' : '#39e7ff';
  const focusLabel = scene.focus ? 'FOCUS' : 'LIVE';
  return `<svg xmlns="http://www.w3.org/2000/svg" width="240" height="240" viewBox="0 0 240 240">
    <rect width="240" height="240" fill="#050a12"/>
    <style>
      text{font-family:Menlo,Monaco,monospace}.head{font-size:17px;font-weight:800;fill:#f2f6fa}
      .tiny{font-size:8px;fill:#718096}.summary{font-size:13px;font-weight:800}
      .primary{font-size:14px;font-weight:800;fill:#f2f6fa}.secondary{font-size:12px;fill:#f2f6fa}
      .secondary.dim{fill:#8ba0ae}.status{font-size:13px;font-weight:800}
    </style>
    <text x="7" y="21" class="head">GH<tspan fill="#39e7ff">//</tspan>STAT</text>
    <circle cx="174" cy="14" r="3" fill="${focusColor}"/><text x="181" y="17" class="tiny" fill="${focusColor}">${focusLabel}</text>
    <text x="232" y="17" text-anchor="end" class="tiny">${scene.page}/${scene.pages}</text>
    <line x1="6" y1="29" x2="234" y2="29" stroke="#193047"/><line x1="6" y1="29" x2="72" y2="29" stroke="#39e7ff"/>
    <circle cx="8" cy="44" r="4" fill="#39e7ff"/><text x="16" y="48" class="summary" fill="#718096">R <tspan fill="#f2f6fa">${summary.run}</tspan></text>
    <circle cx="83" cy="44" r="4" fill="#59ef9a"/><text x="91" y="48" class="summary" fill="#718096">P <tspan fill="#f2f6fa">${summary.pass}</tspan></text>
    <circle cx="164" cy="44" r="4" fill="#ff5d68"/><text x="172" y="48" class="summary" fill="#718096">F <tspan fill="#f2f6fa">${summary.fail}</tspan></text>
    ${rows}
  </svg>`;
}

let activeItems = activeDemo;
let historyItems = historyDemo;
if (!demo) {
  const response = await fetch(feedArg, { headers: { Accept: 'application/json' } });
  if (!response.ok) throw new Error(`GitHub feed returned HTTP ${response.status}`);
  const data = await response.json();
  if (data.ok === false) throw new Error(data.error?.message || 'GitHub feed failed.');
  const items = (data.items || data.runs || []).slice(0, 8);
  if (!items.length) throw new Error('GitHub feed contains no events. Use --demo to test the renderer.');
  activeItems = items.filter(isActive);
  historyItems = items.filter(item => !isActive(item));
  if (!activeItems.length) historyItems = items;
}

const scenes = [];
if (activeItems.length) {
  for (let page = 0; page < Math.ceil(activeItems.length / 2); page++) {
    scenes.push({
      items: activeItems.slice(page * 2, page * 2 + 2),
      summaryItems: [...activeItems, ...historyItems],
      focus: true,
      page: page + 1,
      pages: Math.ceil(activeItems.length / 2),
    });
  }
}
if (historyItems.length) {
  for (let page = 0; page < Math.ceil(historyItems.length / 2); page++) {
    scenes.push({
      items: historyItems.slice(page * 2, page * 2 + 2),
      summaryItems: historyItems,
      focus: false,
      page: page + 1,
      pages: Math.ceil(historyItems.length / 2),
    });
  }
}

await mkdir(output, { recursive: true });
const frameFiles = [];
let frameNumber = 0;
for (let sceneIndex = 0; sceneIndex < scenes.length; sceneIndex++) {
  const preview = resolve(output, `github-status-preview-${sceneIndex + 1}.png`);
  await sharp(Buffer.from(render(scenes[sceneIndex], 0))).png().toFile(preview);
  for (let phase = 0; phase < framesPerScene; phase++) {
    const destination = resolve(output, `github-status-frame-${String(frameNumber++).padStart(3, '0')}.png`);
    await sharp(Buffer.from(render(scenes[sceneIndex], phase))).png().toFile(destination);
    frameFiles.push(destination);
  }
}

const gif = resolve(output, 'github-status.gif');
await execFileAsync('ffmpeg', [
  '-hide_banner', '-loglevel', 'error', '-y',
  '-framerate', String(fps), '-i', resolve(output, 'github-status-frame-%03d.png'),
  '-filter_complex',
  '[0:v]split[s0][s1];[s0]palettegen=max_colors=96:stats_mode=diff[p];[s1][p]paletteuse=dither=bayer:bayer_scale=3:diff_mode=rectangle',
  '-loop', '0', gif,
]);

console.log(`Rendered ${scenes.length} screens and ${frameFiles.length} animation frames to ${gif}`);
console.log('Layout: two large events per screen; active events stay in FOCUS; newest event has a cyan frame.');
console.log('No file was uploaded to the SmallTV.');
