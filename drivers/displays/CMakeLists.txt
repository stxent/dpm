# Copyright (C) 2022 xent
# Project is distributed under the terms of the MIT License

list(APPEND SOURCE_FILES "hd44780.c")
list(APPEND SOURCE_FILES "ili9325.c")
list(APPEND SOURCE_FILES "s6d1121.c")
list(APPEND SOURCE_FILES "st7735.c")

add_library(dpm_displays OBJECT ${SOURCE_FILES})
