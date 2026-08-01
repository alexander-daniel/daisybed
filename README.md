# DaisyBed
trying to simplify development on the Electro-Smith Daisy Patch Submodule with libDaisy and DaisySP.

## repo structure

```
daisybed/
├── package.json               # convenience scripts (configure / build / flash), per firmware
├── lib/
│   ├── libDaisy/               # git submodule
│   └── DaisySP/               # git submodule
├── shared/                    # reusable helpers shared across firmware projects
│   ├── knob.{h,cpp}
│   ├── Voice.{h,cpp}
│   ├── DattorroPlate.h
│   └── cmake/
│       └── daisybed.cmake     # included by each project: sets up libDaisy + DaisySP
└── projects/                  # one self-contained CMake project per firmware
    ├── basic-monosynth/       #   CMakeLists.txt + src/ + build/ (per-project)
    ├── awful-paraphonic-synth/#   CMakeLists.txt + src/ + build/
    ├── cinematic-verb/        #   CMakeLists.txt + src/ + build/
    └── midi-test/             #   CMakeLists.txt + src/ + build/
```

There is intentionally **no top-level `CMakeLists.txt`**. Each firmware is a
standalone CMake project that pulls in libDaisy + DaisySP via
`shared/cmake/daisybed.cmake`. This keeps libDaisy's ARM toolchain autodetect
working (it runs in the same scope as the project's `project()` call) and lets
you configure/build/flash one firmware at a time without touching the others.

`shared/` is exposed as the `daisybed_shared` INTERFACE library (header-only
include path). Projects that need a `.cpp` from it (e.g. `knob.cpp`,
`Voice.cpp`) list it in their own `FIRMWARE_SOURCES`.

## getting started

### requirements

- CMake: Version 3.26 or higher.
- ARM Cross-compiler toolchain: For building the project.
- dfu-util: For flashing the firmware.

### building a firmware

1. clone the repository:
   ```sh
   git clone --recurse-submodules https://github.com/alexander-daniel/daisybed.git
   cd daisybed
   ```

2. configure one firmware (each gets its own build dir under the project):
   ```sh
   FW=basic-monosynth npm run configure
   ```

3. build it:
   ```sh
   FW=basic-monosynth npm run build
   ```

### flashing

```sh
FW=basic-monosynth npm run flash
```

`-a 0`: select the first alt setting
`-s 0x08000000:leave`: specify the address to flash the firmware to and leave the device in DFU mode after flashing
`-D projects/$FW/build/$FW.bin`: specify the path to the firmware binary
`-d ,0483:df11`: specify the USB VID:PID of the device

## License
This project is licensed under the MIT License.
