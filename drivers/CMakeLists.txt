# Copyright (C) 2022 xent
# Project is distributed under the terms of the MIT License

add_subdirectory("displays")
add_subdirectory("gnss")
add_subdirectory("memory")
add_subdirectory("platform")
add_subdirectory("sensors")
add_subdirectory("usb")

list(APPEND SOURCE_FILES "button.c")
list(APPEND SOURCE_FILES "rgb_led.c")
list(APPEND SOURCE_FILES "software_pwm.c")
        "memory/*.c"

add_library(dpm_generic OBJECT ${SOURCE_FILES})
