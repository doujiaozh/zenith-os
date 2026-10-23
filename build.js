#!/usr/bin/env node
/*
 * ============================================================================
 *  ZenithOS build system
 * ============================================================================
 *
 *  node build.js [options]
 *
 *    --no-check       skip the per-module NASM compile gate
 *    --quiet          only print the final result
 *    --verbose        echo every NASM invocation
 *
 *  Pipeline
 *  --------
 *   1. locate NASM (env NASM, ./nasm.exe, PATH, common install dirs)
 *   2. generate build/generated/trig.inc (Q8.8 sin/cos table, 256 steps)
 *   3. compile EVERY module in src/ as a relocatable object (-f obj) with
 *      ZENITH_OBJ_CHECK so cross-module dependencies must be declared:
 *      this is the project's "does every module stand on its own?" gate
 *   4. assemble boot.asm (512-byte real-mode bootstrap)
 *   5. assemble the umbrella src/kernel.asm into a flat 32-bit kernel binary
 *   6. validate sizes / signatures / kernel budget
 *   7. pack build/zenithos.img (1,474,560 bytes) and zenithos.img in the root
 *   8. read the image back, verify every payload byte, print SHA-256
 *   9. write build/report.json for CI / provenance
 *
 *  No npm dependencies. Node 14+ (tested on 18.x), NASM 2.13+.
 * ============================================================================
 */

'use strict';

const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { spawnSync } = require('child_process');

const ROOT = __dirname;
const BUILD = path.join(ROOT, 'build');
const OBJ = path.join(BUILD, 'obj');
const GEN = path.join(BUILD, 'generated');
const SRC = path.join(ROOT, 'src');
const BOOT_SOURCE = 'boot.asm';
const KERNEL_SOURCE = path.join(SRC, 'kernel.asm');
const IMAGE_NAME = 'zenithos.img';
const FLOPPY_SIZE = 1440 * 1024;
const SECTOR = 512;
const INCLUDE_DIRS = [SRC, GEN];

const argv = new Set(process.argv.slice(2));
const OPT = {
  check: !argv.has('--no-check'),
  quiet: argv.has('--quiet'),
  verbose: argv.has('--verbose'),
};

let NASM = null;
const report = {
  project: 'ZenithOS',
  version: '1.0.0',
  started_utc: new Date().toISOString(),
  nasm: null,
  modules: [],
  artifacts: {},
  image: {},
  checks: {},
  trig_table: {},
};

function log(message) {
  if (!OPT.quiet) process.stdout.write(message + '\n');
}

function verbose(message) {
  if (OPT.verbose) process.stdout.write(message + '\n');
}

function fail(message) {
  process.stderr.write('\n[build] FATAL: ' + message + '\n');
  process.exit(1);
}

function mkdirp(dir) {
  fs.mkdirSync(dir, { recursive: true });
}

function sha256(buffer) {
  return crypto.createHash('sha256').update(buffer).digest('hex');
}

function walk(dir, predicate) {
  const out = [];
  for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
    const full = path.join(dir, entry.name);
    if (entry.isDirectory()) out.push(...walk(full, predicate));
    else if (predicate(full)) out.push(full);
  }
  return out;
}

/* ------------------------------------------------------------------------- */
/* toolchain                                                                  */
/* ------------------------------------------------------------------------- */
function findNasm() {
  const candidates = [];
  if (process.env.NASM) candidates.push(process.env.NASM);
  candidates.push(
    path.join(ROOT, 'nasm.exe'),
    path.join(ROOT, 'nasm', 'nasm.exe'),
    path.join(ROOT, 'bin', 'nasm.exe'),
    'C:\\Program Files\\NASM\\nasm.exe',
    'C:\\Program Files (x86)\\NASM\\nasm.exe',
    'C:\\NASM\\nasm.exe',
    'nasm'
  );
  for (const candidate of candidates) {
    if (candidate !== 'nasm' && !fs.existsSync(candidate)) continue;
    const probe = spawnSync(candidate, ['-v'], { cwd: ROOT, encoding: 'utf8', windowsHide: true });
    if (probe.error === undefined && probe.status === 0) {
      report.nasm = (probe.stdout || '').trim();
      return candidate;
    }
  }
  fail('NASM not found. Put nasm.exe next to build.js, add it to PATH, or set NASM=<path>.');
}

