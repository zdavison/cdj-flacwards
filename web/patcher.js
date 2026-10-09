// Apply a patch manifest (tools/build_patch.py manifest) to the official
// update file. The steps and their order are the same as apply_manifest() in
// tools/build_patch.py, so the outputs are byte-identical.
import * as U from "./upd.js";
import { isZip, findMember } from "./zip.js";

export class PatchError extends Error {
  constructor(kind, message, extra = {}) {
    super(message);
    this.kind = kind;
    Object.assign(this, extra);
  }
}

export async function sha256Hex(bytes) {
  const digest = new Uint8Array(await crypto.subtle.digest("SHA-256", bytes));
  return Array.from(digest, (b) => b.toString(16).padStart(2, "0")).join("");
}

export async function readInput(bytes, m) {
  if (!isZip(bytes)) return bytes;
  let upd;
  try {
    upd = await findMember(bytes, m.upd_name);
  } catch (e) {
    throw new PatchError("bad-zip", `The zip file is damaged or cannot be read (${e.message}).`);
  }
  if (!upd) throw new PatchError("not-package", `This zip has no ${m.upd_name}. It is not the CDJ-900 firmware package.`);
  return upd;
}

function same(a, b) {
  if (a.length !== b.length) return false;
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return false;
  return true;
}

function fromHex(s) {
  return Uint8Array.from(s.match(/../g) ?? [], (h) => parseInt(h, 16));
}

function fromBase64(s) {
  return Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
}

export function rollbackUpd(upd, version) {
  const current = new TextDecoder("latin1").decode(upd.subarray(19, 23));
  if (!same(upd.subarray(0, U.HEADER_SIZE), U.header(current)))
    throw new PatchError("internal", "unexpected header");
  const out = new Uint8Array(upd.length);
  out.set(U.header(version));
  out.set(upd.subarray(U.HEADER_SIZE, upd.length - 2), U.HEADER_SIZE);
  const crc = U.crc16xmodem(out.subarray(0, out.length - 2));
  out[out.length - 2] = crc & 0xff;
  out[out.length - 1] = crc >>> 8;
  return out;
}

const internal = (message) => new PatchError("internal", `Internal check failed: ${message}`);

// progress(step) is called after the check of that step passed.
export async function applyManifest(upd, m, progress = () => {}) {
  const found = await sha256Hex(upd);
  if (found !== m.upd_sha256)
    throw new PatchError("wrong-version", `This tool needs firmware ${m.firmware}. Other versions are not supported.`,
      { expected: m.upd_sha256, found });
  const L = m.lzss;
  if (m.format !== 1 || m.app_region !== U.APP_REGION || m.app_region_end !== U.APP_REGION_END
      || m.app_base !== U.APP_BASE || L.n !== U.LZ_N || L.f !== U.LZ_F || L.min !== U.LZ_MIN || L.chain !== U.LZ_CHAIN)
    throw internal("the manifest does not match this patcher");
  progress("hash");

  const rollback = rollbackUpd(upd, m.rollback_version);
  progress("rollback");

  let flash, app;
  try {
    flash = U.decodeUpd(upd);
    app = U.unpackRegion(flash, m.app_region);
  } catch (e) {
    throw internal(e.message);
  }
  if (!U.checkImageSum(app)) throw internal("stock image sum mismatch");
  const at = (addr) => {
    const o = ((addr & 0x1fffffff) >>> 0) - m.app_base;
    if (o < 0 || o >= app.length) throw internal(`0x${addr.toString(16)} is outside the application image`);
    return o;
  };
  progress("unpack");

  for (const [lo, hi] of m.free) {
    if (app.subarray(at(lo), at(hi)).some((b) => b !== 0xff))
      throw internal(`padding 0x${lo.toString(16)}..0x${hi.toString(16)} is not free`);
  }
  const blob = fromBase64(m.blob);
  app.set(blob, at(m.blob_addr));
  for (const [addr, old, nw] of m.words) {
    const o = at(addr);
    const cur = (app[o] | (app[o + 1] << 8) | (app[o + 2] << 16) | (app[o + 3] << 24)) >>> 0;
    if (cur !== old) throw internal(`0x${addr.toString(16)}: unexpected stock word`);
    app[o] = nw & 0xff; app[o + 1] = (nw >>> 8) & 0xff; app[o + 2] = (nw >>> 16) & 0xff; app[o + 3] = nw >>> 24;
  }
  for (const [addr, oldHex, newHex] of m.bytes) {
    const old = fromHex(oldHex), nw = fromHex(newHex), o = at(addr);
    if (!same(app.subarray(o, o + old.length), old) || nw.length !== old.length)
      throw internal(`0x${addr.toString(16)}: unexpected stock bytes`);
    app.set(nw, o);
  }
  U.fixImageSum(app);
  progress("edit");

  let patched;
  try {
    const newFlash = U.buildFlash(flash, app);
    patched = U.encodeUpd(newFlash, m.patched_version, newFlash.length);
  } catch (e) {
    throw internal(e.message);
  }
  progress("pack");

  try {
    const check = U.decodeUpd(patched);
    const checkApp = U.unpackRegion(check, m.app_region);
    if (!same(checkApp, app)) throw new Error("the new file does not unpack to the patched image");
    if (!U.checkImageSum(checkApp)) throw new Error("image sum wrong in the new file");
    if (!same(check.subarray(0, m.app_region), flash.subarray(0, m.app_region)))
      throw new Error("boot ROM or loader changed");
  } catch (e) {
    throw internal(`self-check: ${e.message}`);
  }
  progress("self-check");
  return { patched, rollback };
}
