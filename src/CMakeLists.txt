set(BINARIES doom)

add_subdirectory(adpcm-xq)

foreach(SUBDIR ${BINARIES})
    add_subdirectory("${SUBDIR}")
endforeach()

# Common source files used by absolutely everything:
cmake_policy(SET CMP0076 NEW)

set(I_PLATFORM pico)

add_library(common INTERFACE)
target_sources(common INTERFACE
    i_main.c
    i_system.h
    m_argv.c             m_argv.h
    m_misc.c             m_misc.h)

target_compile_definitions(common INTERFACE
        #HACK_FINALE_E1M1=1
        #HACK_FINALE_SHAREWARE=1
#        DEBUG_MOBJ=1
)

add_subdirectory(pico)

target_link_libraries(common INTERFACE common_${I_PLATFORM})

# Source files used by the game BINARIES (chocolate-doom, etc.)

add_library(game INTERFACE)
target_sources(game INTERFACE
    aes_prng.c          aes_prng.h
    d_event.c           d_event.h
                        doomkeys.h
                        doomtype.h
    d_iwad.c            d_iwad.h
    d_loop.c            d_loop.h
    d_mode.c            d_mode.h
                        d_ticcmd.h
    deh_str.c           deh_str.h
    gusconf.c           gusconf.h
    i_endoom.h
    i_glob.h
    i_input.h
    i_joystick.h
    i_swap.h
    i_sound.h
    i_timer.h
    i_video.h
    midifile.c          midifile.h
    mus2mid.c           mus2mid.h
    m_bbox.c            m_bbox.h
    m_cheat.c           m_cheat.h
    m_config.c          m_config.h
    m_controls.c        m_controls.h
    m_fixed.c           m_fixed.h

    net_client.c        net_client.h
    sha1.c              sha1.h
    memio.c             memio.h
    tables.c            tables.h
    v_diskicon.c        v_diskicon.h
    v_video.c           v_video.h
                        v_patch.h
    w_checksum.c        w_checksum.h
    w_main.c            w_main.h
    w_wad.c             w_wad.h
    w_file.c            w_file.h
        w_file_posix.c
        w_file_memory.c

    w_merge.c           w_merge.h
    z_zone.c            z_zone.h

    i_oplmusic.c
    i_sound.c

)

add_library(game_pico INTERFACE)

target_link_libraries(game INTERFACE game_${I_PLATFORM})

target_link_libraries(game INTERFACE common opl)

add_library(small_doom_common INTERFACE)

target_link_libraries(small_doom_common INTERFACE adpcm-lib) # handy to have about