function runNasm(args, what) {
  const full = [NASM, ...args];
  verbose('$ ' + full.join(' '));
  const result = spawnSync(full[0], full.slice(1), {
    cwd: ROOT,
    encoding: 'utf8',
    windowsHide: true,
  });
  if (result.error) fail('could not start NASM for ' + what + ': ' + result.error.message);
  const output = ((result.stdout || '') + (result.stderr || '')).trim();
  if (result.status !== 0) {
    process.stderr.write('\n--- NASM output (' + what + ') ---\n' + output + '\n');
    fail('NASM rejected ' + what + ' (exit ' + result.status + ')');
  }
  if (output) {
    const warnings = output.split(/\r?\n/).filter((line) => /warning/i.test(line));
    if (warnings.length) process.stdout.write('[nasm] warnings for ' + what + ':\n' + warnings.join('\n') + '\n');
  }
}

/* ------------------------------------------------------------------------- */
/* config parsing                                                             */
/* ------------------------------------------------------------------------- */
function readConfigConstant(name) {
  const text = fs.readFileSync(path.join(SRC, 'include', 'config.inc'), 'utf8');
  const match = text.match(new RegExp('^\\s*' + name + '\\s+equ\\s+([^;\\s]+)', 'm'));
  if (!match) fail('config.inc: missing constant ' + name);
  const value = match[1].trim();
  if (/^0x[0-9a-f]+$/i.test(value)) return parseInt(value, 16);
  if (/^[0-9]+$/.test(value)) return parseInt(value, 10);
  const expr = new Function('return (' + value + ');');
  return expr();
}

/* ------------------------------------------------------------------------- */
/* generated assets: trigonometry + palette                                   */
/* ------------------------------------------------------------------------- */
function clampChannel(value) {
  return Math.max(0, Math.min(63, Math.round(value)));
}

function generatePalette() {
  const palette = new Array(256);
  for (let i = 0; i < 256; i++) palette[i] = [0, 0, 0];
  const set = (index, rgb) => { palette[index] = rgb; };
  const ramp = (first, count, from, to) => {
    for (let i = 0; i < count; i++) {
      const t = count === 1 ? 0 : i / (count - 1);
      set(first + i, from.map((v, c) => clampChannel(v + (to[c] - v) * t)));
    }
  };

  // Fixed slots: stable across every palette so panic output is always legible.
  set(1, [63, 63, 63]);      // white
  set(2, [0, 56, 63]);       // cyan
  set(3, [58, 0, 58]);       // magenta
  set(4, [63, 0, 0]);        // red
  set(5, [0, 50, 10]);       // green
  set(6, [63, 32, 0]);       // amber
  set(7, [0, 0, 10]);        // background
  set(8, [16, 18, 28]);      // dim
  set(9, [0, 32, 63]);       // blue
  set(10, [32, 0, 63]);      // violet
  set(11, [63, 0, 32]);      // hot pink
  set(12, [63, 0, 14]);      // bright red
  set(13, [0, 18, 18]);      // teal
  set(14, [26, 34, 44]);     // steel
  set(15, [63, 63, 63]);     // white (second slot, panic-safe)

  // Neon ramps consumed by the renderer role constants.
  ramp(16, 16, [0, 12, 44], [0, 52, 63]);     // node: deep blue -> cyan
  ramp(32, 16, [0, 52, 63], [63, 63, 63]);    // node: cyan -> white
  ramp(48, 16, [14, 0, 30], [54, 0, 62]);     // edge: violet
  ramp(64, 16, [54, 0, 62], [63, 36, 63]);    // edge: magenta -> pink
  ramp(80, 16, [0, 5, 10], [0, 26, 38]);      // floor grid
  ramp(96, 16, [0, 38, 50], [0, 58, 63]);     // UI cyan
  ramp(112, 16, [22, 0, 38], [50, 0, 63]);    // log violet
  ramp(128, 16, [0, 22, 8], [0, 58, 30]);     // green status
  ramp(144, 16, [30, 14, 0], [63, 46, 0]);    // amber warning
  ramp(160, 16, [30, 0, 0], [63, 8, 8]);      // red error
  ramp(176, 16, [0, 0, 5], [7, 9, 16]);       // near-black background shades
  ramp(192, 64, [0, 0, 4], [10, 12, 24]);     // unused slots, keep it dark

  const rows = [];
  for (let i = 0; i < 256; i += 16) {
    const chunk = palette.slice(i, i + 16).map((rgb) => rgb.join(', '));
    rows.push('    db ' + chunk.join(', '));
  }
  const text = [
    '; ===========================================================================',
    ';  ZenithOS - generated palette (DO NOT EDIT)',
    ';  Generated by build.js from the ramp declarations.  Role constants live in',
    ';  src/include/config.inc; slots 0..15 are stable so the panic report is',
    ';  readable whether it fires before or after theme_init.',
    '; ===========================================================================',
    '%ifndef ZENITH_PALETTE_INC',
    '%define ZENITH_PALETTE_INC',
    'align 4',
    'theme_palette:',
    ...rows,
    '%endif',
    '',
  ].join('\n');
  mkdirp(GEN);
  const file = path.join(GEN, 'palette.inc');
  fs.writeFileSync(file, text);
  report.palette = {
    file: path.relative(ROOT, file).replace(/\\/g, '/'),
    entries: 256,
    bytes: 768,
    sha256: sha256(fs.readFileSync(file)),
  };
  log('[gen]  palette.inc  256 RGB entries (768 B)  sha256=' + report.palette.sha256.slice(0, 16));
}

