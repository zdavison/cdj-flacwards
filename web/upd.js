// Read, change and rebuild the CDJ-900 MAIN update file (C900MAIN.UPD).
// A port of tools/upd.py; see that file for the format. The outputs must be
// byte-identical to the Python module (tests/web/upd.test.mjs).

export const HEADER_SIZE = 0x20;
export const APP_REGION = 0x40000;
export const APP_REGION_END = 0x3e0000;
export const APP_BASE = 0x04000000;
export const LZ_N = 4096;
export const LZ_F = 18;
export const LZ_MIN = 3;
export const LZ_CHAIN = 32;

const RECORD_BYTES = 32;
const S0_RECORD = "S00E0000726F6D6F626A20206D6F74D8";
const S7_RECORD = "S705A00000005A";

// A byte buffer that grows.
class Out {
  constructor(size = 1 << 16) { this.buf = new Uint8Array(size); this.length = 0; }
  push(b) {
    if (this.length === this.buf.length) {
      const bigger = new Uint8Array(this.buf.length * 2);
      bigger.set(this.buf);
      this.buf = bigger;
    }
    this.buf[this.length++] = b;
  }
  bytes() { return this.buf.slice(0, this.length); }
}

export function crc16xmodem(data) {
  let crc = 0;
  for (let i = 0; i < data.length; i++) {
    crc ^= data[i] << 8;
    for (let k = 0; k < 8; k++)
      crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
  }
  return crc;
}

export function header(version) {
  if (!/^\d\.\d\d$/.test(version)) throw new Error("version must look like 4.33");
  const text = "CDJ-900 MAIN    Ver" + version + "\0";
  const h = new Uint8Array(HEADER_SIZE).fill(0x20);
  for (let i = 0; i < text.length; i++) h[i] = text.charCodeAt(i);
  h[HEADER_SIZE - 1] = 0x30; // "0"
  return h;
}

function hexByte(s, i) {
  return parseInt(s.substr(i, 2), 16);
}

export function decodeUpd(upd) {
  const n = upd.length;
  if (crc16xmodem(upd.subarray(0, n - 2)) !== (upd[n - 2] | (upd[n - 1] << 8)))
    throw new Error("UPD CRC mismatch");
  if (upd[0x1f] !== 0x30) throw new Error("UPD flag is not '0'");
  const text = new TextDecoder("latin1").decode(upd.subarray(HEADER_SIZE, n - 2));
  const records = [];
  let end = 0;
  for (const line of text.split("\r\n")) {
    if (!line.startsWith("S2")) continue;
    const count = (line.length - 2) / 2;
    const raw = new Uint8Array(count);
    for (let i = 0; i < count; i++) raw[i] = hexByte(line, 2 + 2 * i);
    let sum = 0;
    for (let i = 0; i < count - 1; i++) sum += raw[i];
    if (((sum + raw[count - 1]) & 0xff) !== 0xff || raw[0] !== count - 1)
      throw new Error("bad S-record");
    const address = (raw[1] << 16) | (raw[2] << 8) | raw[3];
    const payload = raw.subarray(4, count - 1);
    records.push([address, payload]);
    end = Math.max(end, address + payload.length);
  }
  const image = new Uint8Array(end);
  for (const [address, payload] of records) image.set(payload, address);
  return image;
}

function srecord(address, payload) {
  const body = [payload.length + 4, (address >>> 16) & 0xff, (address >>> 8) & 0xff, address & 0xff, ...payload];
  let sum = 0;
  for (const b of body) sum += b;
  body.push(0xff - (sum & 0xff));
  let s = "S2";
  for (const b of body) s += b.toString(16).toUpperCase().padStart(2, "0");
  return s;
}

export function encodeUpd(image, version, end) {
  if (end > APP_REGION_END) throw new Error("image runs past the updater's erase area");
  let img = image;
  if (img.length < end) {
    img = new Uint8Array(end).fill(0xff);
    img.set(image);
  }
  const lines = [S0_RECORD];
  for (let a = 0; a < end; a += RECORD_BYTES) {
    const rec = img.subarray(a, Math.min(a + RECORD_BYTES, img.length));
    if (rec.some((b) => b !== 0)) lines.push(srecord(a, rec));
  }
  lines.push(S7_RECORD);
  const text = new TextEncoder().encode(lines.join("\r\n") + "\r\n");
  const body = new Uint8Array(HEADER_SIZE + text.length + 2);
  body.set(header(version));
  body.set(text, HEADER_SIZE);
  const crc = crc16xmodem(body.subarray(0, body.length - 2));
  body[body.length - 2] = crc & 0xff;
  body[body.length - 1] = crc >>> 8;
  return body;
}

