# Copyright (C) 2022 xent
# Project is distributed under the terms of the MIT License

list(APPEND SOURCE_FILES "dfu_bridge.c")
list(APPEND SOURCE_FILES "mouse.c")

add_library(dpm_usb OBJECT ${SOURCE_FILES})
