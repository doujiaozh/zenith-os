#!/usr/bin/env node
// ---------------------------------------------------------------------------
// tools/emulate.mjs
//
// Headless visual-regression gate for ZenithOS.
//
// Boots a floppy image (the real build/zenithos.img or the self-test fixture)
// inside v86 (WebAssembly x86 emulator, ESM build) with no browser/DOM, waits
// for the OS to reach its graphical screen, captures the VGA framebuffer,
// writes a PNG, prints frame metrics, optionally injects WASD/R keys and
// captures before/after screenshots, optionally captures COM1 serial output.
//
// Usage:
//   node tools/emulate.mjs [options]
//
// Options:
//   --image <path>     floppy image to boot        (default build/zenithos.img)
//   --fixture          boot the self-test fixture tools/fixtures/vga-test.img
//                      (builds it with nasm first if it is missing)
//   --seconds <n>      seconds to let the guest run before capture (default 6)
//   --out <path>       PNG path                    (default screenshots/emu-boot.png)
//   --keys "w,w,d,r"   inject key presses; writes <out>-before.png and
//                      <out>-after.png and prints the frame difference
//   --serial <path>    write captured COM1 output to this file
//   --scale <n>        integer nearest-neighbour upscale for PNGs (default 3)
//   --allow-blank      exit 0 even when the frame fails the non-trivial gate
//   --require-keys     exit 3 if key injection produced no pixel difference
//   --timeout <n>      hard wall-clock timeout in seconds (default seconds+60)
//   --help             show this help
//
// Exit codes:
//   0  PASS   frame is non-trivial (and any --keys/--serial extras are reported)
//   2  emulator fault / v86 panic / harness timeout
//   3  blank or non-graphical frame (failure with diagnostics)
//   4  usage / I/O error (bad arguments, missing image, missing BIOS, no v86)
//
// Implementation notes (verified against v86@0.5.472):
//   * v86's browser build needs the global `ImageData` constructor; Node has no
//     such global, so a minimal shim is installed before the emulator is
//     created. Without it v86 never allocates its RGBA destination buffer.
//   * Image/BIOS options must be objects with a `.buffer` ArrayBuffer property
//     (a raw ArrayBuffer or a plain path string does not work in Node).
//   * In mode 13h v86 reports virtual_width x virtual_height = 320x410
//     (128 KiB VGA plane / 320 bytes per line) while the visible screen is
//     320x200; the visible frame is composed from vga.layers exactly like the
//     browser screen adapter does (image_data, buffer_x/y, screen_x/y).
//   * COM1 output is captured by registering a listener on the v86 bus message
//     "serial0-output-byte"; no serial adapter/DOM is required.
//   * Key injection uses the public emulator.keyboard_send_scancodes() API,
//     which sends "keyboard-code" bus messages to the emulated 8042.
// ---------------------------------------------------------------------------

import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { fileURLToPath, pathToFileURL } from 'node:url';
import { execFileSync } from 'node:child_process';
import { encodePNG } from './lib/png.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..');

const EXIT = {
    PASS: 0,
    EMULATOR_FAULT: 2,
    BLANK_FRAME: 3,
    USAGE: 4,
};

const VENDOR_DIR = path.join(ROOT, 'tools', 'vendor');
const BIOS_FILES = {
    seabios: {
        name: 'seabios.bin',
        size: 131072,
        url: 'https://raw.githubusercontent.com/copy/v86/master/bios/seabios.bin',
    },
    vgabios: {
        name: 'vgabios.bin',
        size: 36352,
        url: 'https://raw.githubusercontent.com/copy/v86/master/bios/vgabios.bin',
    },
};

