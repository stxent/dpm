# DPM

![C21](https://img.shields.io/badge/C-21-blue)
![CMake](https://img.shields.io/badge/CMake-3.21+-blue)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

DPM is a library of peripheral devices and additional drivers for
microcontrollers. It is written using the C21 standard and built with CMake.

## Supported Platforms

The list of supported platforms is similar to the HALM library.
Extended support is available for the following platforms:

* **STMicro**:
  * STM32F1 series
  * STM32F4 series
* **NXP**:
  * LPC11xx
  * LPC11Exx
  * LPC13xx
  * LPC13Uxx
  * LPC17xx
  * LPC43xx

## Required Packages

To build and use the DPM library, you need the following packages:

* **GCC 13 or newer** — the GNU Compiler Collection, required for
  building the x86 version.
* **ARM GCC 13 or newer** — the GNU Compiler Collection, required for
  embedded targets.
* **CMake 3.21 or newer** — used for configuring and generating build
  systems across platforms.
* **Xcore** and **HALM** — can be built separately or included as
  CMake submodules in the project.

## Library Contents

The DPM library includes the following drivers and components:

* NAND and NOR serial memory drivers — for interfacing with
  non‑volatile memory chips using SPI, QSPI and platform-specific
  interfaces with XIP
* EEPROM serial memory drivers — for interfacing with
  non‑volatile memory chips using I2C
* Hardware-accelerated 1‑Wire interface drivers
* IrDA interface drivers
* Audio codec drivers
* Button drivers
* Display drivers with 8080 and SPI interfaces
* GNSS receiver drivers
* NTC thermistor driver and table generator
* Radio IC drivers
* RGB LED strip drivers
* Sensor drivers

## Usage Examples

1. Including in a CMake Project

To include the DPM library in your CMake project, add the following line
to your `CMakeLists.txt`:

```cmake
add_subdirectory("/path/to/dpm" dpm)
```

After that, you can link the library to your target using target_link_libraries:

```cmake
target_link_libraries(your_target dpm)
```

2. Building for x86 Outside the Project Tree

If you want to build the library as a standalone package, follow these steps:

```sh
cmake .. -DCMAKE_INSTALL_PREFIX=/path/to/output/dir/ \
  -DCMAKE_PREFIX_PATH=/path/to/output/dir/
make
make install
```

3. Building for LPC175x Outside the Project Tree

For building the library for the LPC175x platform, use the following command:

```sh
cmake .. -DCMAKE_INSTALL_PREFIX=/path/to/output/dir/ \
  -DCMAKE_PREFIX_PATH=/path/to/output/dir/ \
  -DCMAKE_TOOLCHAIN_FILE=/path/to/xcore/toolchains/cortex-m3.cmake \
  -DPLATFORM=LPC17XX
make
make install
```

## Build Options

The library supports the following CMake build options for customization:

* **PLATFORM** — specifies the target platform. Possible values are similar
  to those in the HALM library. If the PLATFORM option is omitted,
  x86 target is used.
