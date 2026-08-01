cmake_minimum_required(VERSION 3.26)
cmake_policy(SET CMP0048 NEW)
cmake_policy(SET CMP0175 OLD)

include(${CMAKE_CURRENT_LIST_DIR}/../../shared/cmake/daisybed.cmake)

project("midi-test" VERSION 0.1.0)
set(FIRMWARE_NAME "midi-test")
set(FIRMWARE_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/src/basic-midi.cpp
)

set(DAISY_GENERATE_BIN ON)
include(DaisyProject)
