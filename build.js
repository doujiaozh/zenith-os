#!/usr/bin/env node
/*
 * ============================================================================
 *  ZenithOS v6.0 - build system
 * ============================================================================
 *
 *  node build.js [--quiet|--verbose]
 *
 *  Pipeline (C + ASM hybrid, linked by the system GNU toolchain):
 *    1. locate NASM, TCC, GNU ld and objcopy
 *    2. discover kernel sources under src/ (.c and .asm)
 *    3. compile each .c   with TCC   -> ELF32 relocatable object (freestanding)
 *    4. assemble each .asm with NASM -> ELF32 relocatable object
 *    5. link every object with GNU ld + a generated script so the sections
 *       land at KERNEL_ORG (the PE emulation needs --image-base ORG-0x1000;
 *       a native ELF ld takes the same script without it)
 *    6. objcopy -O binary -> flat build/kernel.bin with _start at offset 0
 *    7. assemble boot/bios/boot.asm -> 512-byte boot sector
 *    8. pack build/zenithos.img (1.44 MiB) and copy to the repository root
 *    9. verify entry address / sizes / signature / budget, write report.json
 *
 *  No ELF linker is hand-rolled here: ld resolves every symbol and objcopy
 *  flattens the result. Object files carry their source extension
 *  (foo.c.o / foo.asm.o) so sibling modules like idt.c and idt.asm cannot
 *  overwrite each other.
 *
 *  No npm dependencies; Node 18+, NASM 2.13+, TCC 0.9.27 (i386),
 *  GNU binutils (ld + objcopy) for the final link.
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
const INC = path.join(SRC, 'include');
const BOOT_SOURCE = path.join(ROOT, 'boot', 'bios', 'boot.asm');
const IMAGE_NAME = 'zenithos.img';
const FLOPPY_SIZE = 1440 * 1024;
const SECTOR = 512;

const argv = new Set(process.argv.slice(2));
const quiet = argv.has('--quiet');
const verbose = argv.has('--verbose');

let NASM = null, CC = null, LD = null, OBJCOPY = null, NM = null;

function log(m) { if (!quiet) process.stdout.write(m + '\n'); }
function vlog(m) { if (verbose) process.stdout.write(m + '\n'); }
function fail(m) { process.stderr.write('\n[build] FATAL: ' + m + '\n'); process.exit(1); }
function mkdirp(d) { fs.mkdirSync(d, { recursive: true }); }
function sha256(b) { return crypto.createHash('sha256').update(b).digest('hex'); }

function walk(dir, pred, out) {
  out = out || [];
  for (const e of fs.readdirSync(dir, { withFileTypes: true })) {
    const f = path.join(dir, e.name);
    if (e.isDirectory()) walk(f, pred, out);
    else if (pred(f)) out.push(f);
  }
  return out;
}

/* ------------------------------------------------------------------ */
/* toolchain                                                          */
/* ------------------------------------------------------------------ */
function probe(cmd, flag) {
  const p = spawnSync(cmd, [flag], { cwd: ROOT, encoding: 'utf8', windowsHide: true });
  return p.error === undefined && p.status === 0;
}

function findTool(kind, env, cands, flag) {
  const list = [];
  if (process.env[env]) list.push(process.env[env]);
  list.push(...cands);
  for (const c of list) {
    if (!path.isAbsolute(c)) {
      if (probe(c, flag || '-v')) return c;
      continue;
    }
    if (fs.existsSync(c) && probe(c, flag || '-v')) return c;
  }
  return null;
}

// GNU binutils live next to gcc in the same bin directory, so any mingw /
// msys / tdm layout on the machine is worth probing.
const GNU_DIRS = [
  'D:\\mingw64\\bin', 'C:\\mingw64\\bin', 'C:\\MinGW\\bin',
  'C:\\msys64\\mingw64\\bin', 'C:\\msys64\\mingw32\\bin',
  'C:\\msys64\\usr\\bin', 'C:\\TDM-GCC-64\\bin', 'C:\\TDM-GCC-32\\bin',
];

