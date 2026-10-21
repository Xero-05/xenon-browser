// Pixel helpers for live suites that click screenshot pixels. Screenshots are
// synthetic fixture pages; nothing here reads a real profile.
import assert from 'node:assert/strict';
import { inflateSync } from 'node:zlib';

// Minimal decoder for the 8-bit, non-interlaced RGB(A) PNGs CEF captures (as in theme-integration.mjs).
export function decodePng(buffer) {
  assert.equal(buffer.subarray(0, 8).toString('hex'), '89504e470d0a1a0a', 'Screenshot must be a PNG');
  let offset = 8, width, height, depth, type, interlace; const chunks = [];
  while (offset < buffer.length) {
    const length = buffer.readUInt32BE(offset), kind = buffer.toString('ascii', offset + 4, offset + 8);
    const body = buffer.subarray(offset + 8, offset + 8 + length);
    if (kind === 'IHDR') { width = body.readUInt32BE(0); height = body.readUInt32BE(4); depth = body[8]; type = body[9]; interlace = body[12]; }
    else if (kind === 'IDAT') chunks.push(body);
    else if (kind === 'IEND') break;
    offset += 12 + length;
  }
  assert(depth === 8 && (type === 2 || type === 6) && interlace === 0, 'Unsupported screenshot PNG layout');
  const channels = type === 6 ? 4 : 3, stride = width * channels, raw = inflateSync(Buffer.concat(chunks));
  const pixels = Buffer.alloc(stride * height);
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)], line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    assert(filter <= 4, 'Invalid PNG filter');
    const out = pixels.subarray(y * stride, (y + 1) * stride), prior = y ? pixels.subarray((y - 1) * stride, y * stride) : undefined;
    for (let x = 0; x < stride; x++) {
      const a = x >= channels ? out[x - channels] : 0, b = prior ? prior[x] : 0, c = prior && x >= channels ? prior[x - channels] : 0;
      const p = a + b - c, pa = Math.abs(p - a), pb = Math.abs(p - b), pc = Math.abs(p - c);
      const predictor = [0, a, b, (a + b) >> 1, pa <= pb && pa <= pc ? a : pb <= pc ? b : c][filter];
      out[x] = (line[x] + predictor) & 255;
    }
  }
  return { width, height, channels, pixels };
}
// Image-pixel bounding box of a saturated fixture color; tolerant of display color conversion.
export function colorBox({ width, height, channels, pixels }, matches) {
  let box;
  for (let y = 0; y < height; y++) for (let x = 0; x < width; x++) {
    const i = (y * width + x) * channels;
    if (!matches(pixels[i], pixels[i + 1], pixels[i + 2])) continue;
    box = box ? { left: Math.min(box.left, x), top: Math.min(box.top, y), right: Math.max(box.right, x), bottom: Math.max(box.bottom, y) } : { left: x, top: y, right: x, bottom: y };
  }
  return box;
}
