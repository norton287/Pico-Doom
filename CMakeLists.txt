set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

cmake_minimum_required(VERSION 3.13)

# A Pico SDK location (or a request to fetch one) means we're doing a Pico (RP2040/RP2350) build;
# the only other build is the native whd_gen tool (-DWHD_GEN_ONLY=ON). The environment variables are honoured too.
if (NOT WHD_GEN_ONLY AND NOT PICO_SDK_PATH AND DEFINED ENV{PICO_SDK_PATH})
    set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
endif()
if (NOT WHD_GEN_ONLY AND (PICO_SDK_PATH OR PICO_SDK_FETCH_FROM_GIT OR "$ENV{PICO_SDK_FETCH_FROM_GIT}"))
    include(pico_sdk_import.cmake)
    include(pico_extras_import.cmake)
endif()

project("Chocolate Doom" VERSION 3.0.0 LANGUAGES C CXX)

# -DWHD_GEN_ONLY=ON builds just the native whd_gen WAD converter (no Pico SDK)
option(WHD_GEN_ONLY "Only build the native whd_gen WAD -> WHD/WHX converter" OFF)
if (WHD_GEN_ONLY)
    if (PICO_SDK_PATH)
        message(FATAL_ERROR "WHD_GEN_ONLY is a native (host) build; don't pass PICO_SDK_PATH")
    endif()
    set(CMAKE_CXX_STANDARD 14)
    set(CMAKE_C_STANDARD 11)
    if (NOT CMAKE_BUILD_TYPE)
        # a debug whd_gen deliberately lowers sound encoding quality
        set(CMAKE_BUILD_TYPE Release)
    endif()
    add_subdirectory(src/adpcm-xq)
    add_subdirectory(src/whd_gen)
    return()
endif()
if (NOT PICO_SDK_PATH)
    message(FATAL_ERROR "This fork only builds for the Pico (use ./build.sh, or set PICO_SDK_PATH) "
            "or the native whd_gen tool (-DWHD_GEN_ONLY=ON). The original PC (SDL) build has been removed.")
endif()
enable_language(CXX)
set(CMAKE_CXX_STANDARD 14)

if (PICO_SDK_PATH)
    # Video output / input hardware for the RP2xxx builds:
    #   PIMORONI_DISPLAY_2  - Pimoroni Pico Display Pack 2.0 (320x240 ST7789 SPI LCD + A/B/X/Y buttons)
    #   PIMORONI_DISPLAY_28 - Pimoroni Pico Display Pack 2.8 (same LCD/buttons, RGB LED on GP26-28)
    #   VGA                 - the original 16 bit VGA output (use with PICO_BOARD=vgaboard)
    if (PICO_BOARD STREQUAL "vgaboard")
        set(DOOM_DISPLAY_DEFAULT VGA)
    else()
        set(DOOM_DISPLAY_DEFAULT PIMORONI_DISPLAY_2)
    endif()
    set(DOOM_DISPLAY ${DOOM_DISPLAY_DEFAULT} CACHE STRING "Display hardware: PIMORONI_DISPLAY_2, PIMORONI_DISPLAY_28 or VGA")
    set_property(CACHE DOOM_DISPLAY PROPERTY STRINGS PIMORONI_DISPLAY_2 PIMORONI_DISPLAY_28 VGA)
    if (DOOM_DISPLAY STREQUAL "VGA")
        set(DOOM_DISPLAY_IS_LCD 0)
    elseif (DOOM_DISPLAY STREQUAL "PIMORONI_DISPLAY_2" OR DOOM_DISPLAY STREQUAL "PIMORONI_DISPLAY_28")
        set(DOOM_DISPLAY_IS_LCD 1)
    else()
        message(FATAL_ERROR "Unknown DOOM_DISPLAY '${DOOM_DISPLAY}'")
    endif()
    # 1: stretch 320x200 to the 4:3 shape Doom was drawn for (fills 320x240); 0: 1:1 pixels with black bars
    option(DOOM_LCD_ASPECT_CORRECT "Stretch 320x200 to 320x240 on the LCD" ON)
    # max ST7789 SPI clock; 62.5MHz is the datasheet limit (-> 45MHz at 270MHz clk_sys); 67500000 -> 67.5MHz for more FPS
    set(DOOM_LCD_SPI_HZ 62500000 CACHE STRING "ST7789 SPI clock in Hz")
    option(DOOM_LCD_ROTATE_180 "Rotate the LCD picture by 180 degrees" OFF)
    # Bluetooth gamepads (Bluepad32): adds the doom_tiny_bt target; needs a Pico 2 W (default on for PICO_BOARD=pico2_w)
    if (PICO_BOARD STREQUAL "pico2_w")
        set(DOOM_BLUETOOTH_DEFAULT ON)
    else()
        set(DOOM_BLUETOOTH_DEFAULT OFF)
    endif()
    option(DOOM_BLUETOOTH "Build doom_tiny_bt with Bluetooth gamepad support (Pico 2 W)" ${DOOM_BLUETOOTH_DEFAULT})

    # this only affects device builds device, but we want to use zone for malloc in this case so we don't have two separate elastic spaces and can fit more in
    set(SKIP_PICO_MALLOC 1)
    pico_sdk_init()
    if (PICO_ON_DEVICE AND NOT CMAKE_BUILD_TYPE STREQUAL "MinSizeRel")
        message(WARNING "You should do a MinSizeRel build when targeting the RP2040
        (with -DCMAKE_BUILD_TYPE=MinSizeRel)")
    endif()
endif()

set(CMAKE_C_STANDARD 11)

# Autotools variables
set(top_srcdir ${CMAKE_CURRENT_SOURCE_DIR})
set(top_builddir ${CMAKE_CURRENT_BINARY_DIR})

# AC_INIT variables
set(PACKAGE_NAME "${PROJECT_NAME}")
set(PACKAGE_TARNAME "chocolate-doom")
set(PACKAGE_VERSION "${PROJECT_VERSION}")
set(PACKAGE_STRING "${PROJECT_NAME} ${PROJECT_VERSION}")
set(PACKAGE_BUGREPORT "chocolate-doom-dev-list@chocolate-doom.org")

string(REGEX REPLACE " Doom$" "" PACKAGE_SHORTNAME "${PACKAGE_NAME}")
set(PACKAGE_COPYRIGHT "Copyright (C) 1993-2017")
set(PACKAGE_LICENSE "GNU General Public License, version 2")

# Any settings that should apply to all targets in this directory and all
# subdirectories should go here.  Use judiciously.
#add_compile_options("-Wall" "-Wdeclaration-after-statement" "-Wredundant-decls")
add_compile_options("-Wall" "-Wredundant-decls")

set(HAVE_MMAP 1)
find_package(m)

include(CheckSymbolExists)
include(CheckIncludeFile)
check_symbol_exists(strcasecmp "strings.h" HAVE_DECL_STRCASECMP)
check_symbol_exists(strncasecmp "strings.h" HAVE_DECL_STRNCASECMP)
check_include_file("dirent.h" HAVE_DIRENT_H)

# Without a hyphen. This is used for the bash-completion scripts.
string(TOLOWER "${PACKAGE_SHORTNAME}" PROGRAM_SPREFIX)

# With a hyphen, used almost everywhere else.
set(PROGRAM_PREFIX "${PROGRAM_SPREFIX}-")

configure_file(cmake/config.h.cin config.h)


foreach(SUBDIR opl src)
    add_subdirectory("${SUBDIR}")
endforeach()