function findGnu(tool) {
  // POSIX hosts (the Linux CI: `gcc nasm binutils` via apt) carry binutils on
  // PATH under the bare name, so probe that first; the Windows layouts below
  // add the extension and the well-known mingw / msys / tdm directories.
  const cands = [process.env[tool.toUpperCase()]]
    .filter(Boolean)
    .concat([tool, tool + '.exe'], GNU_DIRS.map((d) => path.join(d, tool + '.exe')));
  for (const c of cands) {
    if (!path.isAbsolute(c)) {
      if (probe(c, '--version')) return c;
      continue;
    }
    if (fs.existsSync(c) && probe(c, '--version')) return c;
  }
  return null;
}

/* C compiler discovery: prefer the repo-local TCC, fall back to a system
 * gcc/cc/clang that understands -m32 (Linux CI path, apt gcc + binutils).
 * gcc -m32 emits PE objects under mingw and ELF objects on Linux; the linker
 * stage below already handles both.
 * `node build.js --cc <bin>` overrides discovery (CI parity testing). */
function findCC() {
  const gccExtra = ['-m32', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                    // mingw GCC prefixes i386 PE symbols with '_'; our NASM
                    // objects (and the asm->C calls) use bare names, and we
                    // pull no system headers, so emit bare symbols.
                    '-fno-leading-underscore'];
  const ai = process.argv.indexOf('--cc');
  if (ai !== -1 && process.argv[ai + 1]) {
    return { bin: process.argv[ai + 1], kind: 'arg', extra: gccExtra };
  }
  const tcc = findTool('TCC', 'TCC', [path.join(ROOT, 'tcc', 'tcc.exe'), path.join(ROOT, 'tcc.exe'), 'tcc']);
  if (tcc) return { bin: tcc, kind: 'tcc', extra: [] };
  for (const c of ['gcc', 'cc', 'clang']) {
    const found = findTool('CC', 'CC', [c]);
    if (found) return { bin: found, kind: c, extra: gccExtra };
  }
  return null;
}

function run(cmd, args, what) {
  vlog('$ ' + cmd + ' ' + args.join(' '));
  const r = spawnSync(cmd, args, { cwd: ROOT, encoding: 'utf8', windowsHide: true });
  if (r.error) fail('could not start ' + what + ': ' + r.error.message);
  if (r.status !== 0) {
    process.stderr.write('\n--- ' + what + ' output ---\n' + ((r.stdout || '') + (r.stderr || '')).trim() + '\n');
    fail(what + ' failed (exit ' + r.status + ')');
  }
  const out = ((r.stdout || '') + (r.stderr || '')).trim();
  if (out) process.stdout.write(out + '\n');
}

/* ------------------------------------------------------------------ */
/* config.inc parsing                                                 */
/* ------------------------------------------------------------------ */
function configEqu(name) {
  const text = fs.readFileSync(path.join(INC, 'config.inc'), 'utf8');
  const m = text.match(new RegExp('^\\s*' + name + '\\s+equ\\s+([^;\\s]+)', 'm'));
  if (!m) fail('config.inc missing constant ' + name);
  const v = m[1].trim();
  if (/^0x[0-9a-f]+$/i.test(v)) return parseInt(v, 16);
  if (/^[0-9]+$/.test(v)) return parseInt(v, 10);
  fail('config.inc constant ' + name + ': unsupported value ' + v);
}

/* ------------------------------------------------------------------ */
/* generated assets (Q8.8 trigonometry table for lib/math)             */
/* ------------------------------------------------------------------ */
function generateTrig() {
  mkdirp(GEN);
  const vals = [];
  for (let i = 0; i < 256; i++) {
    vals.push(Math.round(Math.sin((i * 2 * Math.PI) / 256) * 256));
  }
  let body = '';
  for (let i = 0; i < 256; i += 8) {
    body += '  ' + vals.slice(i, i + 8).join(', ') + ',\n';
  }
  const text = [
    '/* DO NOT EDIT: generated by build.js. 256 steps of sin, one full turn, Q8.8. */',
    '#ifndef ZENITH_TRIG_H',
    '#define ZENITH_TRIG_H',
    'static const short zenith_sin_q8[256] = {',
    body.trimEnd() + '\n};',
    '/* cos(a) = sin(a + 64) because 64 steps == a quarter turn. */',
    '#define ZENITH_COS_Q8(a) zenith_sin_q8[((a) + 64) & 255]',
    '#endif',
    '',
  ].join('\n');
  const file = path.join(GEN, 'trig.h');
  fs.writeFileSync(file, text);
  log('[gen ] trig.h  256-entry Q8.8 sine table');
}

