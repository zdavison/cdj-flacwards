// Local browser test with Playwright and the fake firmware. It skips when
// Playwright is not available. Run it with: make test-browser
import { test } from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { spawn, execFileSync } from "node:child_process";
import { cpSync, readFileSync, writeFileSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const require = createRequire(import.meta.url);
let pw = null;
try { pw = require(process.env.PLAYWRIGHT_MODULE || "playwright"); } catch { pw = null; }
const skip = pw ? false : "no Playwright";

// Serve a copy of web/ with the fake manifest, and give fn a page and the site URL.
async function withPage(fn) {
  const site = mkdtempSync(join(tmpdir(), "site-"));
  cpSync("web", site, { recursive: true });
  cpSync("build/webtest/fake-manifest.json", join(site, "cdj900-4.32.json"));
  const port = 8765 + Math.floor(Math.random() * 1000);
  const server = spawn("python3", ["-m", "http.server", String(port), "--bind", "127.0.0.1"], { cwd: site, stdio: "ignore" });
  await new Promise((r) => setTimeout(r, 800));
  const browser = await pw.chromium.launch();
  try {
    const page = await browser.newPage({ acceptDownloads: true });
    await fn(page, `http://127.0.0.1:${port}/index.html`);
  } finally {
    await browser.close();
    server.kill();
  }
}

test("the page makes both downloads from the fake firmware", { skip }, () => withPage(async (page, url) => {
  await page.goto(url);
  await page.setInputFiles("#file", "build/webtest/fake.zip");
  await page.waitForSelector("#downloads:not([hidden])", { timeout: 120000 });
  const out = mkdtempSync(join(tmpdir(), "dl-"));
  for (const [id, inner, ref] of [["#dl-patched", "patched/C900MAIN.UPD", "fake-patched.upd"],
                                   ["#dl-rollback", "rollback/C900MAIN.UPD", "fake-rollback.upd"]]) {
    const [download] = await Promise.all([page.waitForEvent("download"), page.click(id)]);
    const path = join(out, download.suggestedFilename());
    await download.saveAs(path);
    const names = execFileSync("python3", ["-c", "import sys,zipfile;z=zipfile.ZipFile(sys.argv[1]);print(z.namelist()[0]);open(sys.argv[2],'wb').write(z.read(z.namelist()[0]))", path, join(out, "x")], { encoding: "utf8" }).trim();
    assert.equal(names, inner);
    assert.deepEqual(readFileSync(join(out, "x")), readFileSync("build/webtest/" + ref));
  }
  assert.match(await page.textContent("#dl-patched"), /4\.44/);
  // A wrong file shows the version message, no passed checks and no downloads.
  await page.reload();
  await page.setInputFiles("#file", "build/webtest/fake-patched.upd");
  await page.waitForSelector("#error:not([hidden])");
  assert.match(await page.textContent("#error"), /needs firmware 4\.32/);
  assert.equal(await page.locator("#steps li").count(), 0);
  assert.equal(await page.isHidden("#downloads"), true);
}));

test("a second file stops the run of the first file", { skip }, () => withPage(async (page, url) => {
  await page.goto(url);
  await page.setInputFiles("#file", "build/webtest/fake.zip");
  await page.setInputFiles("#file", "build/webtest/fake-patched.upd");
  await page.waitForSelector("#error:not([hidden])");
  await page.waitForTimeout(5000); // longer than a full run of the fake firmware
  assert.equal(await page.isHidden("#downloads"), true);
  assert.equal(await page.locator("#steps li").count(), 0);
  assert.match(await page.textContent("#error"), /needs firmware 4\.32/);
}));

test("a damaged zip says that the download is damaged", { skip }, () => withPage(async (page, url) => {
  const zip = readFileSync("build/webtest/fake.zip");
  const cut = join(mkdtempSync(join(tmpdir(), "cut-")), "CDJ-900v432.zip");
  writeFileSync(cut, zip.subarray(0, zip.length >> 1));
  await page.goto(url);
  await page.setInputFiles("#file", cut);
  await page.waitForSelector("#error:not([hidden])");
  const text = await page.textContent("#error");
  assert.match(text, /damaged/);
  assert.doesNotMatch(text, /report this/);
}));

test("a missing manifest shows an error", { skip }, () => withPage(async (page, url) => {
  await page.route("**/cdj900-4.32.json", (r) => r.abort());
  await page.goto(url);
  await page.waitForSelector("#error:not([hidden])");
  assert.equal(await page.isDisabled("#file"), true);
}));

test("a worker that cannot load shows an error", { skip }, () => withPage(async (page, url) => {
  await page.route("**/worker.js", (r) => r.abort());
  await page.goto(url);
  await page.setInputFiles("#file", "build/webtest/fake.zip");
  await page.waitForSelector("#error:not([hidden])", { timeout: 20000 });
  assert.equal(await page.isHidden("#downloads"), true);
}));
