// Local browser test with Playwright and the fake firmware. It skips when
// Playwright is not available. Run it with: make test-browser
import { test } from "node:test";
import assert from "node:assert/strict";
import { createRequire } from "node:module";
import { spawn, execFileSync } from "node:child_process";
import { cpSync, mkdirSync, readFileSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";

const require = createRequire(import.meta.url);
let pw = null;
try { pw = require(process.env.PLAYWRIGHT_MODULE || "playwright"); } catch { pw = null; }

test("the page makes both downloads from the fake firmware", { skip: pw ? false : "no Playwright" }, async () => {
  const site = mkdtempSync(join(tmpdir(), "site-"));
  cpSync("web", site, { recursive: true });
  cpSync("build/webtest/fake-manifest.json", join(site, "cdj900-4.32.json"));
  const port = 8765 + Math.floor(Math.random() * 1000);
  const server = spawn("python3", ["-m", "http.server", String(port), "--bind", "127.0.0.1"], { cwd: site, stdio: "ignore" });
  await new Promise((r) => setTimeout(r, 800));
  const browser = await pw.chromium.launch();
  try {
    const page = await browser.newPage({ acceptDownloads: true });
    await page.goto(`http://127.0.0.1:${port}/index.html`);
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
    // A wrong file shows the version message and no downloads.
    await page.reload();
    await page.setInputFiles("#file", "build/webtest/fake-patched.upd");
    await page.waitForSelector("#error:not([hidden])");
    assert.match(await page.textContent("#error"), /needs firmware 4\.32/);
    assert.equal(await page.isHidden("#downloads"), true);
  } finally {
    await browser.close();
    server.kill();
  }
});