/* ------------------------------------------------------------------ */
/* source discovery + compilation                                     */
/* ------------------------------------------------------------------ */
function rel(p) { return path.relative(ROOT, p).replace(/\\/g, '/'); }

function discoverSources() {
  const files = walk(SRC, (f) => /\.(c|asm)$/i.test(f));
  files.sort((a, b) => {
    const ra = rel(a), rb = rel(b);
    const ka = ra === 'src/kernel/startup.asm' ? 0 : 1;
    const kb = rb === 'src/kernel/startup.asm' ? 0 : 1;
    if (ka !== kb) return ka - kb;
    return ra < rb ? -1 : 1;
  });
  return files;
}

// Object names keep the source extension (foo.c.o / foo.asm.o) so that
// sibling sources sharing a basename - e.g. arch/x86_32/idt.c and idt.asm -
// never overwrite each other's object file.
function objectOf(f) {
  const r = rel(f);
  return path.join(OBJ, r.replace(/\.(c|asm)$/i, '.$1.o'));
}

function compileSources() {
  const sources = discoverSources();
  if (!sources.length) fail('no kernel sources under src/');

  // Full rebuild: no stale objects from a previous layout may survive.
  try { fs.rmSync(OBJ, { recursive: true, force: true }); } catch (e) { /* nothing yet */ }
  mkdirp(OBJ);

  const objects = [];
  for (const f of sources) {
    const r = rel(f);
    const out = objectOf(f);
    mkdirp(path.dirname(out));
    if (/\.c$/i.test(f)) {
      run(CC.bin, CC.extra.concat(['-c', '-nostdinc', '-I', INC, '-I', SRC, '-I', GEN, '-o', out, f]), 'cc ' + r);
      log('[c]   ' + r);
    } else {
      run(NASM, ['-f', 'elf32', '-I', INC, '-I', SRC, '-I', GEN, '-o', out, f], 'nasm ' + r);
      log('[asm] ' + r);
    }
    if (!fs.existsSync(out)) fail('compiler produced no object for ' + r);
    objects.push({ file: out, rel: r });
  }
  return objects;
}

/* ------------------------------------------------------------------ */
/* link with GNU ld, then flatten with objcopy                         */
/* ------------------------------------------------------------------ */
function writeLinkerScript(org) {
  const script = [
    '/* DO NOT EDIT: generated by build.js. Sections land at KERNEL_ORG. */',
    'OUTPUT_ARCH(i386)',
    'ENTRY(_start)',
    '',
    'SECTIONS',
    '{',
    '  . = 0x' + org.toString(16) + ';',
    '',
    '  .text : { *(.text) *(.text.*) }',
    '  .rodata : { *(.rodata) *(.rodata.*) }',
    '  .data : { *(.data) *(.data.*) }',
    '',
    '  .bss : {',
    '    __bss_start = .;',
    '    *(.bss) *(.bss.*) *(COMMON)',
    '    __bss_end = .;',
    '  }',
    '',
    '  __end = .;',
    '  __kernel_end = .;',
    '  __kernel_size = . - 0x' + org.toString(16) + ';',
    '',
    '  /DISCARD/ : { *(.comment) *(.eh_frame) *(.note.*) *(.group) }',
    '}',
    '',
  ].join('\n');
  const file = path.join(BUILD, 'kernel.ld');
  fs.writeFileSync(file, script);
  return file;
}

