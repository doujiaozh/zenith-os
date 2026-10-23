// ---------------------------------------------------------------------------
// tools/lib/png.mjs
//
// Minimal dependency-free PNG encoder for 8-bit RGBA images (Node's zlib only).
// Supports integer nearest-neighbour upscaling, which keeps the harness output
// crisp for a 320x200 VGA framebuffer.
// ---------------------------------------------------------------------------

import zlib from 'node:zlib';

const CRC_TABLE = (() => {
    const table = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
        let c = n;
        for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
        table[n] = c >>> 0;
    }
    return table;
})();

function crc32(buf) {
    let c = 0xffffffff;
    for (let i = 0; i < buf.length; i++) c = CRC_TABLE[(c ^ buf[i]) & 0xff] ^ (c >>> 8);
    return (c ^ 0xffffffff) >>> 0;
}

function chunk(type, data) {
    const len = Buffer.alloc(4);
    len.writeUInt32BE(data.length, 0);
    const typeBuf = Buffer.from(type, 'ascii');
    const body = Buffer.concat([typeBuf, data]);
    const crc = Buffer.alloc(4);
    crc.writeUInt32BE(crc32(body), 0);
    return Buffer.concat([len, body, crc]);
}

/**
 * Encode an RGBA framebuffer as a PNG Buffer.
 * @param {Uint8Array|Uint8ClampedArray|Buffer} rgba  width*height*4 bytes
 * @param {number} width  source width in pixels
 * @param {number} height source height in pixels
 * @param {number} [scale=1] integer nearest-neighbour upscale factor
 * @returns {Buffer}
 */
export function encodePNG(rgba, width, height, scale = 1) {
    if (!Number.isInteger(scale) || scale < 1) throw new Error(`invalid scale: ${scale}`);
    if (rgba.length < width * height * 4) {
        throw new Error(`rgba buffer too small: ${rgba.length} < ${width * height * 4}`);
    }
    const outW = width * scale;
    const outH = height * scale;

    // Build raw scanlines (filter byte 0 = None).
    const stride = outW * 4;
    const raw = Buffer.alloc((stride + 1) * outH);
    for (let y = 0; y < outH; y++) {
        const srcY = Math.floor(y / scale);
        const srcRow = srcY * width * 4;
        const dstRow = y * (stride + 1);
        raw[dstRow] = 0;
        if (scale === 1) {
            raw.set(rgba.subarray(srcRow, srcRow + stride), dstRow + 1);
        } else {
            for (let x = 0; x < outW; x++) {
                const si = srcRow + Math.floor(x / scale) * 4;
                const di = dstRow + 1 + x * 4;
                raw[di] = rgba[si];
                raw[di + 1] = rgba[si + 1];
                raw[di + 2] = rgba[si + 2];
                raw[di + 3] = rgba[si + 3];
            }
        }
    }

    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(outW, 0);
    ihdr.writeUInt32BE(outH, 4);
    ihdr[8] = 8;  // bit depth
    ihdr[9] = 6;  // colour type RGBA
    ihdr[10] = 0; // compression
    ihdr[11] = 0; // filter
    ihdr[12] = 0; // interlace

    return Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        chunk('IHDR', ihdr),
        chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
        chunk('IEND', Buffer.alloc(0)),
    ]);
}