function generateTrig() {
  // 256 angular steps over a full turn, Q8.8 fixed point (-256..256).
  // The UI keeps camera angles in 16-bit binary radians and indexes this table
  // with the top 8 bits, which makes rotation a single multiply per axis.
  const cos = [];
  const sin = [];
  for (let i = 0; i < 256; i++) {
    const angle = (i * 2 * Math.PI) / 256;
    cos.push(Math.round(Math.cos(angle) * 256));
    sin.push(Math.round(Math.sin(angle) * 256));
  }
  const fmt = (values) => {
    const rows = [];
    for (let i = 0; i < values.length; i += 8) {
      rows.push('    dw ' + values.slice(i, i + 8).join(', '));
    }
    return rows.join('\n');
  };
  const text = [
    '; ===========================================================================',
    ';  ZenithOS - generated trigonometry table (DO NOT EDIT)',
    ';  Generated by build.js: 256 steps, one full turn, Q8.8 fixed point.',
    ';  trig_sin[i] = round(sin(2*pi*i/256) * 256)',
    '; ===========================================================================',
    '%ifndef ZENITH_TRIG_INC',
    '%define ZENITH_TRIG_INC',
    'align 16',
    'trig_cos:',
    fmt(cos),
    'align 16',
    'trig_sin:',
    fmt(sin),
    '%endif',
    '',
  ].join('\n');
  mkdirp(GEN);
  const file = path.join(GEN, 'trig.inc');
  fs.writeFileSync(file, text);
  report.trig_table = {
    file: path.relative(ROOT, file).replace(/\\/g, '/'),
    entries: 256,
    bytes: 512,
    sha256: sha256(fs.readFileSync(file)),
  };
  log('[gen]  trig.inc  ' + report.trig_table.entries + ' Q8.8 sin/cos steps  sha256=' + report.trig_table.sha256.slice(0, 16));
}

/* ------------------------------------------------------------------------- */
/* module discovery + compile gate                                            */
/* ------------------------------------------------------------------------- */
function discoverModules() {
  const files = walk(SRC, (f) => f.toLowerCase().endsWith('.asm'))
    .filter((f) => path.resolve(f) !== path.resolve(KERNEL_SOURCE))
    .sort((a, b) => a.localeCompare(b));
  return files;
}

function compileModules() {
  const modules = discoverModules();
  if (!modules.length) fail('no kernel modules found under src/');
  log('[mod]  ' + modules.length + ' modules under src/');

  for (const file of modules) {
    const rel = path.relative(ROOT, file).replace(/\\/g, '/');
    const out = path.join(OBJ, rel.replace(/\.asm$/i, '.o'));
    mkdirp(path.dirname(out));
    if (OPT.check) {
      runNasm(
        ['-f', 'obj', '-DZENITH_OBJ_CHECK', ...INCLUDE_DIRS.flatMap((d) => ['-I', d]), '-o', out, file],
        rel
      );
      const size = fs.statSync(out).size;
      report.modules.push({ file: rel, object_bytes: size, status: 'ok' });
      log('[mod]  ok  ' + rel + '  (' + size + ' B object)');
    } else {
      report.modules.push({ file: rel, object_bytes: 0, status: 'skipped' });
    }
  }
}

/* ------------------------------------------------------------------------- */
/* artifacts                                                                  */
/* ------------------------------------------------------------------------- */
function assembleBoot() {
  const out = path.join(BUILD, 'boot.bin');
  runNasm(['-f', 'bin', '-I', SRC, '-o', out, BOOT_SOURCE], 'boot.asm');
  const boot = fs.readFileSync(out);
  if (boot.length !== SECTOR) fail('boot.bin is ' + boot.length + ' bytes; the boot sector must be exactly 512');
  if (boot[510] !== 0x55 || boot[511] !== 0xaa) fail('boot.bin is missing the 0xAA55 signature');
  report.artifacts.boot = {
    file: 'build/boot.bin',
    bytes: boot.length,
    sha256: sha256(boot),
    signature: '0xAA55',
  };
  log('[bin]  boot.bin     512 bytes  signature 0xAA55  sha256=' + sha256(boot).slice(0, 16));
  return boot;
}

