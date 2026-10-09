// Print the language, the image base and the memory blocks of the program.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.mem.MemoryBlock;

public class ProgramInfo extends GhidraScript {
    @Override
    public void run() throws Exception {
        println("language " + currentProgram.getLanguageID() + " compiler " + currentProgram.getCompilerSpec().getCompilerSpecID());
        println("base " + currentProgram.getImageBase());
        println("functions " + currentProgram.getFunctionManager().getFunctionCount());
        for (MemoryBlock b : currentProgram.getMemory().getBlocks())
            println("block " + b.getName() + " " + b.getStart() + " " + b.getEnd() + " " + (b.isInitialized() ? "init" : "uninit")
                    + (b.isExecute() ? " x" : ""));
    }
}
