# Copyright (C) 2018 xent
# Project is distributed under the terms of the GNU General Public License v3.0

add_subdirectory("platform")

file(GLOB SOURCE_FILES
        "*.c"
        "displays/*.c"
        "usb/*.c"
)

add_library(dpm_generic OBJECT ${SOURCE_FILES})