function linkKernel(objects, org) {
  const ldScript = writeLinkerScript(org);
  const exe = path.join(BUILD, 'kernel.exe');
  const args = objects.map((o) => o.file);

  // The mingw PE emulation cannot link below ImageBase+0x1000 (the PE headers
  // own the first page of the image), so park the headers just below KERNEL_ORG
  // and let the script place .text exactly at it. A native ELF ld takes the
  // same script directly - x86_64 hosts need -m elf_i386 to accept i386
  // objects - so try both as fallbacks (the last one lets a multi-arch ld
  // pick its own default).
  const attempts = [
    ['pe', ['-m', 'i386pe', '--image-base', '0x' + (org - 0x1000).toString(16)]],
    ['elf32', ['-m', 'elf_i386']],
    ['native', []],
  ];

  let lastErr = '';
  for (const [label, extra] of attempts) {
    vlog('$ ' + LD + ' ' + extra.concat(['-T', ldScript, '-o', exe]).join(' ') + ' <' + args.length + ' objects>');
    const r = spawnSync(LD, extra.concat(['-T', ldScript, '-o', exe, ...args]),
      { cwd: ROOT, encoding: 'utf8', windowsHide: true });
    if (r.status === 0) {
      log('[link] ld (' + label + ') ' + args.length + ' objects -> kernel.exe');
      return exe;
    }
    lastErr = ((r.stdout || '') + (r.stderr || '')).trim();
    vlog('--- ld (' + label + ') failed ---\n' + lastErr);
    if (fs.existsSync(exe)) { try { fs.unlinkSync(exe); } catch (e) { /* retry anyway */ } }
  }
  fail('GNU ld could not link the kernel:\n' + lastErr);
}

function flattenKernel(exe) {
  const bin = path.join(BUILD, 'kernel.bin');
  run(OBJCOPY, ['-O', 'binary', exe, bin], 'objcopy kernel.exe -> kernel.bin');
  if (!fs.existsSync(bin)) fail('objcopy produced no kernel.bin');
  return fs.readFileSync(bin);
}

// Optional: read the linked symbols back with nm and sanity-check the boot
// contract (_start must sit exactly at KERNEL_ORG, BSS bounds inside the image).
function verifySymbols(exe, org) {
  const info = { entry: null, bss_start: null, bss_end: null };
  if (!NM) { log('[nm  ] not found, skipping symbol verification'); return info; }
  const r = spawnSync(NM, [exe], { cwd: ROOT, encoding: 'utf8', windowsHide: true });
  if (r.status !== 0) { log('[nm  ] failed, skipping symbol verification'); return info; }
  const want = { _start: 'entry', __bss_start: 'bss_start', __bss_end: 'bss_end' };
  for (const line of (r.stdout || '').split(/\r?\n/)) {
    const m = line.match(/^([0-9a-fA-F]+)\s+[TtBb]\s+(\S+)/);
    if (!m) continue;
    const key = want[m[2]];
    if (key) info[key] = parseInt(m[1], 16);
  }
  if (info.entry === null) fail('nm: no _start symbol in the linked kernel');
  if (info.entry !== org)
    fail('_start must sit at KERNEL_ORG 0x' + org.toString(16) + ', nm says 0x' + info.entry.toString(16));
  if (info.bss_start !== null && info.bss_start < org)
    fail('__bss_start 0x' + info.bss_start.toString(16) + ' is below KERNEL_ORG');
  return info;
}

/* ------------------------------------------------------------------ */
/* boot + image                                                       */
/* ------------------------------------------------------------------ */
function assembleBoot() {
  const out = path.join(BUILD, 'boot.bin');
  run(NASM, ['-f', 'bin', '-I', INC, '-o', out, BOOT_SOURCE], 'boot/bios/boot.asm');
  const boot = fs.readFileSync(out);
  if (boot.length !== SECTOR) fail('boot.bin must be 512 bytes, got ' + boot.length);
  if (boot[510] !== 0x55 || boot[511] !== 0xaa) fail('boot.bin missing 0xAA55 signature');
  return boot;
}

function packImage(boot, kernel) {
  const image = Buffer.alloc(FLOPPY_SIZE, 0);
  boot.copy(image, 0);
  kernel.copy(image, SECTOR);
  fs.writeFileSync(path.join(BUILD, IMAGE_NAME), image);
  fs.copyFileSync(path.join(BUILD, IMAGE_NAME), path.join(ROOT, IMAGE_NAME));
  return sha256(image);
}

