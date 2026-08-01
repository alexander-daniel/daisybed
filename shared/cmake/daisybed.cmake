# Included by each firmware project to bring in libDaisy + DaisySP.
#
# Each project is a standalone CMake project: you configure it with
#   cmake -S projects/<name> -B build/<name>
# and build/flash just that firmware. libDaisy's autodetect.cmake runs in
# this same scope, so the ARM cross-compiler is selected before project()
# locks it in -- the same ordering that made the original flat layout work.
#
# The including project CMakeLists.txt owns cmake_minimum_required() and the
# cmake_policy() calls; this file is pulled in via include() before project().

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Resolve the repo root from this file's location (shared/cmake/daisybed.cmake).
get_filename_component(_DAISYBED_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(LIBDAISY_DIR ${_DAISYBED_ROOT}/lib/libDaisy)
add_subdirectory(${LIBDAISY_DIR} daisy)
set(LIBDAISY_LIB daisy)

set(DAISYSP_DIR ${_DAISYBED_ROOT}/lib/DaisySP)
add_subdirectory(${DAISYSP_DIR} DaisySP)
set(DAISYSP_LIB DaisySP)

# Reusable helpers (knob, Voice, DattorroPlate, ...). Added to the include path
# globally here so each firmware's DaisyProject-created target sees them without
# needing an explicit target_link_libraries (which we can't do since DaisyProject
# owns the target). Projects that need a .cpp from shared/ list it in their own
# FIRMWARE_SOURCES.
include_directories(${_DAISYBED_ROOT}/shared)
