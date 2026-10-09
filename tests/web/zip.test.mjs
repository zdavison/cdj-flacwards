// Tests of web/zip.js. Python's zipfile writes and reads the reference zips.
import { test } from "node:test";
import assert from "node:assert/strict";
import { execFileSync } from "node:child_process";
import { mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { findMember, writeStoredZip, isZip, crc32 } from "../../web/zip.js";

const dir = mkdtempSync(join(tmpdir(), "zipjs-"));
const payload = new Uint8Array(70000).map((_, i) => (i * 7 + (i >> 9)) & 0xff);
writeFileSync(join(dir, "payload"), payload);

// Make a zip with Python: entries are "name:method" (method stored or deflated).
function pyzip(file, ...entries) {
  execFileSync("python3", ["-c", `
import sys, zipfile
data = open(sys.argv[1], "rb").read()
with zipfile.ZipFile(sys.argv[2], "w") as z:
    for e in sys.argv[3:]:
        name, method = e.split(":")
        m = zipfile.ZIP_STORED if method == "stored" else zipfile.ZIP_DEFLATED
        z.writestr(zipfile.ZipInfo(name), data if not name.split("/")[-1].startswith("._") else b"junk", m)
`, join(dir, "payload"), join(dir, file), ...entries]);
  return new Uint8Array(readFileSync(join(dir, file)));
}

test("crc32 check value", () => {
  assert.equal(crc32(new TextEncoder().encode("123456789")), 0xcbf43926);
});

test("deflated member", async () => {
  const z = pyzip("a.zip", "CDJ-900v432/C900MAIN.UPD:deflated", "CDJ-900v432/C900GUI.UPD:deflated");
  assert.ok(isZip(z));
  assert.deepEqual(await findMember(z, "C900MAIN.UPD"), payload);
});

test("stored", async () => {
  const z = pyzip("b.zip", "CDJ-900v432/C900MAIN.UPD:stored");
  assert.deepEqual(await findMember(z, "C900MAIN.UPD"), payload);
});

test("appledouble", async () => {
  const z = pyzip("c.zip", "__MACOSX/CDJ-900v432/._C900MAIN.UPD:deflated", "CDJ-900v432/C900MAIN.UPD:deflated");
  assert.deepEqual(await findMember(z, "C900MAIN.UPD"), payload);
});

test("case", async () => {
  const z = pyzip("d.zip", "cdj-900v432/c900main.upd:deflated");
  assert.deepEqual(await findMember(z, "C900MAIN.UPD"), payload);
});

test("missing member gives null", async () => {
  const z = pyzip("e.zip", "CDJ-900v432/C900GUI.UPD:deflated");
  assert.equal(await findMember(z, "C900MAIN.UPD"), null);
});

test("damaged member is refused", async () => {
  const z = pyzip("f.zip", "C900MAIN.UPD:stored");
  z[100] ^= 1; // inside the stored data
  await assert.rejects(findMember(z, "C900MAIN.UPD"), /CRC/);
});

test("writeStoredZip is readable by Python", () => {
  const z = writeStoredZip([{ name: "patched/C900MAIN.UPD", data: payload }]);
  writeFileSync(join(dir, "w.zip"), z);
  const out = execFileSync("python3", ["-c", `
import sys, zipfile
z = zipfile.ZipFile(sys.argv[1])
assert z.testzip() is None
print(z.namelist()[0], len(z.read(z.namelist()[0])))
`, join(dir, "w.zip")], { encoding: "utf8" }).trim();
  assert.equal(out, `patched/C900MAIN.UPD ${payload.length}`);
});
