// Print the register context values that are set at each address given.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.lang.Register;
import ghidra.program.model.listing.ProgramContext;
import java.math.BigInteger;

public class ContextInfo extends GhidraScript {
    @Override
    public void run() throws Exception {
        ProgramContext pc = currentProgram.getProgramContext();
        for (String arg : getScriptArgs()) {
            Address a = toAddr(Long.decode(arg) & 0x1FFFFFFFL);
            for (Register r : pc.getRegisters()) {
                BigInteger v = pc.getValue(r, a, false);
                if (v != null) println(a + " " + r.getName() + " = 0x" + v.toString(16));
            }
        }
    }
}
