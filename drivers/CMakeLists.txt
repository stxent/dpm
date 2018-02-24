# Copyright (C) 2018 xent
# Project is distributed under the terms of the GNU General Public License v3.0

list(APPEND SOURCE_FILES "ds18b20.c")
list(APPEND SOURCE_FILES "rgb_led.c")
list(APPEND SOURCE_FILES "software_pwm.c")

file(GLOB SOURCE_FILES_DISPLAYS "displays/*.c")
file(GLOB SOURCE_FILES_USB "usb/*.c")

include("platform/CMakeLists.txt")

add_library(drivers OBJECT
        ${SOURCE_FILES}
        ${SOURCE_FILES_DISPLAYS}
        ${SOURCE_FILES_USB}
)