function assembleKernel(sectors) {
  const out = path.join(BUILD, 'kernel.bin');
  runNasm(
    ['-f', 'bin', ...INCLUDE_DIRS.flatMap((d) => ['-I', d]), '-o', out, KERNEL_SOURCE],
    'kernel'
  );
  const kernel = fs.readFileSync(out);
  const max = sectors * SECTOR;
  if (kernel.length === 0) fail('kernel.bin is empty');
  if (kernel.length > max) {
    fail('kernel.bin is ' + kernel.length + ' bytes, but boot.asm only loads ' + max + ' bytes (' + sectors + ' sectors)');
  }
  report.artifacts.kernel = {
    file: 'build/kernel.bin',
    bytes: kernel.length,
    budget: max,
    fill: (100 * kernel.length / max).toFixed(1) + '%',
    sha256: sha256(kernel),
  };
  log('[bin]  kernel.bin   ' + kernel.length + ' / ' + max + ' bytes  (' + (100 * kernel.length / max).toFixed(1) + '% of window)  sha256=' + sha256(kernel).slice(0, 16));
  return kernel;
}

function packImage(boot, kernel) {
  const image = Buffer.alloc(FLOPPY_SIZE, 0);
  boot.copy(image, 0);
  kernel.copy(image, SECTOR);
  const imagePath = path.join(BUILD, IMAGE_NAME);
  fs.writeFileSync(imagePath, image);
  // Keep a copy at the repository root: "the deliverable is zenithos.img".
  fs.copyFileSync(imagePath, path.join(ROOT, IMAGE_NAME));

  const check = fs.readFileSync(imagePath);
  if (check.length !== FLOPPY_SIZE) fail('image size verification failed');
  if (!check.subarray(0, SECTOR).equals(boot)) fail('boot sector read-back verification failed');
  if (!check.subarray(SECTOR, SECTOR + kernel.length).equals(kernel)) fail('kernel payload read-back verification failed');
  if (!check.subarray(SECTOR + kernel.length).equals(Buffer.alloc(FLOPPY_SIZE - SECTOR - kernel.length))) {
    fail('image tail verification failed (expected zero fill)');
  }
  report.image = {
    file: IMAGE_NAME,
    bytes: check.length,
    sectors: check.length / SECTOR,
    boot_sha256: sha256(boot),
    kernel_sha256: sha256(kernel),
    sha256: sha256(check),
  };
  report.checks = {
    boot_512: boot.length === SECTOR,
    boot_signature: boot[510] === 0x55 && boot[511] === 0xaa,
    kernel_within_budget: kernel.length <= report.artifacts.kernel.budget,
    image_size: check.length === FLOPPY_SIZE,
    payload_readback: true,
  };
  log('[img]  zenithos.img  ' + check.length + ' bytes (1.44 MB)  sha256=' + report.image.sha256);
  log('[img]  written to zenithos.img and build/' + IMAGE_NAME);
}

/* ------------------------------------------------------------------------- */
/* main                                                                       */
/* ------------------------------------------------------------------------- */
function main() {
  const started = Date.now();
  log('');
  log('  ZENITHOS // build pipeline');
  log('  root: ' + ROOT);
  log('');

  mkdirp(BUILD);
  mkdirp(OBJ);
  mkdirp(GEN);

  NASM = findNasm();
  log('[nasm] ' + report.nasm);

  const sectors = readConfigConstant('KERNEL_SECTORS');
  const org = readConfigConstant('KERNEL_ORG');
  log('[cfg]  kernel load 0x' + org.toString(16) + ', window ' + sectors + ' sectors (' + sectors * SECTOR + ' bytes)');

  generateTrig();
  generatePalette();
  compileModules();
  const boot = assembleBoot();
  const kernel = assembleKernel(sectors);
  packImage(boot, kernel);

  report.finished_utc = new Date().toISOString();
  report.duration_ms = Date.now() - started;
  report.result = 'ok';
  fs.writeFileSync(path.join(BUILD, 'report.json'), JSON.stringify(report, null, 2));

  log('');
  log('  BUILD OK  in ' + report.duration_ms + ' ms');
  log('  boot    : build/boot.bin           512 B');
  log('  kernel  : build/kernel.bin         ' + kernel.length + ' B');
  log('  image   : build/zenithos.img       ' + FLOPPY_SIZE + ' B');
  log('  image   : zenithos.img             ' + FLOPPY_SIZE + ' B');
  log('  report  : build/report.json');
  log('');
  log('  run it:');
  log('    qemu-system-i386 -fda zenithos.img -boot a');
  log('    node tools/emulate.mjs');
  log('');
}

main();