// PS/2 set-1 scancodes: [make code, break code (make | 0x80)]
const KEYMAP = {
    // row 1
    '1': [0x02, 0x82], '2': [0x03, 0x83], '3': [0x04, 0x84], '4': [0x05, 0x85],
    '5': [0x06, 0x86], '6': [0x07, 0x87], '7': [0x08, 0x88], '8': [0x09, 0x89],
    '9': [0x0a, 0x8a], '0': [0x0b, 0x8b],
    // row 2
    q: [0x10, 0x90], w: [0x11, 0x91], e: [0x12, 0x92], r: [0x13, 0x93],
    t: [0x14, 0x94], y: [0x15, 0x95], u: [0x16, 0x96], i: [0x17, 0x97],
    o: [0x18, 0x98], p: [0x19, 0x99],
    // row 3
    a: [0x1e, 0x9e], s: [0x1f, 0x9f], d: [0x20, 0xa0], f: [0x21, 0xa1],
    g: [0x22, 0xa2], h: [0x23, 0xa3], j: [0x24, 0xa4], k: [0x25, 0xa5],
    l: [0x26, 0xa6],
    // row 4
    z: [0x2c, 0xac], x: [0x2d, 0xad], c: [0x2e, 0xae], v: [0x2f, 0xaf],
    b: [0x30, 0xb0], n: [0x31, 0xb1], m: [0x32, 0xb2],
    // control
    enter: [0x1c, 0x9c], esc: [0x01, 0x81], backspace: [0x0e, 0x8e],
    tab: [0x0f, 0x8f], space: [0x39, 0xb9],
    // arrows (E0-prefixed: each entry is the full 4-byte make+break sequence)
    up: [0xe0, 0x48, 0xe0, 0xc8], down: [0xe0, 0x50, 0xe0, 0xd0],
    left: [0xe0, 0x4b, 0xe0, 0xcb], right: [0xe0, 0x4d, 0xe0, 0xcd],
};

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------

const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function md5(buf) {
    return createHash('md5').update(buf).digest('hex');
}

function sha256(buf) {
    return createHash('sha256').update(buf).digest('hex');
}

function pct(x) {
    return `${(x * 100).toFixed(2)}%`;
}

function readArrayBuffer(filePath) {
    const buf = fs.readFileSync(filePath);
    return { buf, arrayBuffer: buf.buffer.slice(buf.byteOffset, buf.byteOffset + buf.byteLength) };
}

function ensureDirFor(filePath) {
    fs.mkdirSync(path.dirname(filePath), { recursive: true });
}

function printHelp() {
    const lines = fs.readFileSync(fileURLToPath(import.meta.url), 'utf8')
        .split('\n')
        .filter((line) => line.startsWith('//'))
        .map((line) => line.replace(/^\/\/ ?/, ''));
    // Only the usage header of the comment block is user-facing help.
    const start = lines.findIndex((line) => line.startsWith('Usage:'));
    const end = lines.findIndex((line, i) => i > start && line.startsWith('Implementation notes'));
    console.log(lines.slice(Math.max(0, start - 1), end).join('\n'));
    console.log('\nExit codes: 0 PASS | 2 emulator fault | 3 blank frame | 4 usage/IO error');
}

class UsageError extends Error {}
class IoError extends Error {}

// ---------------------------------------------------------------------------
// Argument parsing
// ---------------------------------------------------------------------------

function parseArgs(argv) {
    const opts = {
        image: path.join(ROOT, 'build', 'zenithos.img'),
        imageExplicit: false,
        fixture: false,
        seconds: 6,
        out: path.join(ROOT, 'screenshots', 'emu-boot.png'),
        keys: [],
        keysRaw: '',
        serial: '',
        scale: 3,
        allowBlank: false,
        requireKeys: false,
        timeout: 0,
        help: false,
    };

    for (let i = 0; i < argv.length; i++) {
        const arg = argv[i];
        const needValue = (name) => {
            const value = argv[i + 1];
            if (value === undefined || value.startsWith('--')) {
                throw new UsageError(`missing value for ${name}`);
            }
            i++;
            return value;
        };
        switch (arg) {
            case '--image':
                opts.image = path.resolve(process.cwd(), needValue('--image'));
                opts.imageExplicit = true;
                break;
            case '--fixture':
                opts.fixture = true;
                break;
            case '--seconds': {
                const v = Number(needValue('--seconds'));
                if (!Number.isFinite(v) || v < 0 || v > 600) throw new UsageError('--seconds must be 0..600');
                opts.seconds = v;
                break;
            }
            case '--out':
                opts.out = path.resolve(process.cwd(), needValue('--out'));
                break;
            case '--keys': {
                opts.keysRaw = needValue('--keys');
                opts.keys = opts.keysRaw
                    .split(/[,\s]+/)
                    .map((s) => s.trim().toLowerCase())
                    .filter(Boolean);
                for (const key of opts.keys) {
                    if (!KEYMAP[key]) {
                        throw new UsageError(`unknown key "${key}" (known: ${Object.keys(KEYMAP).join(', ')})`);
                    }
                }
                break;
            }
            case '--serial':
                opts.serial = path.resolve(process.cwd(), needValue('--serial'));
                break;
            case '--scale': {
                const v = Number(needValue('--scale'));
                if (!Number.isInteger(v) || v < 1 || v > 16) throw new UsageError('--scale must be an integer 1..16');
                opts.scale = v;
                break;
            }
            case '--allow-blank':
                opts.allowBlank = true;
                break;
            case '--require-keys':
                opts.requireKeys = true;
                break;
            case '--timeout': {
                const v = Number(needValue('--timeout'));
                if (!Number.isFinite(v) || v <= 0 || v > 3600) throw new UsageError('--timeout must be seconds > 0');
                opts.timeout = v * 1000;
                break;
            }
            case '--help':
            case '-h':
                opts.help = true;
                break;
            default:
                throw new UsageError(`unknown argument "${arg}" (use --help)`);
        }
    }

    if (opts.requireKeys && opts.keys.length === 0) {
        throw new UsageError('--require-keys needs --keys');
    }
    if (opts.timeout === 0) {
        opts.timeout = opts.seconds * 1000 + 60000;
    }
    return opts;
}