target_compile_definitions(small_doom_common INTERFACE
        SHRINK_MOBJ=1 # may break saved games

        DOOM_ONLY=1 # don't support hexxen etc.
        DOOM_SMALL=1
        DOOM_CONST=1 # thread const thru lots of places

        SOUND_LOW_PASS=1
        NUM_SOUND_CHANNELS=8 # sounds ok (actually that is how many are used by default)

        # functionality
        NO_USE_CHECKSUM=1
        NO_USE_RELOAD=1
        USE_SINGLE_IWAD=1
        NO_USE_WIPE=1 #memory if nothing else - we can fix this (you can do it)
        NO_USE_JOYSTICK=1
        NO_USE_DEH=1
        NO_USE_MUSIC_PACKS=1

        USE_FLAT_MAX_256=1
        USE_MEMMAP_ONLY=1

        USE_LIGHTMAP_INDEXES=1 # saves about 9K

        #FRACBITS=15 # ha ha doesn't seem to be passed thru everywhere!
        #NO_USE_ENDDOOM=1
        #NO_DRAW_VISPLANES=1

        USE_ERASE_FRAME=1 #
        NO_DRAW_MID=1
        NO_DRAW_TOP=1
        NO_DRAW_BOTTOM=1
        NO_DRAW_MASKED=1
        NO_DRAW_SKY=1
        #NO_DRAW_SPANS=1
        NO_DRAW_SPRITES=1
        NO_DRAW_PSPRITES=1

#        NO_VISPLANES=1
        NO_VISPLANE_GUTS=1
        NO_VISPLANE_CACHES=1 # todo these might be fine as they can be temporary
        NO_DRAWSEGS=1
        NO_VISSPRITES=1


        NO_MASKED_FLOOR_CLIP=1 # not needed with PD_ rendering and with floor clip it we have occasional visual glitches with sprites split across visplanes

        PD_DRAW_COLUMNS=1
        PD_DRAW_MARKERS=1
        PD_DRAW_PLANES=1

        PD_SCALE_SORT=1

        PD_CLIP_WALLS=1
        PD_QUANTIZE=1

        PD_SANITY=1

        #MU_STATS=1
        PD_COLUMNS=1
        PICO_DOOM=1
        NO_USE_DS_COLORMAP=1
        NO_USE_DC_COLORMAP=1

        #PRINT_COLORMAPS=1
        #PRINT_PALETTE=1
        USE_READONLY_MMAP=1

# -----------------------------------------------------------------
# MUSIC RELATED
# -----------------------------------------------------------------
        NO_USE_TIMIDITY=1
        NO_USE_GUS=1
        NO_USE_LIBSAMPLERATE=1

        # slightly slower but only uses 1K of sin tables vs 9K
        EMU8950_NO_WAVE_TABLE_MAP=1

        EMU8950_NO_TLL=1 # don't use lookup table for total level
        EMU8950_NO_FLOAT=1 # double check there is no float
        EMU8950_NO_TIMER=1 # disable timer which isn't used
        EMU8950_NO_TEST_FLAG=1 # disable test flags (which aren't used)
        EMU8950_SIMPLER_NOISE=1 # only generate noise bit when needed
        EMU8950_SHORT_NOISE_UPDATE_CHECK=1 # only update short noise if it is used
        # actually this doesn't make things faster
        #EMU8950_LINEAR_NEG_NOT_NOT=1 # negative values rounded towrds -infinity not 0 without this; does it matter?

        EMU8950_LINEAR_SKIP=1 # skip silent slots
        EMU8950_LINEAR_END_OF_NOTE_OPTIMIZATION # early out envelope when DECAY/SUSTAIN/RELEASE envelope reaches mute
        EMU8950_NO_PERCUSSION_MODE=1 # rhythm only mode (doom doesn't use percussion; whhhaaaah!?)
        EMU8950_LINEAR=1 # reorganize to do linear runs of channels

        # things we really don't care about but need for diff-ing with non linear version
        # BEWARE - need to turn this off for comparison with on device version which doesn't do nitpicks (though could i guess)

        EMU8950_ASM=1
#        EMU8950_NIT_PICKS=1
#        DUMPO=1

# -----------------------------------------------------------------
# FLASH SIZE
# -----------------------------------------------------------------
        NO_USE_STATE_MISC #doesn't appear to be used anyway - perhaps in doom only

# -----------------------------------------------------------------
# RAM SIZE
# -----------------------------------------------------------------
        USE_RAW_MAPNODE=1
        USE_RAW_MAPVERTEX=1
        USE_RAW_MAPSEG=1
        USE_RAW_MAPLINEDEF=1
        # USE_RAW_MAPSUBSECTOR=1 # obsolete... now WHD
        # USE_RAW_MAPSIDEDEF=1 obsolete
        USE_RAW_MAPTHING=1

        USE_INDEX_LINEBUFFER=1
        NO_USE_ZLIGHT=1
        NO_Z_ZONE_ID=1 # seems unused

        Z_MALOOC_EXTRA_DATA=1
        USE_THINKER_POOL=1
        NO_INTERCEPTS_OVERRUN=1
#        INCLUDE_SOUND_C_IN_S_SOUND=1 # avoid issues with non static const array
# -----------------------------------------------------------------
# IMMUTABLE
# -----------------------------------------------------------------

        TEMP_IMMUTABLE_DISABLED=1
        USE_CONST_SFX=1
        USE_CONST_MUSIC=1

        NO_DEMO_RECORDING=1
        PICO_NO_TIMING_DEMO=1
        NO_USE_EXIT=1 # not sure whether we have an exit... if so what does it need to do?

        PICO_DEBUG_PIN_BASE=18
        PICO_DEBUG_PIN_COUNT=2
)

