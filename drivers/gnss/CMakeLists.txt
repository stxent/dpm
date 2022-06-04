# Copyright (C) 2022 xent
# Project is distributed under the terms of the MIT License

list(APPEND SOURCE_FILES "ublox.c")
list(APPEND SOURCE_FILES "ublox_parser.c")

add_library(dpm_gnss OBJECT ${SOURCE_FILES})
