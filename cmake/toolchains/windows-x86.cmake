# Cross-compiles for Windows x86 with llvm-mingw
# (https://github.com/mstorsjo/llvm-mingw). Set LLVM_MINGW_ROOT, or unpack it
# to ~/Toolchains/llvm-mingw.
#   cmake -S . -B build-windows-x86 --toolchain cmake/toolchains/windows-x86.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86)

if(DEFINED ENV{LLVM_MINGW_ROOT})
    set(LLVM_MINGW_ROOT "$ENV{LLVM_MINGW_ROOT}")
else()
    set(LLVM_MINGW_ROOT "$ENV{HOME}/Toolchains/llvm-mingw")
endif()

set(triple i686-w64-mingw32)
set(CMAKE_C_COMPILER "${LLVM_MINGW_ROOT}/bin/${triple}-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_ROOT}/bin/${triple}-clang++")
set(CMAKE_RC_COMPILER "${LLVM_MINGW_ROOT}/bin/${triple}-windres")
set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/${triple}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
