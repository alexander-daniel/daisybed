# DaisyBed
trying to simplify development on the Electro-Smith Daisy Patch Submodule with libDaisy and DaisySP.

## repo structure

- CMakeLists.txt: configuration file for CMake -- used to build the project.
- package.json: I'm using this just to run scripts -- you can call them directly if you prefer.
- src/: directory with example projects demonstrating basic and more usage.
  - main.cpp: A basic monosynth example.
  - awful-paraphonic.cpp: An example of an awful paraphonic synthesizer with round-robin voice allocation.

## getting started

### requirements

- CMake: Version 3.26 or higher.
- ARM Cross-compiler toolchain: For building the project.
- dfu-util: For flashing the firmware.

### building the project

1. clone the repository:
   ```sh
   git clone --recurse submodules https://github.com/alexander-daniel/daisybed.git
   cd daisybed
   ```

2. configure the project:
   ```sh
   npm run configure
   ```

3. build the project:
   ```sh
   npm run build
   ```

### post build

After building the project, the firmware binaries will be generated in the `build` directory. You can then flash this firmware onto your Daisy Patch Submodule.

### flashing the firmware

To flash the firmware to your Daisy Patch Submodule, run:
```sh
dfu-util -a 0 -s 0x08000000:leave -D build/YOUR_FIRMWARE_BINARY_NAME.bin -d ,0483:df11
```

replace `YOUR_FIRMWARE_BINARY_NAME` with the name of the firmware binary generated in the `build` directory.
`-a 0`: select the first alt setting
`-s 0x08000000:leave`: specify the address to flash the firmware to and leave the device in DFU mode after flashing
`-D build/YOUR_FIRMWARE_BINARY_NAME.bin`: specify the path to the firmware binary
`-d ,0483:df11`: specify the USB VID:PID of the device

## License
This project is licensed under the MIT License.