// ---------------------------------------------------------------------------
// BIOS provisioning (auto-download + checksums)
// ---------------------------------------------------------------------------

async function downloadFile(url, destPath, expectedSize, label) {
    console.log(`[emulate] ${label} missing, downloading ${url}`);
    let response;
    try {
        response = await fetch(url, { redirect: 'follow' });
    } catch (err) {
        throw new IoError(`BIOS download failed for ${label}: ${err.message}`);
    }
    if (!response.ok) {
        throw new IoError(`BIOS download failed for ${label}: HTTP ${response.status} ${response.statusText}`);
    }
    const arrayBuffer = await response.arrayBuffer();
    const buf = Buffer.from(arrayBuffer);
    if (expectedSize && buf.length !== expectedSize) {
        throw new IoError(
            `BIOS download for ${label} has unexpected size ${buf.length} (expected ${expectedSize})`,
        );
    }
    ensureDirFor(destPath);
    fs.writeFileSync(destPath, buf);
    console.log(`[emulate] downloaded ${label}: ${buf.length} bytes md5=${md5(buf)}`);
}

async function ensureBios() {
    ensureDirFor(path.join(VENDOR_DIR, 'seabios.bin'));
    const result = {};
    for (const [key, info] of Object.entries(BIOS_FILES)) {
        const filePath = path.join(VENDOR_DIR, info.name);
        let stat = null;
        try {
            stat = fs.statSync(filePath);
        } catch {
            stat = null;
        }
        if (!stat || stat.size !== info.size) {
            await downloadFile(info.url, filePath, info.size, info.name);
        }
        const buf = fs.readFileSync(filePath);
        if (buf.length !== info.size) {
            throw new IoError(`${info.name} has size ${buf.length}, expected ${info.size} (delete it to re-download)`);
        }
        result[key] = { path: filePath, buf };
        console.log(
            `[emulate] bios ${info.name}: ${buf.length} bytes md5=${md5(buf)} sha256=${sha256(buf).slice(0, 16)}`,
        );
    }
    return result;
}

// ---------------------------------------------------------------------------
// Image provisioning
// ---------------------------------------------------------------------------

function ensureFixtureImage() {
    const fixtureImg = path.join(ROOT, 'tools', 'fixtures', 'vga-test.img');
    if (!fs.existsSync(fixtureImg)) {
        console.log('[emulate] fixture image missing, building it with nasm...');
        const builder = path.join(ROOT, 'tools', 'fixtures', 'build-fixture.mjs');
        try {
            execFileSync(process.execPath, [builder], { cwd: ROOT, stdio: 'inherit' });
        } catch (err) {
            throw new IoError(`could not build fixture image: ${err.message}`);
        }
    }
    return fixtureImg;
}

function loadImage(imagePath) {
    let stat;
    try {
        stat = fs.statSync(imagePath);
    } catch {
        throw new IoError(
            `image not found: ${imagePath}` +
                (path.basename(imagePath) === 'zenithos.img'
                    ? '\n            build it first with: npm run build'
                    : ''),
        );
    }
    if (!stat.isFile()) throw new IoError(`image is not a file: ${imagePath}`);
    const { buf, arrayBuffer } = readArrayBuffer(imagePath);
    if (buf.length % 512 !== 0) {
        console.warn(`[emulate] warning: image size ${buf.length} is not a multiple of 512`);
    }
    console.log(`[emulate] image ${imagePath}: ${buf.length} bytes md5=${md5(buf)}`);
    return { path: imagePath, buf, arrayBuffer, size: buf.length };
}

