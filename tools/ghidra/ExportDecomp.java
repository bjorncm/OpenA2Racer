// Exports decompiled C for every function to <out>/<program>.c
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;

public class ExportDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String outDir = getScriptArgs()[0];
        DecompInterface decomp = new DecompInterface();
        decomp.openProgram(currentProgram);
        File out = new File(outDir, currentProgram.getName() + ".c");
        try (PrintWriter writer = new PrintWriter(new FileWriter(out))) {
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            for (Function function : functions) {
                if (monitor.isCancelled()) break;
                DecompileResults result = decomp.decompileFunction(function, 60, monitor);
                writer.println("// ===== " + function.getEntryPoint() + " " + function.getName());
                if (result != null && result.decompileCompleted()) {
                    writer.println(result.getDecompiledFunction().getC());
                } else {
                    writer.println("// decompile failed");
                }
            }
        }
        decomp.dispose();
    }
}
