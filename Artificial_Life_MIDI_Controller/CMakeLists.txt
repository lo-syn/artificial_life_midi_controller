# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.12)
include(pico_sdk_import.cmake)
project(Pico-ResTouch-X_X_code) 
pico_sdk_init()

# Add compilation subdirectory
add_subdirectory(lib/config)
add_subdirectory(lib/lcd)
add_subdirectory(lib/font)
add_subdirectory(examples)
add_subdirectory(lib/fatfs)
add_subdirectory(lib/sdcard)

# Add header file directory
include_directories(examples)
include_directories(./lib/config)
include_directories(./lib/lcd)
include_directories(./lib/font)
include_directories(./lib/fatfs)
include_directories(./lib/sdcard)

# Generate executable file
add_executable(main 
main.c
usb_descriptors.c
)
set_target_properties(main PROPERTIES
    OUTPUT_NAME "artificial_life_midi_controller"
)

# enable usb output, disable uart output
pico_enable_stdio_usb(main 1)
pico_enable_stdio_uart(main 0)
target_compile_definitions(main PRIVATE
    CFG_TUD_MIDI=1
    PICO_STDIO_USB_USE_DEFAULT_DESCRIPTORS=0
)

# create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(main)

target_link_libraries(main examples lcd font config pico_stdlib hardware_spi fatfs)