export function lzssDecompress(src) {
  const window = new Uint8Array(LZ_N).fill(0x20);
  let r = LZ_N - LZ_F;
  let flags = 0;
  let i = 0;
  const out = new Out(src.length * 2 + 16);
  while (i < src.length) {
    flags >>>= 1;
    if (!(flags & 0x100)) {
      flags = src[i] | 0xff00;
      i += 1;
      if (i >= src.length) break;
    }
    if (flags & 1) {
      const c = src[i++];
      out.push(c);
      window[r] = c;
      r = (r + 1) & (LZ_N - 1);
    } else {
      if (i + 1 >= src.length) break;
      const pos = src[i] | ((src[i + 1] & 0xf0) << 4);
      const length = (src[i + 1] & 0x0f) + LZ_MIN;
      i += 2;
      for (let k = 0; k < length; k++) {
        const c = window[(pos + k) & (LZ_N - 1)];
        out.push(c);
        window[r] = c;
        r = (r + 1) & (LZ_N - 1);
      }
    }
  }
  return out.bytes();
}

// Greedy LZSS with hash chains, exactly as tools/upd.py lzss_compress.
export function lzssCompress(data, chain = LZ_CHAIN) {
  const out = new Out(data.length + 16);
  const heads = new Map();
  const n = data.length;
  const key = (k) => (data[k] << 16) | (data[k + 1] << 8) | data[k + 2];
  let i = 0;
  let flagPos = -1;
  let flagBit = 8;
  while (i < n) {
    if (flagBit === 8) {
      flagPos = out.length;
      out.push(0);
      flagBit = 0;
    }
    let bestLen = 0;
    let bestPos = 0;
    if (i + LZ_MIN <= n) {
      const cands = heads.get(key(i));
      if (cands && cands.length) {
        const limit = Math.min(LZ_F, n - i);
        for (let c = cands.length - 1; c >= Math.max(0, cands.length - chain); c--) {
          const p = cands[c];
          if (i - p > LZ_N - 1) break;
          let length = LZ_MIN;
          while (length < limit && data[p + length] === data[i + length]) length++;
          if (length > bestLen) {
            bestLen = length;
            bestPos = p;
            if (length === limit) break;
          }
        }
      }
    }
    let step;
    if (bestLen >= LZ_MIN) {
      const ring = (LZ_N - LZ_F + bestPos) & (LZ_N - 1);
      out.push(ring & 0xff);
      out.push(((ring >>> 4) & 0xf0) | (bestLen - LZ_MIN));
      step = bestLen;
    } else {
      out.buf[flagPos] |= 1 << flagBit;
      out.push(data[i]);
      step = 1;
    }
    const stop = Math.min(i + step, n - LZ_MIN + 1);
    for (let k = i; k < stop; k++) {
      const kk = key(k);
      let lst = heads.get(kk);
      if (!lst) {
        lst = [];
        heads.set(kk, lst);
      }
      lst.push(k);
      if (lst.length > 4 * chain) lst.splice(0, lst.length - chain);
    }
    i += step;
    flagBit += 1;
  }
  return out.bytes();
}

function u32le(b, o) { return (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0; }
function sum16(b, from, to) { let s = 0; for (let i = from; i < to; i++) s += b[i]; return s & 0xffff; }

export function unpackRegion(flash, address) {
  const size = u32le(flash, address);
  const end = address + 4 + size;
  if (sum16(flash, address, end) !== (flash[end] | (flash[end + 1] << 8)))
    throw new Error(`region 0x${address.toString(16)} byte sum mismatch`);
  return lzssDecompress(flash.subarray(address + 4, end));
}

export function packRegion(unpacked) {
  const packed = lzssCompress(unpacked);
  const check = lzssDecompress(packed);
  if (check.length !== unpacked.length || check.some((b, i) => b !== unpacked[i]))
    throw new Error("LZSS round trip failed");
  const region = new Uint8Array(4 + packed.length + 2);
  region[0] = packed.length; region[1] = packed.length >>> 8;
  region[2] = packed.length >>> 16; region[3] = packed.length >>> 24;
  region.set(packed, 4);
  const s = sum16(region, 0, 4 + packed.length);
  region[4 + packed.length] = s & 0xff;
  region[5 + packed.length] = s >>> 8;
  return region;
}

export function imageSum(app) {
  let s = 0;
  for (let o = 0; o + 4 <= app.length - 4; o += 4)
    s = (s + ((app[o] << 24) | (app[o + 1] << 16) | (app[o + 2] << 8) | app[o + 3]) >>> 0) >>> 0;
  return s;
}

export function fixImageSum(app) {
  const s = imageSum(app);
  const o = app.length - 4;
  app[o] = s >>> 24; app[o + 1] = s >>> 16; app[o + 2] = s >>> 8; app[o + 3] = s;
}

export function checkImageSum(app) {
  const o = app.length - 4;
  return imageSum(app) === (((app[o] << 24) | (app[o + 1] << 16) | (app[o + 2] << 8) | app[o + 3]) >>> 0);
}

export function buildFlash(stockFlash, app) {
  const region = packRegion(app);
  const end = APP_REGION + region.length;
  if (end > APP_REGION_END)
    throw new Error(`packed application ends at 0x${end.toString(16)}, past 0x${APP_REGION_END.toString(16)}`);
  const flash = new Uint8Array(end);
  flash.set(stockFlash.subarray(0, APP_REGION));
  flash.set(region, APP_REGION);
  return flash;
}