// ---------------------------------------------------------------------------
// Frame capture (headless equivalent of the browser screen adapter)
// ---------------------------------------------------------------------------

/**
 * Rebuild the visible screen from the VGA device's layers (the same data the
 * browser screen adapter would blit). Returns null when no framebuffer is
 * available (e.g. the guest is still in VGA text mode).
 */
function composeVisibleFrame(vga) {
    const screenWidth = vga.screen_width | 0;
    const screenHeight = vga.screen_height | 0;
    const image = vga.image_data;
    if (!image || !image.data) return null;
    if (screenWidth <= 0 || screenHeight <= 0) return null;

    const virtualWidth = image.width | 0;
    const virtualHeight = image.height | 0;
    const src = image.data;
    const out = new Uint8ClampedArray(screenWidth * screenHeight * 4);
    for (let i = 3; i < out.length; i += 4) out[i] = 255; // opaque black background

    const layers = Array.isArray(vga.layers) ? vga.layers : [];
    let composed = false;
    for (const layer of layers) {
        if (!layer || !layer.image_data) continue;
        const layerData = layer.image_data.data;
        if (!layerData) continue;
        const layerWidth = layer.image_data.width | 0;
        const bufferWidth = layer.buffer_width | 0;
        const bufferHeight = layer.buffer_height | 0;
        if (bufferWidth <= 0 || bufferHeight <= 0) continue;

        const srcX0 = layer.buffer_x | 0;
        const srcY0 = layer.buffer_y | 0;
        const dstX0 = layer.screen_x | 0;
        const dstY0 = layer.screen_y | 0;

        for (let v = 0; v < bufferHeight; v++) {
            const srcY = srcY0 + v;
            const dstY = dstY0 + v;
            if (srcY < 0 || srcY >= virtualHeight || dstY < 0 || dstY >= screenHeight) continue;
            const srcRow = srcY * layerWidth * 4;
            const dstRow = dstY * screenWidth * 4;
            for (let u = 0; u < bufferWidth; u++) {
                const srcX = srcX0 + u;
                const dstX = dstX0 + u;
                if (srcX < 0 || srcX >= virtualWidth || dstX < 0 || dstX >= screenWidth) continue;
                const si = srcRow + srcX * 4;
                const di = dstRow + dstX * 4;
                out[di] = layerData[si];
                out[di + 1] = layerData[si + 1];
                out[di + 2] = layerData[si + 2];
                out[di + 3] = 255;
            }
        }
        composed = true;
    }

    if (!composed) {
        // Fallback: the visible area starts at the top of the virtual buffer.
        const copyW = Math.min(screenWidth, virtualWidth);
        const copyH = Math.min(screenHeight, virtualHeight);
        for (let y = 0; y < copyH; y++) {
            const srcRow = y * virtualWidth * 4;
            const dstRow = y * screenWidth * 4;
            out.set(src.subarray(srcRow, srcRow + copyW * 4), dstRow);
            for (let x = 0; x < copyW; x++) out[dstRow + x * 4 + 3] = 255;
        }
    }

    return {
        width: screenWidth,
        height: screenHeight,
        rgba: out,
        virtualWidth,
        virtualHeight,
        composition: composed ? 'layers' : 'crop',
    };
}

function captureFrame(emulator) {
    const vga = emulator.v86 && emulator.v86.cpu && emulator.v86.cpu.devices.vga;
    if (!vga) return { error: 'no VGA device' };
    if (!vga.graphical_mode) {
        let textRows = [];
        try {
            textRows = emulator.screen_adapter.get_text_screen() || [];
        } catch {
            textRows = [];
        }
        return { error: 'not in graphical mode', graphical: false, textRows };
    }
    vga.screen_fill_buffer();
    const frame = composeVisibleFrame(vga);
    if (!frame) return { error: 'no image_data / screen size not set' };
    return frame;
}

