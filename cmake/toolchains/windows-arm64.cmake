# Cross-compiles for Windows arm64 with llvm-mingw
# (https://github.com/mstorsjo/llvm-mingw). Set LLVM_MINGW_ROOT, or unpack it
# to ~/Toolchains/llvm-mingw.
#   cmake -S . -B build-windows-arm64 --toolchain cmake/toolchains/windows-arm64.cmake
set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR ARM64)

if(DEFINED ENV{LLVM_MINGW_ROOT})
    set(LLVM_MINGW_ROOT "$ENV{LLVM_MINGW_ROOT}")
else()
    set(LLVM_MINGW_ROOT "$ENV{HOME}/Toolchains/llvm-mingw")
endif()

set(triple aarch64-w64-mingw32)
set(CMAKE_C_COMPILER "${LLVM_MINGW_ROOT}/bin/${triple}-clang")
set(CMAKE_CXX_COMPILER "${LLVM_MINGW_ROOT}/bin/${triple}-clang++")
set(CMAKE_RC_COMPILER "${LLVM_MINGW_ROOT}/bin/${triple}-windres")
set(CMAKE_FIND_ROOT_PATH "${LLVM_MINGW_ROOT}/${triple}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
