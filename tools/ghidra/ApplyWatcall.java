// Sets the Watcom register calling convention on all internal functions,
// then re-runs analysis so parameters and returns are recovered.
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.SourceType;
public class ApplyWatcall extends GhidraScript {
    @Override
    public void run() throws Exception {
        int count = 0;
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (f.isExternal() || f.isThunk()) continue;
            if (f.getName().startsWith("FUN_") || f.getSignatureSource() == SourceType.DEFAULT
                || f.getSignatureSource() == SourceType.ANALYSIS) {
                f.setCallingConvention("__watcall");
                f.setNoReturn(false);
                count++;
            }
        }
        println("watcall applied to " + count);
    }
}
