// Read one member of a zip file, and write a small stored zip. Only what the
// patcher needs: no zip64, no encryption, no multi-disk archives.

let crcTable = null;

export function crc32(data) {
  if (!crcTable) {
    crcTable = new Uint32Array(256);
    for (let i = 0; i < 256; i++) {
      let c = i;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      crcTable[i] = c >>> 0;
    }
  }
  let crc = 0xffffffff;
  for (let i = 0; i < data.length; i++) crc = crcTable[(crc ^ data[i]) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

const u16 = (b, o) => b[o] | (b[o + 1] << 8);
const u32 = (b, o) => (b[o] | (b[o + 1] << 8) | (b[o + 2] << 16) | (b[o + 3] << 24)) >>> 0;

export function isZip(bytes) {
  return bytes.length >= 4 && bytes[0] === 0x50 && bytes[1] === 0x4b && bytes[2] === 3 && bytes[3] === 4;
}

async function inflateRaw(data) {
  const stream = new Blob([data]).stream().pipeThrough(new DecompressionStream("deflate-raw"));
  return new Uint8Array(await new Response(stream).arrayBuffer());
}

export async function findMember(zip, name) {
  // The end of central directory record is in the last 64 KiB + 22 bytes.
  let eocd = -1;
  for (let o = zip.length - 22; o >= Math.max(0, zip.length - 65557); o--) {
    if (u32(zip, o) === 0x06054b50) { eocd = o; break; }
  }
  if (eocd < 0) throw new Error("not a zip file");
  const count = u16(zip, eocd + 10);
  let p = u32(zip, eocd + 16);
  const want = name.toLowerCase();
  const decoder = new TextDecoder();
  for (let n = 0; n < count; n++) {
    if (u32(zip, p) !== 0x02014b50) throw new Error("damaged zip directory");
    const flags = u16(zip, p + 8);
    const method = u16(zip, p + 10);
    const crc = u32(zip, p + 16);
    const csize = u32(zip, p + 20);
    const size = u32(zip, p + 24);
    const nlen = u16(zip, p + 28);
    const elen = u16(zip, p + 30);
    const clen = u16(zip, p + 32);
    const local = u32(zip, p + 42);
    const path = decoder.decode(zip.subarray(p + 46, p + 46 + nlen));
    p += 46 + nlen + elen + clen;
    const base = path.split("/").pop();
    if (path.startsWith("__MACOSX/") || base.startsWith("._") || base.toLowerCase() !== want) continue;
    if (flags & 1) throw new Error("the zip member is encrypted");
    const start = local + 30 + u16(zip, local + 26) + u16(zip, local + 28);
    const raw = zip.subarray(start, start + csize);
    let data;
    if (method === 0) data = raw.slice();
    else if (method === 8) data = await inflateRaw(raw);
    else throw new Error(`unsupported zip compression method ${method}`);
    if (data.length !== size || crc32(data) !== crc) throw new Error("zip member CRC or size mismatch");
    return data;
  }
  return null;
}

export function writeStoredZip(entries) {
  const enc = new TextEncoder();
  const parts = [];
  const central = [];
  let offset = 0;
  for (const { name, data } of entries) {
    const nameBytes = enc.encode(name);
    const crc = crc32(data);
    const lh = new Uint8Array(30 + nameBytes.length);
    const lv = new DataView(lh.buffer);
    lv.setUint32(0, 0x04034b50, true); lv.setUint16(4, 20, true);
    lv.setUint32(14, crc, true); lv.setUint32(18, data.length, true); lv.setUint32(22, data.length, true);
    lv.setUint16(26, nameBytes.length, true);
    lh.set(nameBytes, 30);
    const ch = new Uint8Array(46 + nameBytes.length);
    const cv = new DataView(ch.buffer);
    cv.setUint32(0, 0x02014b50, true); cv.setUint16(4, 20, true); cv.setUint16(6, 20, true);
    cv.setUint32(16, crc, true); cv.setUint32(20, data.length, true); cv.setUint32(24, data.length, true);
    cv.setUint16(28, nameBytes.length, true); cv.setUint32(42, offset, true);
    ch.set(nameBytes, 46);
    parts.push(lh, data);
    central.push(ch);
    offset += lh.length + data.length;
  }
  const cdSize = central.reduce((s, c) => s + c.length, 0);
  const end = new Uint8Array(22);
  const ev = new DataView(end.buffer);
  ev.setUint32(0, 0x06054b50, true);
  ev.setUint16(8, entries.length, true); ev.setUint16(10, entries.length, true);
  ev.setUint32(12, cdSize, true); ev.setUint32(16, offset, true);
  const all = [...parts, ...central, end];
  const out = new Uint8Array(all.reduce((s, a) => s + a.length, 0));
  let o = 0;
  for (const a of all) { out.set(a, o); o += a.length; }
  return out;
}
