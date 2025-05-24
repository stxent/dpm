# Copyright (C) 2022 xent
# Project is distributed under the terms of the MIT License

list(APPEND SOURCE_FILES "m24.c")
list(APPEND SOURCE_FILES "mx35.c")
list(APPEND SOURCE_FILES "mx35_serial.c")
list(APPEND SOURCE_FILES "mx35_quad.c")
list(APPEND SOURCE_FILES "nand_defs.c")
list(APPEND SOURCE_FILES "nor_defs.c")
list(APPEND SOURCE_FILES "w25q_quad.c")
list(APPEND SOURCE_FILES "w25q_serial.c")

add_library(dpm_memory OBJECT ${SOURCE_FILES})
