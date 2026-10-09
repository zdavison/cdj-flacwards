// Print decompiled C for each function address given as a script argument.
// Usage: -postScript Decompile.java 0x041c33c0 0x04164a2a ...
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;

public class Decompile extends GhidraScript {
    @Override
    public void run() throws Exception {
        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);
        for (String arg : getScriptArgs()) {
            Address a = toAddr(Long.decode(arg) & 0x1FFFFFFFL);
            Function f = getFunctionContaining(a);
            if (f == null) {
                disassemble(a);
                f = createFunction(a, null);
            }
            if (f == null) { println("// no function at " + a); continue; }
            DecompileResults r = ifc.decompileFunction(f, 120, monitor);
            println("// ===== " + f.getName() + " @ " + f.getEntryPoint());
            println(r.decompileCompleted() ? r.getDecompiledFunction().getC() : "// failed: " + r.getErrorMessage());
        }
    }
}
