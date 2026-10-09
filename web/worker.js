// Runs the patcher off the page's main thread.
import { applyManifest, readInput } from "./patcher.js";

self.onmessage = async (event) => {
  const { file, manifest } = event.data;
  try {
    const upd = await readInput(new Uint8Array(file), manifest);
    const r = await applyManifest(upd, manifest, (step) => self.postMessage({ type: "progress", step }));
    self.postMessage({ type: "done", patched: r.patched.buffer, rollback: r.rollback.buffer },
      [r.patched.buffer, r.rollback.buffer]);
  } catch (e) {
    self.postMessage({ type: "error", kind: e.kind || "internal", message: e.message,
      expected: e.expected, found: e.found });
  }
};
