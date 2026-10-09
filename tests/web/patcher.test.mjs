// Tests of web/patcher.js on the fake firmware (tests/fake_firmware.py).
// The Python outputs in build/webtest are the reference.
import { test } from "node:test";
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { applyManifest, readInput, PatchError } from "../../web/patcher.js";

const D = "build/webtest/";
const bytes = (f) => new Uint8Array(readFileSync(D + f));
const json = (f) => JSON.parse(readFileSync(D + f, "utf8"));
const m = json("fake-manifest.json");

test("patched and rollback equal Python", async () => {
  const steps = [];
  const r = await applyManifest(bytes("fake.upd"), m, (s) => steps.push(s));
  assert.deepEqual(r.patched, bytes("fake-patched.upd"));
  assert.deepEqual(r.rollback, bytes("fake-rollback.upd"));
  assert.deepEqual(steps, ["hash", "rollback", "unpack", "edit", "pack", "self-check"]);
});

test("readInput takes a deflated zip, a stored zip and a bare file", async () => {
  const upd = bytes("fake.upd");
  assert.deepEqual(await readInput(bytes("fake.zip"), m), upd);
  assert.deepEqual(await readInput(bytes("fake-stored.zip"), m), upd);
  assert.deepEqual(await readInput(upd, m), upd);
});

test("a zip without the update file is not the package", async () => {
  const { writeStoredZip } = await import("../../web/zip.js");
  const z = writeStoredZip([{ name: "CDJ-900v432/C900GUI.UPD", data: new Uint8Array(10) }]);
  await assert.rejects(readInput(z, m), (e) => e instanceof PatchError && e.kind === "not-package");
});

test("already-patched", async () => {
  await assert.rejects(applyManifest(bytes("fake-patched.upd"), m),
    (e) => e instanceof PatchError && e.kind === "wrong-version" && e.expected === m.upd_sha256);
});

test("a wrong stock word is an internal error", async () => {
  await assert.rejects(applyManifest(bytes("fake.upd"), json("bad-word-manifest.json")),
    (e) => e instanceof PatchError && e.kind === "internal");
});

test("padding that is not free is an internal error", async () => {
  await assert.rejects(applyManifest(bytes("fake.upd"), json("bad-padding-manifest.json")),
    (e) => e instanceof PatchError && e.kind === "internal");
});

test("a manifest with other LZSS settings is refused", async () => {
  await assert.rejects(applyManifest(bytes("fake.upd"), { ...m, lzss: { ...m.lzss, chain: 16 } }),
    (e) => e instanceof PatchError && e.kind === "internal");
});
