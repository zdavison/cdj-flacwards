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
  return "DecompressionStream" in window && window.crypto?.subtle && "Worker" in window;
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
  return `${e.message} No file was made. Please report this on GitHub.`;
}

function offer(id, name, inner, data, label) {
  const zip = writeStoredZip([{ name: inner, data }]);
  const a = $(id);
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
  const manifest = await (await fetch("cdj900-4.32.json")).json();
  const run = async (file) => {
    $("steps").replaceChildren();
    $("error").hidden = true;
    $("downloads").hidden = true;
    const worker = new Worker("worker.js", { type: "module" });
    worker.onmessage = async ({ data }) => {
      if (data.type === "progress") showStep(data.step);
      else if (data.type === "error") { showError(errorText(data)); worker.terminate(); }
      else if (data.type === "done") {
        const patched = new Uint8Array(data.patched), rollback = new Uint8Array(data.rollback);
        const pv = manifest.patched_version, rv = manifest.rollback_version;
        offer("dl-patched", `cdj-flacwards-${pv}.zip`, "patched/C900MAIN.UPD", patched, `Download patched C900MAIN.UPD (${pv})`);
        offer("dl-rollback", `cdj900-rollback-${rv}.zip`, "rollback/C900MAIN.UPD", rollback, `Download rollback C900MAIN.UPD (${rv})`);
        $("hashes").textContent = `SHA-256 patched: ${await sha256Hex(patched)}. SHA-256 rollback: ${await sha256Hex(rollback)}.`;
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
