// Tests of web/upd.js. The Python module tools/upd.py is the reference.
import { test } from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, writeFileSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import * as U from "../../web/upd.js";

const dir = mkdtempSync(join(tmpdir(), "updjs-"));
const py = (...args) => execFileSync("python3", ["tests/web/pyref.py", ...args], { encoding: "utf8" });

// Deterministic test data: compressible, with repeats and random runs.
function sample(n, seed) {
  let x = seed >>> 0 || 1;
  const rnd = () => { x ^= x << 13; x >>>= 0; x ^= x >>> 17; x ^= x << 5; x >>>= 0; return x; };
  const words = Array.from({ length: 48 }, () => rnd());
  const out = new Uint8Array(n);
  for (let i = 0; i < n; i += 4) {
    const w = (rnd() & 3) === 0 ? rnd() : words[rnd() % 48];
    out[i] = w; out[i + 1] = w >>> 8; out[i + 2] = w >>> 16; out[i + 3] = w >>> 24;
  }
  return out;
}

test("crc16xmodem check value", () => {
  assert.equal(U.crc16xmodem(new TextEncoder().encode("123456789")), 0x31c3);
});

test("header equals Python", () => {
  py("header", "4.44", join(dir, "h"));
  assert.deepEqual(U.header("4.44"), new Uint8Array(readFileSync(join(dir, "h"))));
  assert.throws(() => U.header("4.4"));
});

for (const [n, seed] of [[0, 1], [1, 2], [17, 3], [5000, 4], [200000, 5]]) {
  test(`lzssCompress equals Python (${n} bytes)`, () => {
    const data = sample(n, seed);
    writeFileSync(join(dir, "in"), data);
    py("compress", join(dir, "in"), join(dir, "out"));
    const ref = new Uint8Array(readFileSync(join(dir, "out")));
    assert.deepEqual(U.lzssCompress(data), ref);
    assert.deepEqual(U.lzssDecompress(ref), data);
  });
}

test("lzss round trip on runs of one byte", () => {
  const data = new Uint8Array(100000).fill(0x20);
  data.fill(0xff, 40000, 90000);
  assert.deepEqual(U.lzssDecompress(U.lzssCompress(data)), data);
});

test("encodeUpd and decodeUpd equal Python", () => {
  const image = sample(70000, 9);
  image.fill(0, 1000, 1100); // an all-zero record is left out
  writeFileSync(join(dir, "img"), image);
  py("encode", join(dir, "img"), join(dir, "upd"), "4.44");
  const ref = new Uint8Array(readFileSync(join(dir, "upd")));
  assert.deepEqual(U.encodeUpd(image, "4.44", image.length), ref);
  py("decode", join(dir, "upd"), join(dir, "dec"));
  assert.deepEqual(U.decodeUpd(ref), new Uint8Array(readFileSync(join(dir, "dec"))));
});

test("decodeUpd refuses a bad CRC", () => {
  const upd = U.encodeUpd(sample(1000, 3), "4.44", 1000);
  upd[100] ^= 1;
  assert.throws(() => U.decodeUpd(upd), /CRC/);
});

test("imageSum equals Python, and fixImageSum makes it check", () => {
  const app = sample(4096, 11);
  writeFileSync(join(dir, "app"), app);
  assert.equal(U.imageSum(app), Number(py("imagesum", join(dir, "app")).trim()));
  U.fixImageSum(app);
  assert.ok(U.checkImageSum(app));
});

test("packRegion and unpackRegion round trip, with the byte sum", () => {
  const app = sample(30000, 12);
  const region = U.packRegion(app);
  const flash = new Uint8Array(U.APP_REGION + region.length);
  flash.set(region, U.APP_REGION);
  assert.deepEqual(U.unpackRegion(flash, U.APP_REGION), app);
  flash[U.APP_REGION + 10] ^= 1;
  assert.throws(() => U.unpackRegion(flash, U.APP_REGION), /sum/);
});
