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
set(DAISYSP_DIR ${CMAKE_CURRENT_LIST_DIR}/lib/DaisySP)
add_subdirectory(${DAISYSP_DIR} DaisySP)
set(DAISYSP_LIB DaisySP)

project("basic-monosynth" VERSION 0.1.0)
set(FIRMWARE_NAME "basic-monosynth")
set(FIRMWARE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/main.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/knob.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/Voice.cpp
)

# Add include directory for header files
include_directories(${CMAKE_CURRENT_LIST_DIR}/src)


set(DAISY_GENERATE_BIN ON)
include(DaisyProject)

project("awful-paraphonic-synth" VERSION 0.1.0)
set(FIRMWARE_NAME "awful-paraphonic-synth")
set(FIRMWARE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/awful-paraphonic.cpp
)

set(DAISY_GENERATE_BIN ON)
include(DaisyProject)

project("midi-test" VERSION 0.1.0)
set(FIRMWARE_NAME "midi-test")
set(FIRMWARE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/basic-midi.cpp
)

set(DAISY_GENERATE_BIN ON)
include(DaisyProject)