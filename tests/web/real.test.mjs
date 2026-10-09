// Local test with the real firmware. It skips when the files are missing.
// Make them first: make && python3 tools/build_patch.py release rollback manifest
// (each target separately: release, then rollback, then manifest).
import { test } from "node:test";
import assert from "node:assert/strict";
import { existsSync, readFileSync } from "node:fs";
import { applyManifest, readInput } from "../../web/patcher.js";

const files = ["CDJ-900v432.zip", "out/web/cdj900-4.32.json", "out/release/C900MAIN.UPD", "out/rollback/C900MAIN.UPD"];
const missing = files.filter((f) => !existsSync(f));

test("real firmware: equal to tools/build_patch.py", { skip: missing.length ? `missing ${missing.join(", ")}` : false }, async () => {
  const m = JSON.parse(readFileSync("out/web/cdj900-4.32.json", "utf8"));
  const upd = await readInput(new Uint8Array(readFileSync("CDJ-900v432.zip")), m);
  const r = await applyManifest(upd, m);
  assert.deepEqual(r.patched, new Uint8Array(readFileSync("out/release/C900MAIN.UPD")));
  assert.deepEqual(r.rollback, new Uint8Array(readFileSync("out/rollback/C900MAIN.UPD")));
});
