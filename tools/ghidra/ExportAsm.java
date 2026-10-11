// Exports the disassembly of every function to <out>/<program>.asm
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;
public class ExportAsm extends GhidraScript {
    @Override
    public void run() throws Exception {
        File out = new File(getScriptArgs()[0], currentProgram.getName() + ".asm");
        Listing listing = currentProgram.getListing();
        try (PrintWriter w = new PrintWriter(new FileWriter(out))) {
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                w.println("; ===== " + f.getEntryPoint() + " " + f.getName());
                for (Instruction i : listing.getInstructions(f.getBody(), true)) {
                    w.println(i.getAddress() + "  " + i.toString());
                }
            }
        }
    }
}
