# Copyright (C) 2022 xent
# Project is distributed under the terms of the MIT License

list(APPEND SOURCE_FILES "ds18b20.c")
list(APPEND SOURCE_FILES "hmc5883.c")
list(APPEND SOURCE_FILES "mpu60xx.c")
list(APPEND SOURCE_FILES "mpu60xx_proxy.c")
list(APPEND SOURCE_FILES "ms56xx.c")
list(APPEND SOURCE_FILES "ms56xx_thermometer.c")
list(APPEND SOURCE_FILES "sensor_handler.c")
list(APPEND SOURCE_FILES "sht2x.c")
list(APPEND SOURCE_FILES "sht2x_thermometer.c")
list(APPEND SOURCE_FILES "xpt2046.c")

add_library(dpm_sensors OBJECT ${SOURCE_FILES})