function frameMetrics(frame) {
    const { width, height, rgba } = frame;
    const total = width * height;
    let nonBlack = 0;
    const histogram = new Map();
    for (let i = 0; i < rgba.length; i += 4) {
        const r = rgba[i];
        const g = rgba[i + 1];
        const b = rgba[i + 2];
        const key = (r << 16) | (g << 8) | b;
        histogram.set(key, (histogram.get(key) || 0) + 1);
        if (r !== 0 || g !== 0 || b !== 0) nonBlack++;
    }
    const topColors = [...histogram.entries()]
        .sort((a, b) => b[1] - a[1])
        .slice(0, 8)
        .map(([key, count]) => ({
            hex: `#${key.toString(16).padStart(6, '0')}`,
            count,
            share: count / total,
        }));
    return {
        width,
        height,
        totalPixels: total,
        nonBlackPixels: nonBlack,
        nonBlackRatio: total ? nonBlack / total : 0,
        distinctColours: histogram.size,
        uniform: histogram.size <= 1,
        topColors,
        md5: md5(Buffer.from(rgba.buffer, rgba.byteOffset, rgba.byteLength)),
    };
}

function describeFrameMetrics(metrics) {
    const top = metrics.topColors
        .map((c) => `${c.hex}(${c.count},${pct(c.share)})`)
        .join(' ');
    return (
        `dimensions=${metrics.width}x${metrics.height} ` +
        `nonBlack=${pct(metrics.nonBlackRatio)} ` +
        `distinctColours=${metrics.distinctColours} ` +
        `uniform=${metrics.uniform} ` +
        `md5=${metrics.md5}\n` +
        `[emulate] top colours: ${top}`
    );
}