/* ------------------------------------------------------------------ */
/* main                                                               */
/* ------------------------------------------------------------------ */
function main() {
  const t0 = Date.now();
  log('');
  log('  ZENITHOS v6.0 // build pipeline (C + ASM, GNU ld + objcopy)');
  log('  root: ' + ROOT);
  log('');

  mkdirp(BUILD); mkdirp(OBJ); mkdirp(GEN);

  NASM = findTool('NASM', 'NASM', [
    path.join(ROOT, 'nasm.exe'), path.join(ROOT, 'nasm', 'nasm.exe'),
    'C:\\Program Files\\NASM\\nasm.exe', 'nasm',
  ]);
  if (!NASM) fail('NASM not found ($NASM, nasm.exe next to build.js, or PATH).');
  // C compiler: TCC first (repo-local on Windows), then the system gcc/cc
  // (Linux CI installs it via apt: gcc + nasm + binutils; -m32 objects are
  // compile-only, so no multilib runtime files are ever linked in).
  // gcc -m32 objects are PE-on-mingw / ELF-on-linux, both fine for ld below.
  CC = findCC();
  if (!CC) fail('No C compiler: $TCC, tcc/tcc.exe, tcc, or gcc/cc with -m32.');
  LD = findGnu('ld');
  if (!LD) fail('GNU ld not found (binutils on PATH, or C:\\mingw64\\bin).');
  OBJCOPY = findGnu('objcopy');
  if (!OBJCOPY) fail('GNU objcopy not found (binutils on PATH, or C:\\mingw64\\bin).');
  NM = findGnu('nm'); // optional

  log('[nasm] ' + NASM);
  log('[cc  ] ' + CC.bin + ' (' + CC.kind + ')');
  log('[ld  ] ' + LD);
  log('[objcopy] ' + OBJCOPY);
  if (NM) log('[nm  ] ' + NM);

  const org = configEqu('KERNEL_ORG');
  const sectors = configEqu('KERNEL_SECTORS');
  const budget = sectors * SECTOR;
  log('[cfg ] kernel @ 0x' + org.toString(16) + ', window ' + budget + ' B (' + sectors + ' sectors)');

  generateTrig();
  const objects = compileSources();

  const exe = linkKernel(objects, org);
  const kernel = flattenKernel(exe);
  const syms = verifySymbols(exe, org);

  const pct = (100 * kernel.length / budget).toFixed(1);
  log('[bin ] kernel.bin  ' + kernel.length + ' / ' + budget + ' B  (' + pct + '%)');
  log('[bin ]   file-backed .text+.rodata+.data = ' + kernel.length + ' B;  bss = ' +
      (syms.bss_end !== null ? (syms.bss_end - syms.bss_start) + ' B @ 0x' + syms.bss_start.toString(16) : 'n/a'));
  if (kernel.length > budget) fail('kernel exceeds ' + budget + ' B boot window');

  const boot = assembleBoot();
  const imgSha = packImage(boot, kernel);
  log('[img ] zenithos.img  ' + FLOPPY_SIZE + ' B  sha256=' + imgSha.slice(0, 16));
  log('[img ] written to zenithos.img and build/zenithos.img');

  const report = {
    project: 'ZenithOS', version: '6.0.0', result: 'ok',
    started: new Date().toISOString(), finished: new Date().toISOString(),
    duration_ms: Date.now() - t0,
    toolchain: { nasm: NASM, cc: CC.bin, cc_kind: CC.kind, ld: LD, objcopy: OBJCOPY, nm: NM },
    objects: objects.map((o) => o.rel),
    kernel: {
      bytes: kernel.length, budget, fill_pct: pct,
      entry: syms.entry, bss_start: syms.bss_start, bss_end: syms.bss_end,
      sha256: sha256(kernel),
    },
    image: { file: IMAGE_NAME, bytes: FLOPPY_SIZE, sha256: imgSha },
  };
  fs.writeFileSync(path.join(BUILD, 'report.json'), JSON.stringify(report, null, 2));

  log('');
  log('  BUILD OK in ' + report.duration_ms + ' ms');
  log('  run: node tools/emulate.mjs  (v86 headless boot + screenshot + serial)');
  log('');
}

main();