target_compile_options(small_doom_common INTERFACE
        -Wall
        -Wno-unused-function
        -Wno-unused-but-set-variable
        -Wno-unused-variable
        )

if (TARGET chocolate-doom)
    target_compile_definitions(chocolate-doom PRIVATE
        USE_FLAT_MAX_256=1
    )
endif()

set(DOOM_PICO_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/pico)
# WAD data to bundle into "<target>_wad.uf2" next to the plain program UF2 (so a single drag & drop installs
# both). The super-tiny targets use WHX (doom1.whx, the shareware WAD, ships with this repo); the "nost" targets
# use WHD, e.g. made from doom.wad / doom2.wad with "whd_gen <wad> <whd> -no-super-tiny".
set(DOOM_BUNDLE_WHX "${CMAKE_CURRENT_LIST_DIR}/../doom1.whx" CACHE FILEPATH "WHX to bundle with the super-tiny builds (empty for none)")
set(DOOM_BUNDLE_WHD "" CACHE FILEPATH "WHD to bundle with the _nost builds (empty for none)")
if (PICO_ON_DEVICE)
    find_package(Python3 COMPONENTS Interpreter)
endif()

# The WHX/WHD file is flashed separately at a fixed address after the program; make sure they never overlap
function(set_doom_tiny_wad_addr TARGET ADDR WAD_FILE)
    target_compile_definitions(${TARGET} PRIVATE TINY_WAD_ADDR=${ADDR})
    if (PICO_ON_DEVICE)
        target_link_options(${TARGET} PRIVATE "LINKER:--defsym=DOOM_WAD_ADDR=${ADDR}" "${DOOM_PICO_SRC_DIR}/wad_addr_check.ld")
        if (WAD_FILE AND EXISTS "${WAD_FILE}" AND Python3_Interpreter_FOUND)
            add_custom_command(TARGET ${TARGET} POST_BUILD
                    COMMAND Python3::Interpreter "${DOOM_PICO_SRC_DIR}/../../tools/uf2_add_data.py"
                            "$<TARGET_FILE_DIR:${TARGET}>/${TARGET}.uf2" "${WAD_FILE}" ${ADDR}
                            "$<TARGET_FILE_DIR:${TARGET}>/${TARGET}_wad.uf2"
                    VERBATIM)
        endif()
    endif()
endfunction()

# core 1's stack lives in the 4K SCRATCH_X bank; the LCD scanline code isn't there, so core 1 (which also
# takes the LCD interrupts) gets most of it
set(DOOM_CORE1_STACK_SIZE 0xc00)

