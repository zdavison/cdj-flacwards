// Print the functions that reference each address given as a script argument.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;

public class Callers extends GhidraScript {
    @Override
    public void run() throws Exception {
        for (String arg : getScriptArgs()) {
            Address a = toAddr(Long.decode(arg) & 0x1FFFFFFFL);
            println("// refs to " + a);
            for (Reference r : getReferencesTo(a)) {
                Function f = getFunctionContaining(r.getFromAddress());
                println("   " + r.getFromAddress() + " " + r.getReferenceType()
                        + " in " + (f == null ? "?" : f.getName() + "@" + f.getEntryPoint()));
            }
        }
    }
}
