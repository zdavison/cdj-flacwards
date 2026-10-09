// Decompile every function into OUTDIR/<4-hex-digit bucket>.c, with the
// strings each function references listed above it. Local use only: the
// output is derived from Pioneer firmware and must never be published.
// Usage: -postScript DecompileAll.java OUTDIR
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.address.Address;
import ghidra.program.model.symbol.Reference;
import java.io.*;
import java.nio.charset.Charset;
import java.util.*;

public class DecompileAll extends GhidraScript {
    String cstr(Memory mem, long addr) {
        try {
            Address a = toAddr(addr & 0x1FFFFFFFL);
            byte[] b = new byte[96];
            int n = mem.getBytes(a, b);
            int len = 0;
            while (len < n && b[len] != 0) len++;
            if (len < 3) return null;
            String s = new String(b, 0, len, Charset.forName("windows-31j"));
            int printable = 0;
            for (char c : s.toCharArray()) if (c >= 0x20 && c != 0xfffd) printable++;
            return printable == s.length() ? s : null;
        } catch (Exception e) { return null; }
    }

    @Override
    public void run() throws Exception {
        File out = new File(getScriptArgs()[0]);
        out.mkdirs();
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        Memory mem = currentProgram.getMemory();
        Map<String, PrintWriter> files = new HashMap<>();
        int count = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (monitor.isCancelled()) break;
            String bucket = String.format("%04x", f.getEntryPoint().getOffset() >> 16);
            PrintWriter w = files.get(bucket);
            if (w == null) {
                w = new PrintWriter(new OutputStreamWriter(
                        new FileOutputStream(new File(out, bucket + ".c")), "UTF-8"));
                files.put(bucket, w);
            }
            // Strings referenced through literal pools inside the function body.
            Set<String> strs = new LinkedHashSet<>();
            for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
                for (Reference r : ins.getReferencesFrom()) {
                    try {
                        long v = mem.getInt(r.getToAddress()) & 0xFFFFFFFFL;
                        if ((v >> 24) == 0xa4 || (v >> 24) == 0x04) {
                            String s = cstr(mem, v);
                            if (s != null) strs.add(s.replace("\r", "\\r").replace("\n", "\\n"));
                        }
                    } catch (Exception e) { }
                }
            }
            w.println("// ===== " + f.getName() + " @ " + f.getEntryPoint());
            for (String s : strs) w.println("//   str: \"" + s + "\"");
            DecompileResults r = ifc.decompileFunction(f, 60, monitor);
            w.println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// decompile failed");
            if (++count % 200 == 0) println("decompiled " + count);
        }
        for (PrintWriter w : files.values()) w.close();
        println("DecompileAll: " + count + " functions");
    }
}
