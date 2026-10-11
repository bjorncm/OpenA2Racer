# Ghidra scripts

These scripts were used to analyse the original game modules (`A2.EXE`,
`A2MENU.DAT`, `A2MOVIE.DAT`, `A2SPEL.DAT`) whose logic OpenA2Racer ports.
Their output (decompiled C, disassembly, listings) and the Ghidra projects are
working material and are never committed.

| Script | Kind | What it does |
|---|---|---|
| `PrepWatcom.java` | pre | Stops non-return discovery, which Watcom binaries confuse. |
| `ApplyWatcall.java` | post | Puts the Watcom register calling convention on all internal functions and re-runs analysis. |
| `ExportDecomp.java` | post | Writes the decompiled C of every function to `<out>/<program>.c`. |
| `ExportAsm.java` | post | Writes the disassembly of every function to `<out>/<program>.asm`. |
| `ExportListing.java` | post | Writes every instruction to `<out>/<program>.listing`, including code Ghidra leaves outside functions. |

## Setup

1. Install Ghidra 12.x and JDK 21. Ghidra has no native decompiler for Apple
   silicon; build one and copy `ghidra_opt` to
   `Ghidra/Features/Decompiler/os/mac_arm_64/decompile`:
   ```sh
   cd Ghidra/Features/Decompiler/src/decompile/cpp
   make ghidra_opt ARCH_TYPE="-arch arm64"
   ```
2. The modules are Watcom C++ with register calls. Add a `__watcall`
   prototype to Ghidra's `x86win.cspec` and make it the `default_proto`:
   - inputs: EAX, EDX, EBX, ECX, then the stack
   - outputs: EAX, or EDX:EAX
   - unaffected: ESP, EBP, ESI, EDI, EBX, ECX
3. Run Ghidra headless from the repository root:
   ```sh
   analyzeHeadless <project-dir> a2 -import <game-folder>/A2MENU.DAT \
       -scriptPath tools/ghidra -preScript PrepWatcom.java \
       -postScript ApplyWatcall.java -postScript ExportDecomp.java <out> \
       -postScript ExportAsm.java <out>
   ```
   For `A2SPEL.DAT`, also add `-postScript ExportListing.java <out>`. About
   40 KB of its code is reached only through jump tables, and Ghidra doesn't
   define functions for it.

`A2MENU.DAT` has a Watcom debug symbol table after its PE sections, with 906
`W?name$...` records. Each record is a u32 offset, a u16 segment
(1 = code at 0x401000, 2 = data at 0x425000), a u16 module, a u8 kind, a u8
length and the name. Decoding it gives the original function names. The other
modules have no symbol table.