function(add_doom_tiny SUFFIX RENDER_LIB)
    add_executable(doom_tiny${SUFFIX})
    target_link_libraries(doom_tiny${SUFFIX} PRIVATE game)

    target_sources(doom_tiny${SUFFIX} PRIVATE
            tiny_huff.c
            musx_decoder.c
            image_decoder.c
            )
    target_include_directories(doom_tiny${SUFFIX} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/../")
    target_link_libraries(doom_tiny${SUFFIX} PRIVATE doom)

    target_link_libraries(doom_tiny${SUFFIX} PRIVATE small_doom_common)

    if (PICO_SDK)
        pico_set_float_implementation(doom_tiny${SUFFIX} none)
        pico_set_double_implementation(doom_tiny${SUFFIX} none)
        pico_add_extra_outputs(doom_tiny${SUFFIX})
    endif()

    target_compile_options(doom_tiny${SUFFIX} PRIVATE
            -Wno-format-truncation)
    target_compile_definitions(doom_tiny${SUFFIX} PRIVATE
        DOOM_TINY=1
        EMU8950_SLOT_RENDER=1

        SHRINK_MOBJ=1 # may break saved games
        NO_RDRAW=1

        #NO_DRAW_MENU=1
        # SDK minimilization
        # (the Bluetooth build needs the default alarm pool and shared IRQ handlers, for BTstack / cyw43)
        $<$<NOT:$<BOOL:${DOOM_TINY_BLUETOOTH}>>:PICO_TIME_DEFAULT_ALARM_POOL_DISABLED=1>
        $<$<NOT:$<BOOL:${DOOM_TINY_BLUETOOTH}>>:PICO_TIME_DEFAULT_ALARM_POOL_MAX_TIMERS=1>
        $<$<NOT:$<BOOL:${DOOM_TINY_BLUETOOTH}>>:PICO_DISABLE_SHARED_IRQ_HANDLERS=1>
        PICO_USE_OPTIMISTIC_SBRK=1

        # MUSIC
        USE_EMU8950_OPL=1
        USE_DIRECT_MIDI_LUMP=1

        NO_USE_NET=1 # standard networking
        USE_PICO_NET=1
        PICO_DEFAULT_I2C=1

        #NO_USE_LOAD=1
        #NO_USE_SAVE=1
        $<$<BOOL:${PICO_ON_DEVICE}>:NO_FILE_ACCESS=1>

        SAVE_COMPRESSED=1
        LOAD_COMPRESSED=1

        NO_USE_ARGS=1
        NO_USE_SAVE_CONFIG=1
        NO_USE_FLOAT=1

        USE_VANILLA_KEYBOARD_MAPPING_ONLY=1

        #FORCE_NODRAW=1

        NO_USE_LOADING_DISK=1

        # -- these aren't used any more, we adapt based on DOOM_TINY
        #NO_USE_ST=1 # for now skip all the status crap
        #NO_USE_DEFAULT_ST_LIB=1

        #NO_PAGE_DRAWER=1 # splash screens
        #NO_HU_DRAWER=1

        USE_WHD=1


        NO_Z_MALLOC_USER_PTR=1 # onyl needed for freeing textures i think

        FIXED_SCREENWIDTH=1
        FLOOR_CEILING_CLIP_8BIT=1

#        NO_USE_WI=1
        USE_MUSX=1
        MUSX_COMPRESSED=1

        NO_SCREENSHOT=1

        NO_USE_BOUND_CONFIG=1

        USE_FPS=1

#        PICO_DOOM_INFO=1
    )
    target_compile_definitions(doom_tiny${SUFFIX} PRIVATE
        #FORCE_NODRAW=1
        USE_MEMORY_WAD=1
        PICO_DEBUG_MALLOC=1
        EMU8950_NO_RATECONV=1

        PICO_CORE1_STACK_SIZE=${DOOM_CORE1_STACK_SIZE}

        NO_IERROR=1
    )
    if (PICO_ON_DEVICE)
        target_compile_options(doom_tiny${SUFFIX} PRIVATE -fno-common -fdata-sections -Wl,--sort-section=alignment)
        target_compile_definitions(doom_tiny${SUFFIX} PRIVATE
                NO_ZONE_DEBUG=1
                )
        #target_link_libraries(doom_tiny${SUFFIX} PRIVATE hardware_flash)
    endif()
    if (FORCE_DEBUG)
        target_compile_options(doom_tiny${SUFFIX} PRIVATE -g)
    endif()
    target_link_libraries(doom_tiny${SUFFIX} PRIVATE ${RENDER_LIB})
    if (PICO_SDK)
        # Pimoroni Pico Display Pack: LCD on GP16-20, buttons on GP12-15, so I2C networking moves to GP2/GP3
        target_compile_definitions(doom_tiny${SUFFIX} PRIVATE
                PICO_DEFAULT_I2C_SDA_PIN=2
                PICO_DEFAULT_I2C_SCL_PIN=3
                LCD_PIN_DC=16
                LCD_PIN_CS=17
                LCD_PIN_SCK=18
                LCD_PIN_MOSI=19
                LCD_PIN_BL=20
                BUTTON_PIN_A=12
                BUTTON_PIN_B=13
                BUTTON_PIN_X=14
                BUTTON_PIN_Y=15
                LCD_SPI_HZ=${DOOM_LCD_SPI_HZ}
                LCD_ASPECT_CORRECT=$<BOOL:${DOOM_LCD_ASPECT_CORRECT}>
                LCD_ROTATE_180=$<BOOL:${DOOM_LCD_ROTATE_180}>
        )
        if (DOOM_DISPLAY STREQUAL "PIMORONI_DISPLAY_28")
            # the 2.8" pack's RGB LED sits on GP26-28 where I2S audio normally lives, so move audio to GP9-11
            target_compile_definitions(doom_tiny${SUFFIX} PRIVATE
                    LCD_PIN_LED_R=26
                    LCD_PIN_LED_G=27
                    LCD_PIN_LED_B=28
                    PICO_AUDIO_I2S_DATA_PIN=9
                    PICO_AUDIO_I2S_CLOCK_PIN_BASE=10
            )
        else()
            target_compile_definitions(doom_tiny${SUFFIX} PRIVATE
                    LCD_PIN_LED_R=6
                    LCD_PIN_LED_G=7
                    LCD_PIN_LED_B=8
                    # same I2S pins as the original rp2040-doom VGA board builds
                    PICO_AUDIO_I2S_DATA_PIN=26
                    PICO_AUDIO_I2S_CLOCK_PIN_BASE=27
            )
        endif()
    endif()
    set(PICO_HACK 0)
    set(STAMP_HACK 0)
    if (PICO_HACK)
        target_compile_definitions(doom_tiny${SUFFIX} PRIVATE
                PICO_AUDIO_I2S_DATA_PIN=20
                PICO_AUDIO_I2S_CLOCK_PIN_BASE=21
                NO_USE_UART=1
                )
        pico_enable_stdio_uart(doom_tiny${SUFFIX} 0)
    elseif(STAMP_HACK)
        target_compile_definitions(doom_tiny${SUFFIX} PRIVATE
                PICO_AUDIO_I2S_DATA_PIN=22
                PICO_AUDIO_I2S_CLOCK_PIN_BASE=23
                NO_USE_UART=1
                INCREASE_I2S_DRIVE_STRENGTH=1
        )
        pico_enable_stdio_uart(doom_tiny${SUFFIX} 0)
    endif()
endfunction()

add_subdirectory(whd_gen)

add_library(render_newhope INTERFACE)
target_sources(render_newhope INTERFACE
        pd_render.cpp
        )
target_compile_definitions(render_newhope INTERFACE
        PICODOOM_RENDER_NEWHOPE=1
        MERGE_DISTSCALE0_INTO_VIEWCOSSINANGLE=1
        NO_USE_ZLIGHT=1
        PICO_SCANVIDEO_SCANLINE_BUFFER_COUNT=4
        PICO_SCANVIDEO_LINKED_SCANLINE_BUFFERS=1
        PICO_SCANVIDEO_SCANLINE_RELEASE_FUNCTION=1
        )
if (PICO_ON_DEVICE)
    target_compile_definitions(render_newhope INTERFACE
            PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=164
    )
else()
    # we cannot rewrite the buffers in host mode so we need them to be big enough for text mode
    target_compile_definitions(render_newhope INTERFACE
            PICO_SCANVIDEO_MAX_SCANLINE_BUFFER_WORDS=324
    )
endif()

if (PICO_SDK)
    add_doom_tiny("" render_newhope)
    add_library(tiny_settings INTERFACE)
    target_compile_definitions(tiny_settings INTERFACE
            WHD_SUPER_TINY=1
            DEMO1_ONLY=1
            NO_USE_FINALE_CAST=1
            NO_USE_FINALE_BUNNY=1
    )
    set_doom_tiny_wad_addr(doom_tiny 0x10040000 "${DOOM_BUNDLE_WHX}")
    target_link_libraries(doom_tiny PRIVATE tiny_settings)
    add_doom_tiny(_nost render_newhope)

    # target_compile_definitions(doom_tiny_nost PRIVATE NO_USE_FINALE_CAST=1) # only used by doom II but doesnt take that much more space
    set_doom_tiny_wad_addr(doom_tiny_nost 0x10048000 "${DOOM_BUNDLE_WHD}")
    if (PICO_ON_DEVICE)
        suppress_tinyusb_warnings()
        add_doom_tiny(_usb render_newhope)
        target_link_libraries(doom_tiny_usb PRIVATE tiny_settings pico_cd)
        target_compile_definitions(doom_tiny_usb PRIVATE
                USB_SUPPORT=1
        )
        set_doom_tiny_wad_addr(doom_tiny_usb 0x10043000 "${DOOM_BUNDLE_WHX}") # TinyUSB adds >8K (was 0x10042000, which the current SDK overflows)
        add_doom_tiny(_nost_usb render_newhope)
        target_link_libraries(doom_tiny_nost_usb PRIVATE pico_cd)
        target_compile_definitions(doom_tiny_nost_usb PRIVATE
                USB_SUPPORT=1
                )
        set_doom_tiny_wad_addr(doom_tiny_nost_usb 0x10048000 "${DOOM_BUNDLE_WHD}")
    endif()

    if (DOOM_BLUETOOTH)
        # doom_tiny_bt: doom_tiny (DOOM1.WAD as WHX) + Bluetooth gamepads via Bluepad32, for the Pico 2 W
        if (NOT BLUEPAD32_ROOT)
            include(FetchContent)
            FetchContent_Declare(bluepad32
                    GIT_REPOSITORY https://github.com/ricardoquesada/bluepad32.git
                    GIT_TAG 80d6314e46f871719fbb4b721a2cc02227c4f369 # 5.0.0-beta0, tested
                    GIT_SUBMODULES "" # we use the BTstack bundled with the Pico SDK
            )
            FetchContent_GetProperties(bluepad32)
            if (NOT bluepad32_POPULATED)
                message("Downloading Bluepad32")
                FetchContent_Populate(bluepad32)
            endif()
            set(BLUEPAD32_ROOT ${bluepad32_SOURCE_DIR})
        endif()
        set(BLUEPAD32_ROOT ${BLUEPAD32_ROOT} CACHE PATH "Path to Bluepad32" FORCE)
        # btstack_config.h / sdkconfig.h, needed by BTstack and libbluepad32 as well as us
        include_directories(${CMAKE_CURRENT_LIST_DIR}/pico/bt ${PICO_SDK_PATH}/lib/btstack/src)
        # the sys clock is overclocked to 270MHz: slow the cyw43 SPI down to ~34MHz
        add_compile_definitions(CYW43_PIO_CLOCK_DIV_INT=4 CYW43_PIO_CLOCK_DIV_FRAC8=0)
        add_subdirectory(${BLUEPAD32_ROOT}/src/components/bluepad32 libbluepad32)

        set(DOOM_TINY_BLUETOOTH 1)
        add_doom_tiny(_bt render_newhope)
        set(DOOM_TINY_BLUETOOTH 0)
        target_sources(doom_tiny_bt PRIVATE pico/i_bluetooth.c)
        target_include_directories(doom_tiny_bt PRIVATE ${BLUEPAD32_ROOT}/src/components/bluepad32/include)
        target_link_libraries(doom_tiny_bt PRIVATE tiny_settings bluepad32 pico_cyw43_arch_none pico_btstack_classic pico_btstack_ble pico_btstack_cyw43)
        target_compile_definitions(doom_tiny_bt PRIVATE
                PICO_BLUETOOTH_INPUT=1
                # BTstack's pairing keys: the 8K below the last 4K sector of flash (RP2350 default) - keep saves clear
                DOOM_FLASH_RESERVED_TOP=12288
        )
        # Bluetooth + the cyw43 firmware are ~500K, so the WHX moves up (Pico 2 W has 4MB)
        set_doom_tiny_wad_addr(doom_tiny_bt 0x10100000 "${DOOM_BUNDLE_WHX}")
    endif()
endif()