function frameDiff(before, after) {
    const n = Math.min(before.rgba.length, after.rgba.length);
    let changed = 0;
    let sumDelta = 0;
    let maxDelta = 0;
    for (let i = 0; i < n; i += 4) {
        const dr = Math.abs(before.rgba[i] - after.rgba[i]);
        const dg = Math.abs(before.rgba[i + 1] - after.rgba[i + 1]);
        const db = Math.abs(before.rgba[i + 2] - after.rgba[i + 2]);
        const d = Math.max(dr, dg, db);
        if (d !== 0) changed++;
        sumDelta += dr + dg + db;
        if (d > maxDelta) maxDelta = d;
    }
    const pixels = n / 4;
    return {
        changedPixels: changed,
        totalPixels: pixels,
        ratio: pixels ? changed / pixels : 0,
        meanChannelDelta: pixels ? sumDelta / (pixels * 3) : 0,
        maxChannelDelta: maxDelta,
    };
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

async function main() {
    const opts = parseArgs(process.argv.slice(2));
    if (opts.help) {
        printHelp();
        return EXIT.PASS;
    }

    // ---- resolve image ----------------------------------------------------
    if (opts.fixture) {
        const fixtureImg = ensureFixtureImage();
        if (!opts.imageExplicit) opts.image = fixtureImg;
    }
    const image = loadImage(opts.image);

    // ---- BIOS images ------------------------------------------------------
    const bios = await ensureBios();

    // ---- v86 --------------------------------------------------------------
    const v86Entry = path.join(ROOT, 'node_modules', 'v86', 'build', 'libv86.mjs');
    const wasmPath = path.join(ROOT, 'node_modules', 'v86', 'build', 'v86.wasm');
    if (!fs.existsSync(v86Entry) || !fs.existsSync(wasmPath)) {
        throw new IoError('v86 is not installed (node_modules/v86 missing); run: npm install');
    }

    // v86 allocates its RGBA destination buffer only when a global ImageData
    // exists (normally provided by the browser). Provide it before creating
    // the emulator.
    if (typeof globalThis.ImageData === 'undefined') {
        globalThis.ImageData = class ImageData {
            constructor(data, width, height) {
                if (typeof data === 'number') {
                    this.width = data;
                    this.height = width;
                    this.data = new Uint8ClampedArray(this.width * this.height * 4);
                } else {
                    this.data = data;
                    this.width = width;
                    this.height = height;
                }
            }
        };
    }

    const v86Version = (() => {
        try {
            return JSON.parse(fs.readFileSync(path.join(ROOT, 'node_modules', 'v86', 'package.json'), 'utf8')).version;
        } catch {
            return 'unknown';
        }
    })();
    console.log(`[emulate] v86 ${v86Version} (headless, no browser/DOM)`);

    const { V86 } = await import(pathToFileURL(v86Entry).href);

    // ---- crash / panic handling ------------------------------------------
    const serialBytes = [];
    const cpuExceptions = [];
    let lastFrame = null;
    let lastMetrics = null;
    let lastTextRows = null;
    let firstGraphicalAt = null;
    let exiting = false;
    let emulator = null;
    let watchdog = null;

    const outBase = opts.out.replace(/\.png$/i, '');
    const crashPng = `${outBase}-crash.png`;
    const crashTxt = `${outBase}-crash.txt`;

    function saveFramePng(frame, filePath) {
        const png = encodePNG(frame.rgba, frame.width, frame.height, opts.scale);
        ensureDirFor(filePath);
        fs.writeFileSync(filePath, png);
        return png;
    }

    function writeCrashReport(kind, err) {
        const message = err instanceof Error ? err.stack || err.message : String(err);
        const lines = [
            `kind: ${kind}`,
            `time: ${new Date().toISOString()}`,
            `image: ${opts.image}`,
            `imageBytes: ${image.size}`,
            `imageMd5: ${md5(image.buf)}`,
            `cpuExceptions: ${cpuExceptions.join(',') || '(none)'}`,
            `serialBytes: ${serialBytes.length}`,
            `error: ${message}`,
        ];
        if (lastMetrics) {
            lines.push(`lastFrame: ${JSON.stringify(lastMetrics)}`);
        }
        if (lastTextRows && lastTextRows.length) {
            lines.push(`lastTextScreen: ${lastTextRows.map((r) => r.trimEnd()).filter(Boolean).join(' | ')}`);
        }
        try {
            ensureDirFor(crashTxt);
            fs.writeFileSync(crashTxt, lines.join('\n') + '\n');
        } catch {
            /* best effort */
        }
        if (lastFrame) {
            try {
                saveFramePng(lastFrame, crashPng);
                console.error(`[emulate] saved last captured frame -> ${crashPng}`);
                if (lastMetrics) console.error(`[emulate] last frame metrics: ${describeFrameMetrics(lastMetrics)}`);
            } catch (saveErr) {
                console.error(`[emulate] could not save crash frame: ${saveErr.message}`);
            }
        } else {
            console.error('[emulate] no frame had been captured before the fault');
        }
        console.error(`[emulate] crash report -> ${crashTxt}`);
    }

    function fatal(kind, err) {
        if (exiting) return;
        exiting = true;
        if (watchdog) clearTimeout(watchdog);
        const firstLine = err instanceof Error ? err.message : String(err);
        console.error(`\n[emulate] ${kind}: ${firstLine}`);
        writeCrashReport(kind, err);
        console.error(`[emulate] exit ${EXIT.EMULATOR_FAULT} (emulator fault/panic)`);
        process.exit(EXIT.EMULATOR_FAULT);
    }

    process.on('uncaughtException', (err) => fatal('EMULATOR FAULT (uncaught exception)', err));
    process.on('unhandledRejection', (err) => fatal('EMULATOR FAULT (unhandled rejection)', err));
    process.on('SIGINT', () => fatal('INTERRUPTED (SIGINT)', new Error('SIGINT')));
    process.on('SIGTERM', () => fatal('INTERRUPTED (SIGTERM)', new Error('SIGTERM')));

    // ---- start the emulator ----------------------------------------------
    console.log(
        `[emulate] booting ${path.basename(opts.image)} for ${opts.seconds}s ` +
            `(memory 64 MiB, VGA 8 MiB, boot fda)`,
    );
    emulator = new V86({
        bios: { buffer: bios.seabios.buf.buffer.slice(bios.seabios.buf.byteOffset, bios.seabios.buf.byteOffset + bios.seabios.buf.byteLength) },
        vga_bios: { buffer: bios.vgabios.buf.buffer.slice(bios.vgabios.buf.byteOffset, bios.vgabios.buf.byteOffset + bios.vgabios.buf.byteLength) },
        fda: { buffer: image.arrayBuffer },
        wasm_path: wasmPath,
        autostart: true,
        memory_size: 64 * 1024 * 1024,
        vga_memory_size: 8 * 1024 * 1024,
    });

    // COM1 capture: the emulated 16550 sends one bus message per TX byte.
    emulator.bus.register(
        'serial0-output-byte',
        (byte) => {
            if (serialBytes.length < 4 * 1024 * 1024) serialBytes.push(byte);
        },
        null,
    );

    // Record (but do not swallow) CPU exceptions such as #GP/#UD; v86 panics
    // inside wasm when an exception handler is not implemented.
    emulator.cpu_exception_hook = (vector) => {
        cpuExceptions.push(vector);
        if (cpuExceptions.length <= 10) console.error(`[emulate] CPU exception vector ${vector}`);
    };

    // Hard watchdog: never hang forever.
    watchdog = setTimeout(() => {
        fatal('TIMEOUT', new Error(`no result after ${opts.timeout} ms`));
    }, opts.timeout);
    watchdog.unref?.();

    // ---- wait for boot ----------------------------------------------------
    // While waiting, keep a rolling snapshot of the newest graphical frame (and
    // of the VGA text screen) so that if v86 panics later we still have
    // evidence of what the guest was displaying at the time.
    const bootStart = Date.now();
    const bootDeadline = bootStart + opts.seconds * 1000;
    let nextSample = 0;
    while (Date.now() < bootDeadline) {
        await sleep(Math.min(250, Math.max(0, bootDeadline - Date.now())));
        if (Date.now() < nextSample) continue;
        nextSample = Date.now() + 400;
        try {
            const sample = captureFrame(emulator);
            if (sample && !sample.error) {
                lastFrame = sample;
                lastMetrics = frameMetrics(sample);
                if (firstGraphicalAt === null) {
                    firstGraphicalAt = Date.now() - bootStart;
                    console.log(
                        `[emulate] graphical mode reached after ${(firstGraphicalAt / 1000).toFixed(2)}s ` +
                            `(${sample.width}x${sample.height})`,
                    );
                }
            } else if (sample && sample.error && sample.textRows && sample.textRows.length) {
                lastTextRows = sample.textRows;
            }
        } catch {
            /* best-effort sampling only; the real gate runs after the wait */
        }
    }
    if (firstGraphicalAt === null) {
        console.warn('[emulate] WARNING: no graphical VGA mode detected during the boot wait');
    }

    // ---- capture "before" frame ------------------------------------------
    let before;
    try {
        before = captureFrame(emulator);
    } catch (err) {
        fatal('FRAME CAPTURE FAILED', err);
    }
    if (before.error) {
        const problems = [before.error];
        if (before.textRows && before.textRows.length) {
            const rows = before.textRows.map((r) => r.replace(/\s+$/, '')).filter((r) => r.length);
            console.error(`[emulate] VGA text-mode screen dump (${rows.length} non-empty rows):`);
            for (const row of rows.slice(0, 25)) console.error(`  |${row}|`);
            problems.push('guest never switched to a graphical VGA mode (mode 13h not set)');
        }
        if (opts.allowBlank) {
            console.warn(
                `[emulate] WARNING: no graphical frame but --allow-blank is set: ${problems.join('; ')}`,
            );
            console.log('[emulate] PASS (allow-blank)');
            exiting = true;
            clearTimeout(watchdog);
            process.exit(EXIT.PASS);
        }
        console.error(`[emulate] FAIL(blank): ${problems.join('; ')}`);
        console.error(`[emulate] exit ${EXIT.BLANK_FRAME} (blank/non-graphical frame)`);
        exiting = true;
        process.exit(EXIT.BLANK_FRAME);
    }

    lastFrame = before;
    const beforeMetrics = frameMetrics(before);
    lastMetrics = beforeMetrics;

    const beforePath = opts.keys.length ? `${outBase}-before.png` : opts.out;
    saveFramePng(before, beforePath);

    // ---- key injection ----------------------------------------------------
    let keyReport = null;
    if (opts.keys.length) {
        console.log(`[emulate] injecting keys: ${opts.keys.join(',')}`);
        let injectionError = null;
        for (const key of opts.keys) {
            const scancodes = KEYMAP[key];
            try {
                await emulator.keyboard_send_scancodes(scancodes, 140);
            } catch (err) {
                injectionError = err;
                break;
            }
        }
        if (injectionError) {
            console.warn(`[emulate] WARNING: v86 key injection failed (${injectionError.message}); continuing`);
            keyReport = { error: injectionError.message };
        } else {
            await sleep(700);
            let after = null;
            try {
                after = captureFrame(emulator);
            } catch (err) {
                console.warn(`[emulate] WARNING: post-key frame capture failed (${err.message}); continuing`);
            }
            if (after && !after.error) {
                lastFrame = after;
                const afterMetrics = frameMetrics(after);
                lastMetrics = afterMetrics;
                const afterPath = `${outBase}-after.png`;
                saveFramePng(after, afterPath);
                const diff = frameDiff(before, after);
                keyReport = { diff, afterPath, afterMetrics };
                console.log(
                    `[emulate] keys: ${opts.keys.join(',')} -> frame diff ${pct(diff.ratio)} ` +
                        `(${diff.changedPixels}/${diff.totalPixels} px, meanDelta=${diff.meanChannelDelta.toFixed(3)}, ` +
                        `maxDelta=${diff.maxChannelDelta})`,
                );
                console.log(`[emulate] keys: before -> ${beforePath}`);
                console.log(`[emulate] keys: after  -> ${afterPath}`);
                if (diff.changedPixels === 0) {
                    console.warn('[emulate] WARNING: injected keys produced no pixel difference');
                }
            } else {
                console.warn(`[emulate] WARNING: no post-key frame (${after ? after.error : 'capture failed'})`);
                keyReport = { error: after ? after.error : 'capture failed' };
            }
        }
    }

    // ---- serial -----------------------------------------------------------
    if (opts.serial) {
        ensureDirFor(opts.serial);
        fs.writeFileSync(opts.serial, Buffer.from(serialBytes));
        const text = Buffer.from(serialBytes).toString('latin1');
        const preview = text.replace(/\r/g, '\\r').replace(/\n/g, '\\n').slice(0, 200);
        console.log(`[emulate] serial: ${serialBytes.length} bytes -> ${opts.serial}`);
        if (serialBytes.length === 0) {
            console.warn('[emulate] WARNING: no COM1 output was captured (guest may not write to 0x3F8)');
        } else {
            console.log(`[emulate] serial preview: ${preview}${text.length > 200 ? '...' : ''}`);
        }
    }

    // ---- report + PASS gate ----------------------------------------------
    console.log(`[emulate] before frame: ${describeFrameMetrics(beforeMetrics)}`);
    if (keyReport && keyReport.afterMetrics) {
        console.log(`[emulate] after frame:  ${describeFrameMetrics(keyReport.afterMetrics)}`);
    }
    console.log(`[emulate] png ${beforePath}: ${beforeMetrics.width * opts.scale}x${beforeMetrics.height * opts.scale} (scale ${opts.scale})`);

    const problems = [];
    if (beforeMetrics.nonBlackRatio < 0.01) {
        problems.push(`non-black pixels ${pct(beforeMetrics.nonBlackRatio)} < 1%`);
    }
    if (beforeMetrics.distinctColours < 16) {
        problems.push(`distinct colours ${beforeMetrics.distinctColours} < 16`);
    }
    if (beforeMetrics.uniform) problems.push('frame is a uniform fill');
    if (keyReport && keyReport.diff && keyReport.diff.changedPixels === 0 && opts.requireKeys) {
        problems.push('key injection produced no frame difference (--require-keys)');
    }

    if (problems.length === 0) {
        console.log(
            `[emulate] PASS: non-trivial graphical frame ` +
                `(${pct(beforeMetrics.nonBlackRatio)} non-black, ${beforeMetrics.distinctColours} colours)`,
        );
        exiting = true;
        clearTimeout(watchdog);
        process.exit(EXIT.PASS);
    }

    if (opts.allowBlank) {
        console.warn(`[emulate] WARNING: frame gate failed but --allow-blank is set: ${problems.join('; ')}`);
        console.log('[emulate] PASS (allow-blank)');
        exiting = true;
        clearTimeout(watchdog);
        process.exit(EXIT.PASS);
    }

    console.error(`[emulate] FAIL(blank): ${problems.join('; ')}`);
    console.error(`[emulate] serial bytes captured: ${serialBytes.length}`);
    console.error(`[emulate] cpu exceptions: ${cpuExceptions.join(',') || '(none)'}`);
    console.error(`[emulate] exit ${EXIT.BLANK_FRAME} (blank/non-graphical frame)`);
    exiting = true;
    clearTimeout(watchdog);
    process.exit(EXIT.BLANK_FRAME);
}

main().catch((err) => {
    if (err instanceof UsageError) {
        console.error(`[emulate] usage error: ${err.message}`);
        console.error('[emulate] try: node tools/emulate.mjs --help');
        process.exit(EXIT.USAGE);
    }
    if (err instanceof IoError) {
        console.error(`[emulate] I/O error: ${err.message}`);
        process.exit(EXIT.USAGE);
    }
    console.error(`[emulate] unexpected error: ${err && err.stack ? err.stack : err}`);
    process.exit(EXIT.EMULATOR_FAULT);
});
