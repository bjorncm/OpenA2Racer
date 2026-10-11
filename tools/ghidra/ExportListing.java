import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import java.io.*;
public class ExportListing extends GhidraScript {
    @Override
    public void run() throws Exception {
        File out = new File(getScriptArgs()[0], currentProgram.getName() + ".listing");
        Listing listing = currentProgram.getListing();
        try (PrintWriter w = new PrintWriter(new FileWriter(out))) {
            InstructionIterator it = listing.getInstructions(true);
            while (it.hasNext()) {
                Instruction i = it.next();
                Function f = currentProgram.getFunctionManager().getFunctionContaining(i.getAddress());
                w.println(i.getAddress() + "  " + (f == null ? "-" : f.getName()) + "  " + i);
            }
        }
    }
}
