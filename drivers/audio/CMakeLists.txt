# Copyright (C) 2023 xent
# Project is distributed under the terms of the MIT License

list(APPEND SOURCE_FILES "tlv320aic3x.c")

add_library(dpm_audio OBJECT ${SOURCE_FILES})
