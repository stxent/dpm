# Copyright (C) 2022 xent
# Project is distributed under the terms of the MIT License

list(APPEND SOURCE_FILES "m24.c")
list(APPEND SOURCE_FILES "w25_spim.c")

add_library(dpm_memory OBJECT ${SOURCE_FILES})
