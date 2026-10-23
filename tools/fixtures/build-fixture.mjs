// ---------------------------------------------------------------------------
// tools/fixtures/build-fixture.mjs
//
// Assembles tools/fixtures/vga-test.asm with the bundled NASM (nasm.exe in the
// workspace root) and packs the resulting boot sector into a standard 1.44 MB
// floppy image (boot sector at LBA 0, rest zero-filled).
//
// Usage: node tools/fixtures/build-fixture.mjs [--out tools/fixtures/vga-test.img]
// ---------------------------------------------------------------------------

import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ROOT = path.resolve(HERE, '..', '..');
const ASM = path.join(HERE, 'vga-test.asm');
const BIN = path.join(HERE, 'vga-test.bin');
const NASM = path.join(ROOT, 'nasm.exe');
const FLOPPY_SIZE = 1474560;

function argValue(name, fallback) {
    const i = process.argv.indexOf(name);
    if (i === -1) return fallback;
    const v = process.argv[i + 1];
    if (v === undefined || v.startsWith('--')) {
        throw new Error(`missing value for ${name}`);
    }
    return v;
}

const outPath = path.resolve(argValue('--out', path.join(HERE, 'vga-test.img')));

if (!fs.existsSync(NASM)) {
    console.error(`[fixture] NASM not found at ${NASM}`);
    process.exit(4);
}

console.log(`[fixture] assembling ${path.relative(ROOT, ASM)}`);
execFileSync(NASM, ['-f', 'bin', ASM, '-o', BIN], {
    cwd: ROOT,
    stdio: 'inherit',
});

const boot = fs.readFileSync(BIN);
if (boot.length > 512) {
    console.error(`[fixture] boot sector too large: ${boot.length} bytes (max 512)`);
    process.exit(4);
}
if (boot[510] !== 0x55 || boot[511] !== 0xaa) {
    console.error('[fixture] missing 0xAA55 boot signature');
    process.exit(4);
}

const img = Buffer.alloc(FLOPPY_SIZE);
boot.copy(img, 0);
fs.mkdirSync(path.dirname(outPath), { recursive: true });
fs.writeFileSync(outPath, img);

const md5 = createHash('md5').update(img).digest('hex').toUpperCase();
const sha = createHash('sha256').update(img).digest('hex').slice(0, 16).toUpperCase();
console.log(`[fixture] boot sector: ${boot.length} bytes`);
console.log(`[fixture] image: ${outPath}`);
console.log(`[fixture] size: ${img.length} bytes  md5=${md5}  sha256(16)=${sha}`);
