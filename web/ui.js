// The page: reads the selected file, runs the worker, shows the checks and the downloads.
import { writeStoredZip } from "./zip.js";
import { sha256Hex } from "./patcher.js";

const STEPS = {
  hash: "The file is the official firmware 4.32 (SHA-256)",
  rollback: "Rollback file made",
  unpack: "Firmware unpacked, checksum correct",
  edit: "Patch applied, every stock value as expected",
  pack: "Update file packed",
  "self-check": "New file checked",
};
const $ = (id) => document.getElementById(id);

function supported() {
  if (!(window.crypto?.subtle && "Worker" in window)) return false;
  try {
    new DecompressionStream("deflate-raw");
    return true;
  } catch {
    return false;
  }
}

function showStep(step) {
  const li = document.createElement("li");
  li.textContent = STEPS[step] ?? step;
  $("steps").appendChild(li);
}

function showError(msg) {
  $("error").textContent = msg;
  $("error").hidden = false;
}

function errorText(e) {
  if (e.kind === "wrong-version")
    return `${e.message} Expected SHA-256 ${e.expected}, found ${e.found}.`;
  if (e.kind === "not-package") return `${e.message} Download CDJ-900v432.zip from the AlphaTheta support site.`;
  if (e.kind === "bad-zip") return `${e.message} Download CDJ-900v432.zip again.`;
  return `${e.message}. No file was made. Please report this on GitHub.`;
}

function offer(id, name, inner, data, label) {
  const zip = writeStoredZip([{ name: inner, data }]);
  const a = $(id);
  if (a.href) URL.revokeObjectURL(a.href);
  a.href = URL.createObjectURL(new Blob([zip], { type: "application/zip" }));
  a.download = name;
  a.textContent = label;
}

async function main() {
  if (!supported()) {
    $("unsupported").hidden = false;
    $("file").disabled = true;
    return;
  }
  let manifest;
  try {
    const r = await fetch("cdj900-4.32.json");
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    manifest = await r.json();
  } catch (e) {
    showError(`The page could not load its patch data (${e.message}). Reload the page.`);
    $("file").disabled = true;
    return;
  }
  // Only the newest run may change the page. A new file stops the old run.
  let current = null;
  const run = async (file) => {
    if (current) current.terminate();
    const worker = new Worker("worker.js", { type: "module" });
    current = worker;
    $("steps").replaceChildren();
    $("error").hidden = true;
    $("downloads").hidden = true;
    worker.onerror = (e) => {
      if (worker !== current) return;
      e.preventDefault();
      showError("The patcher could not start in this browser. Use a current version of Firefox, Chrome, Edge or Safari.");
      worker.terminate();
    };
    worker.onmessage = async ({ data }) => {
      if (worker !== current) return;
      if (data.type === "progress") showStep(data.step);
      else if (data.type === "error") { showError(errorText(data)); worker.terminate(); }
      else if (data.type === "done") {
        const patched = new Uint8Array(data.patched), rollback = new Uint8Array(data.rollback);
        const pv = manifest.patched_version, rv = manifest.rollback_version;
        offer("dl-patched", `cdj-flacwards-${pv}.zip`, "patched/C900MAIN.UPD", patched, `Download patched C900MAIN.UPD (${pv})`);
        offer("dl-rollback", `cdj900-rollback-${rv}.zip`, "rollback/C900MAIN.UPD", rollback, `Download rollback C900MAIN.UPD (${rv})`);
        const hashes = `SHA-256 patched: ${await sha256Hex(patched)}. SHA-256 rollback: ${await sha256Hex(rollback)}.`;
        if (worker !== current) return;
        $("hashes").textContent = hashes;
        $("downloads").hidden = false;
        worker.terminate();
      }
    };
    worker.postMessage({ file: await file.arrayBuffer(), manifest });
  };
  $("file").addEventListener("change", (e) => { if (e.target.files[0]) run(e.target.files[0]); });
  const drop = $("drop");
  drop.addEventListener("dragover", (e) => { e.preventDefault(); drop.classList.add("over"); });
  drop.addEventListener("dragleave", () => drop.classList.remove("over"));
  drop.addEventListener("drop", (e) => {
    e.preventDefault();
    drop.classList.remove("over");
    if (e.dataTransfer.files[0]) run(e.dataTransfer.files[0]);
  });
}

main();
