cmake_minimum_required(VERSION 3.26)
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0175 OLD)

# Load libDaisy first, it sets up the toolchain and project
# to use all the ARM Cross-compiler stuff
# set(DAISY_STORAGE QSPI)
set(LIBDAISY_DIR ${CMAKE_CURRENT_LIST_DIR}/lib/libDaisy)
add_subdirectory(${LIBDAISY_DIR} daisy)
set(LIBDAISY_LIB daisy)

# Loading in DaisySP
# TODO: figure out how to load in the 
set(DAISYSP_DIR ${CMAKE_CURRENT_LIST_DIR}/lib/DaisySP)
add_subdirectory(${DAISYSP_DIR} DaisySP)
set(DAISYSP_LIB DaisySP)

# Our firmware project
project("test-synth" VERSION 0.1.0)
set(FIRMWARE_NAME "test-synth")
set(FIRMWARE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/main.cpp
)

# Use the DaisyProject helper to set up our firmware project
set(DAISY_GENERATE_BIN ON)
include(DaisyProject)
