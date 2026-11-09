# pico-outrun for the Waveshare ESP32-S3-Touch-LCD-1.83 (ESP-IDF v6), as a game of the EspRacer console.
# The Cannonball engine and the port/ data code are the Pico build's own (referenced in place);
# only esp32/main/ is new. It runs from a game slot of the launcher's partition table
# (esp32/partitions.csv is EspRacer's, linked), using EspRacer's racer_board component.
#   . ~/esp/v6.0/esp-idf/export.sh; idf.py -C esp32 -B esp32/build-racer build
#   (EspRacer's tools/flash_all.sh flashes it into its slot)
# EspRacer is found at ../../EspRacer, or where ESPRACER_DIR says.
cmake_minimum_required(VERSION 3.22)
if(NOT DEFINED ENV{ESPRACER_DIR})
  get_filename_component(ESPRACER_DIR ${CMAKE_CURRENT_LIST_DIR}/../../EspRacer ABSOLUTE)
else()
  set(ESPRACER_DIR $ENV{ESPRACER_DIR})
endif()
set(SDKCONFIG_DEFAULTS "${ESPRACER_DIR}/sdkconfig.common;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
set(EXTRA_COMPONENT_DIRS ${ESPRACER_DIR}/components)
set(SDKCONFIG ${CMAKE_BINARY_DIR}/sdkconfig)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(outrun)
