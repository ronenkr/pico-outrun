# The Cannonball engine and port/ code as the Pico build compiles them, plus this directory's ESP32 platform layer
# (what port/glue.cpp, render.cpp, audio.cpp, alloc.cpp, outrun_sdload.cpp and main.cpp do for the Pico).
set(ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
set(PORT ${ROOT}/port)
set(CB   ${ROOT}/cannonball/src/main)

file(GLOB ENGINE
     ${CB}/engine/*.cpp
     ${CB}/engine/audio/*.cpp
     ${CB}/hwvideo/*.cpp
     ${CB}/hwaudio/*.cpp)
list(APPEND ENGINE ${CB}/video.cpp ${CB}/roms.cpp ${CB}/trackloader.cpp ${CB}/utils.cpp)

# shared with the Pico build and the host tests, unchanged
set(SHARED ${PORT}/config.cpp ${PORT}/romloader.cpp ${PORT}/input.cpp ${PORT}/outrun_data.c ${PORT}/outrun_pack.c)

idf_component_register(
    SRCS main.cpp esp_glue.cpp esp_render.cpp esp_screen.cpp esp_audio.cpp esp_console.cpp esp_alloc.cpp esp_input.cpp esp_data.cpp
         ${SHARED} ${ENGINE}
    # this directory first: its sdl2/ headers shadow port/sdl2/ where the ESP32 needs its own;
    # then port/ (which shadows the dropped upstream sdl2/, frontend/ and directx/), then the engine
    INCLUDE_DIRS . ${PORT} ${CB}
    PRIV_REQUIRES racer_board esp_timer esp_psram esp_hw_support esp_driver_gpio vfs fatfs esp_driver_usb_serial_jtag mbedtls
    LDFRAGMENTS linker.lf)

# 2010-era C++: warnings are the engine's to mend, not errors here; the engine's arithmetic as the PC's
target_compile_options(${COMPONENT_LIB} PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>
    -fwrapv -fsigned-char
    -Wno-error -Wno-unused-variable -Wno-unused-but-set-variable -Wno-sign-compare
    -Wno-format-truncation -Wno-unused-parameter)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    OUTRUN_GFX_IN_FLASH=1        # tiles, sprites and road come pre-decoded from the data image
    OUTRUN_ESP32=1
    OUTRUN_DATA_ADDR=0           # there is no flash data image here: the image is on the card
    OUTRUN_DATA_MAX_SIZE=0)
