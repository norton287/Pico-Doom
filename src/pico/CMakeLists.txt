add_library(common_pico INTERFACE)
target_sources(common_pico INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/i_glob.c
        ${CMAKE_CURRENT_LIST_DIR}/i_input.c
        ${CMAKE_CURRENT_LIST_DIR}/i_system.c
        ${CMAKE_CURRENT_LIST_DIR}/piconet.c
        ${CMAKE_CURRENT_LIST_DIR}/i_timer.c
        ${CMAKE_CURRENT_LIST_DIR}/i_video.c
        ${CMAKE_CURRENT_LIST_DIR}/stubs.c

        ${CMAKE_CURRENT_LIST_DIR}/i_picosound.c
)
if (PICO_ON_DEVICE)
    target_sources(common_pico INTERFACE
            ${CMAKE_CURRENT_LIST_DIR}/blit.S
            ${CMAKE_CURRENT_LIST_DIR}/picoflash.c
            )
    pico_wrap_function(common_pico malloc)
    pico_wrap_function(common_pico calloc)
    pico_wrap_function(common_pico free)
    target_compile_definitions(common_pico INTERFACE
            PICO_HEAP_SIZE=0
            USE_ZONE_FOR_MALLOC=1
    )
    target_link_libraries(common_pico INTERFACE  hardware_i2c)
endif()
target_include_directories(common_pico INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/..
        ${CMAKE_CURRENT_LIST_DIR}/../../textscreen
        )

target_compile_definitions(common_pico INTERFACE
        NO_USE_MOUSE=1
        PICO_AUDIO_I2S_PIO=1
        PICO_AUDIO_I2S_DMA_IRQ=1
        PICO_USE_STACK_GUARDS=0 # todo we can actually use these sensibly, but right now we do overflow!
)
if (PICO_RP2040)
    # hardware spin locks are fine (and faster) on RP2040; on RP2350 erratum E2 means the SDK (and pico-extras)
    # require the default software spin locks
    target_compile_definitions(common_pico INTERFACE PICO_USE_SW_SPIN_LOCKS=0)
endif()

target_link_libraries(common_pico INTERFACE pico_stdlib pico_multicore)

# ST7789 SPI LCD + A/B/X/Y buttons (Pimoroni Pico Display Pack 2.0 / 2.8)
if (NOT PICO_ON_DEVICE)
    message(FATAL_ERROR "DOOM_DISPLAY=${DOOM_DISPLAY} is only supported for on-device builds")
endif()
target_sources(common_pico INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/st7789_lcd.c
        ${CMAKE_CURRENT_LIST_DIR}/i_buttons.c
)
pico_generate_pio_header(common_pico ${CMAKE_CURRENT_LIST_DIR}/st7789_lcd.pio)
target_compile_definitions(common_pico INTERFACE
        PICO_DOOM_LCD=1
        PICO_BUTTON_INPUT=1
)
option(DOOM_BUTTON_DEBUG "Draw raw button / chord state in the top right corner of the LCD" OFF)
if (DOOM_BUTTON_DEBUG)
    target_compile_definitions(common_pico INTERFACE DOOM_BUTTON_DEBUG=1)
endif()
option(DOOM_BT_DEBUG "Draw Bluetooth bring-up / controller state in the top left corner of the LCD (doom_tiny_bt)" OFF)
if (DOOM_BT_DEBUG)
    target_compile_definitions(common_pico INTERFACE DOOM_BT_DEBUG=1)
endif()
target_link_libraries(common_pico INTERFACE hardware_pio hardware_dma)

add_library(pico_cd INTERFACE)
if (TARGET tinyusb_host)
    target_link_libraries(pico_cd INTERFACE tinyusb_host)
endif()