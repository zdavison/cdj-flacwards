// Create functions at every code pointer in the CDJ-900 MAIN image.
// SH-4 code reaches functions through 32-bit literal-pool words (0x04xxxxxx),
// so each word that points to a plausible prologue is a function start.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;

public class SeedFunctions extends GhidraScript {
    static final long BASE = 0x04000000L;
    static final long CODE_LO = 0x0405c400L;

    boolean prologue(int w) {
        return (w & 0xFF0F) == 0x2F06        // mov.l Rm,@-r15
            || w == 0x4F22                   // sts.l pr,@-r15
            || (w & 0xFF80) == 0x7F80;       // add #-imm,r15
    }

    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        Address start = toAddr(BASE);
        long size = mem.getBlock(start).getSize();
        long end = BASE + size;
        int made = 0;
        for (long a = BASE; a + 4 <= end; a += 4) {
            long v = mem.getInt(toAddr(a)) & 0xFFFFFFFFL;
            long p = v & 0x1FFFFFFFL;
            if (p < CODE_LO || p >= end || (p & 1) != 0) continue;
            int w = mem.getShort(toAddr(p)) & 0xFFFF;
            if (!prologue(w)) continue;
            Address t = toAddr(p);
            if (getFunctionAt(t) != null) continue;
            disassemble(t);
            if (createFunction(t, null) != null) made++;
        }
        println("SeedFunctions: created " + made + " functions");
    }
}
