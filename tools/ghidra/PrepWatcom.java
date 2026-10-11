// Pre-analysis: Watcom binaries confuse non-return discovery.
import ghidra.app.script.GhidraScript;
public class PrepWatcom extends GhidraScript {
    @Override
    public void run() throws Exception {
        setAnalysisOption(currentProgram, "Non-Returning Functions - Discovered", "false");
    }
}
