# CMake toolchain for cross-compiling to aarch64-linux using zig.
# zig bundles its own sysroot/libc, so no apt crossbuild-essential is needed.
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)

# Locate zig (installed via pip; `zig` is on PATH inside CI).
find_program(ZIG_EXECUTABLE zig REQUIRED)

set(CMAKE_C_COMPILER "${ZIG_EXECUTABLE}" CACHE STRING "" FORCE)
set(CMAKE_CXX_COMPILER "${ZIG_EXECUTABLE}" CACHE STRING "" FORCE)

# Target flags: zig cc/c++ take --target as the first compiler argument.
set(ZIG_TARGET "aarch64-linux-gnu")
set(CMAKE_C_FLAGS "--target=${ZIG_TARGET} -fPIC" CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS "--target=${ZIG_TARGET} -fPIC" CACHE STRING "" FORCE)

# zig links its own libc; don't search the host toolchain for target libs.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
