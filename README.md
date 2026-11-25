# RP2040 (+RP2350) Doom

This is a port of Doom for RP2040 / RP2350 devices, derived from [Chocolate Doom](https://github.com/chocolate-doom/chocolate-doom).

Significant changes have been made to support running on the RP2xxx device, but particularly to support running the 
entire shareware `DOOM1.WAD` which is 4M big on a Raspberry Pi Pico with only 2M flash!

You can read many details on this port in the blog post [here](https://kilograham.github.io/rp2040-doom/).

This fork only builds for the Pico (plus the native `whd_gen` WAD converter): the original PC (SDL) `chocolate-doom`
build and the never-built Heretic / Hexen / Strife code have been removed (see `BIBLE.md`).

This chocolate-doom commit that the code is branched off can be found in the `upstream` branch.

The original Chocolate Doom README is [here](README-chocolate.md).

## Pimoroni Pico Display Pack 2.0 / 2.8 (SPI LCD + buttons)

This fork can also run on a plain Raspberry Pi **Pico (RP2040)** or **Pico 2 (RP2350)** with a
[Pimoroni Pico Display Pack 2.0](https://shop.pimoroni.com/products/pico-display-pack-2-0)
(or the 2.8" version) plugged on: no VGA board, no wiring. It renders the normal 320x200 game and
streams it to the 320x240 ST7789 LCD. By default it is stretched to fill the screen at the 4:3 shape
Doom was designed for (every 5th line doubled), exactly like the original game on a CRT. Use
`-DDOOM_LCD_ASPECT_CORRECT=OFF` for square pixels with black bars instead.

Everything else is the same game: save/load (stored in flash), demo playback, all cheats, intermissions/finales,
and I2C multiplayer. Sound isn't needed for the Display Pack, but I2S audio still works if you wire up a DAC (see pins below).

### Quick start

You need `cmake`, a C/C++ compiler, `python3`, `git`, and the
[Arm GNU toolchain](https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads) (`arm-none-eabi-gcc`,
13.2.rel1 is what this was built and size-checked with) on your `PATH`.

```bash
git clone --recursive <this repo> && cd rp2040-doom
./build.sh                # Pico (RP2040) + Display Pack 2.0
./build.sh -b pico2       # Pico 2 (RP2350) + Display Pack 2.0
./build.sh -d PIMORONI_DISPLAY_28          # Display Pack 2.8
```

If `PICO_SDK_PATH` / `PICO_EXTRAS_PATH` are set (or `../pico-sdk` and `../pico-extras` exist) they are used;
otherwise `build.sh` downloads the SDK and pico-extras into `.deps/`. Output goes to `build-<board>-<display>/src/`.

Hold BOOTSEL, plug the Pico in, and drag **`doom_tiny_wad.uf2`** onto the drive. That one file contains the
program *and* the shareware `DOOM1.WAD` (as `doom1.whx`), and fits a 2MB Pico with ~22K spare for saved games.
(`doom_tiny.uf2` is the program alone, if you'd rather load the WHX yourself with
`picotool load -v -t bin doom1.whx -o 0x10040000`.)

The equivalent plain CMake invocation is:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=MinSizeRel -DPICO_BOARD=pico \
      -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_EXTRAS_PATH=/path/to/pico-extras
cmake --build build -j
```

### Controls

| Where | Button(s) | Action |
|---|---|---|
| Playing | **A** / **X** | turn left / turn right |
| | **B** | move forward |
| | **Y** | fire |
| | **A+X** tap | use (open doors, flip switches) |
| | **A+X** held 1s | menu (Esc) |
| | hold **A+X**, then **B** | move backward |
| | hold **A+X**, then tap **Y** | next weapon |
| | all four, tap | automap |
| | all four, hold 1.5s / 3s | `IDDQD` (god mode) / then `IDKFA` (all weapons, keys, ammo) |
| Menus | **A** / **B** | up / down (hold to repeat) |
| | **X** | select, or "yes" at a Y/N prompt |
| | **Y** | close menu / back out, or "no" at a prompt |
| | hold **Y** + **A** / **B** | left / right (sliders such as screen size) |
| Quit (ENDOOM) screen | any | restart |

Buttons can be combined with movement: e.g. hold B to run forward while tapping A+X to open a door, or hold Y to keep
firing while turning. Saving to an empty slot names it after the level (e.g. `E1M3`), since there is no keyboard.

For **every** cheat (`IDCLIP`, `IDCLEV`, `IDBEHOLD`...) and anything else needing typing, keyboard input still works too:
the `*_usb` builds accept a USB keyboard, and all builds accept keys from
[SDL Event Forwarder](https://github.com/kilograham/sdl_event_forwarder) over UART0 (GP0 TX / GP1 RX, 115200).

### Bluetooth gamepads (Pico 2 W)

`./build.sh -b pico2_w -t doom_tiny_bt` builds **`doom_tiny_bt_wad.uf2`**, which also accepts Bluetooth gamepads via
[Bluepad32](https://github.com/ricardoquesada/bluepad32) (8BitDo, PlayStation, Switch, Xbox...). Put the controller in
pairing mode after Doom has started; the Pico 2 W's LED lights while one is connected, and paired controllers reconnect
by themselves. An 8BitDo Zero 2 pairs with **B + Start** to power on, then **Select** held 3s.

D-pad-only pads (e.g. 8BitDo Zero 2): d-pad moves / turns, **L / R** strafe, **X** fire, **A** use, **B** run,
**Y** next weapon, **Select** automap, **Start** menu. Twin-stick pads: left stick moves / strafes, right stick turns,
RT fire, A use, LT/B run, LB/RB weapons. See `BIBLE.md` for details.

### Pins (Display Pack builds)

```
 0, 1:    UART0 TX/RX (stdio + SDL event forwarder keyboard input)
 2, 3:    I2C1 SDA/SCL - multiplayer link (moved from 18/19, which the LCD uses)
 6-8:     Display Pack 2.0 RGB LED (switched off)
 12-15:   Buttons A, B, X, Y
 16-20:   LCD DC, CS, SCK, MOSI, backlight
 26-28:   I2S DIN, BCK, LRCK (optional DAC; moved to 9-11 for the 2.8" pack whose LED is on 26-28)
```

`picotool info -a <uf2/elf>` lists them for any build.

### Multiplayer

The original I2C networking is unchanged apart from the pins: connect GP2 (SDA), GP3 (SCL) and GND between up to
four Picos running the same build, then pick the network game option from the main menu to host or join.
The internal pull-ups are enabled; for longer wires add 4.7K pull-ups from SDA/SCL to 3V3.

### Bigger WADs (Ultimate Doom, Doom II) on bigger flash boards

These need the `doom_tiny_nost` targets (WHD format) and a board with 8MB+ of flash, e.g. a Pimoroni Pico Plus 2 (16MB):

```bash
# build the converter (native, no SDL needed)
cmake -S . -B build-whdgen -DWHD_GEN_ONLY=ON && cmake --build build-whdgen -j
build-whdgen/src/whd_gen/whd_gen doom2.wad doom2.whd -no-super-tiny
# build, bundling the WHD into doom_tiny_nost_wad.uf2 (the WHD lives at 0x10048000)
./build.sh -b pimoroni_pico_plus2_rp2350 -w doom2.whd -t doom_tiny_nost
```

Saved games use whatever flash is left after the WHD; the flash size is detected at run time.

### Build options

| CMake option | Default | Meaning |
|---|---|---|
| `DOOM_DISPLAY` | `PIMORONI_DISPLAY_2` | `PIMORONI_DISPLAY_2` or `PIMORONI_DISPLAY_28` (the original VGA output has been removed) |
| `DOOM_LCD_ASPECT_CORRECT` | `ON` | stretch 320x200 to 320x240 (4:3) vs. 1:1 with bars |
| `DOOM_LCD_SPI_HZ` | `62500000` | max LCD SPI clock (in spec). At 270MHz this gives 45MHz, ~37 LCD fps; `67500000` gives 67.5MHz, ~55 fps, slightly over spec |
| `DOOM_LCD_ROTATE_180` | `OFF` | flip the picture |
| `DOOM_BUNDLE_WHX` / `DOOM_BUNDLE_WHD` | `doom1.whx` / none | data bundled into the `*_wad.uf2` files |

The link fails with a clear message if a binary would ever overlap its WAD data (it used to just silently corrupt it).

### Notes on what changed for this port

* `src/pico/st7789_lcd.*`: PIO + DMA write-only SPI driver for the ST7789. Core 1 converts each 8-bit line
  (plus status bar/menu overlays, exactly as for VGA) into RGB565 and streams it; because an LCD has no beam to
  race, falling behind just delays a frame rather than glitching. The path runs from RAM so the picture keeps
  updating while core 0 writes saved games to flash.
* `src/pico/i_buttons.c`: A/B/X/Y chords mapped to ordinary Doom key events. The game only asks for input once
  per tic, which can be 30-60ms apart, too coarse for chords and holds, so the whole button state machine runs on
  core 1 every 2ms (from the LCD interrupt) and queues finished key events for core 0. `-DDOOM_BUTTON_DEBUG=ON`
  draws the raw button/chord state and the game's input poll gaps in the top right corner of the LCD.
* Build fixes: RP2350 builds (hardware spin locks are now only forced on RP2040, per erratum E2),
  `PICO_SDK_PATH` from the environment is honoured, the SDK import no longer points at an internal git server,
  `whd_gen` builds with current GCC and without SDL2, and the USB WHX address moved to `0x10043000` because
  the current SDK's TinyUSB made `doom_tiny_usb` overflow `0x10042000`.

## Code State

Thus far, the focus has been entirely on getting RP2040 Doom running. Not a lot of time has been 
spent 
cleaning 
the code up. There are a bunch of defunct `#ifdefs` and other code that was useful at some point, 
but no longer are, and indeed changing them may result in non-functional code. This is particularly 
true of 
the 
`whd_gen` tool 
used to 
convert/compress WADs 
who's code is 
likely completely incomprehensible!  

## Goals

The original rp2040-doom's goals (it drove a VGA monitor; this fork drives a Display Pack LCD instead) were:

1. Everything should match the original game experience, i.e. all the graphics at classic 320x200 resolution, stereo
   sound,
   OPL2 music, save/load, demo playback, cheats, network multiplayer... basically it should feel like the original game.
2. `DOOM1.WAD` should run on a Raspberry Pi Pico. There was also to be no sneaky discarding of splash screens, altering of levels, down-sampling of
   textures or whatever. RP2040 boards with 8M should be able to play at least the full *Ultimate Doom* and *DOOM II*
   WADs.
3. The RP2040 should output directly to VGA (16 color pins for RGB565 along with HSync/VSync) along with stereo sound.

## Results

[![RP2040 Doom on a Raspberry Pi Pico](https://img.youtube.com/vi/eDVazQVycP4/maxresdefault.jpg)](https://youtu.be/eDVazQVycP4)

Features:

* Full `DOOM1.WAD` playable on Raspberry Pi Pico with 2M flash.
* *Ultimate Doom* and *Doom II* are playable on 8M devices.
* 320x200x60 VGA output (really 1280x1024x60).
* 9 Channel OPL2 Sound at 49716Hz.
* 9 Channel Stereo Sound Effects.
* I2C networking for up to 4 players.
* Save/Load of games.
* All cheats supported.
* Demos from original WADs run correctly.
* USB Keyboard Input support.
* All end scenes, intermissions, help screens etc. supported.
* Good frame rate; generally 30-35+ FPS.
* Uses 270Mhz overclock (requires flash chip that will run at 135Mhz)

# Building

RP2040 Doom should build fine on Linux and macOS. The RP2040 targeting builds should also work on Windows, though I 
haven't tried.

The build uses `CMake`.

## Native whd_gen build

The only native (non-Pico) build is the `whd_gen` WAD converter:

```bash
cmake -S . -B build-whd_gen -DWHD_GEN_ONLY=ON
cmake --build build-whd_gen -j
```

## RP2040 Doom builds

You must have [pico-sdk](https://github.com/raspberrypi/pico-sdk) and 
**the latest version of** [pico-extras](https://github.com/raspberrypi/pico-extras) installed, along with the regular 
pico-sdk requisites (e.g.
`arm-none-eabi-gcc`). If in doubt, see the Raspberry Pi
[documentation](https://datasheets.raspberrypi.com/pico/getting-started-with-pico.pdf). I have been building against 
the `develop` branch of `pico-sdk`, so I recommend that..

**NOTE: I am building with arm-none-eabi-gcc 13.2.rel1 .. whilst other versions may work, changes in compiler version may affect the binary size which, being tight, can cause problems (either link failure, or you may see stack overflow in the form of color palette corruption). Particularly I know tjhat arm-none-eabi-gcc 10.x versions don't work well.**

USB keyboard/mouse support (the `*_usb` targets) uses the TinyUSB bundled with the Pico SDK.

You can create a build directly like this:

```bash
mkdir rp2040-build
cd rp2040-build
cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DPICO_BOARD=pico -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_EXTRAS_PATH=/path/to/pico-extras ..
```

(`./build.sh` does this for you.) Use `PICO_BOARD=pico2` / `pico2_w` for RP2350 boards.

As before, use `make` or `make <target>` to build. 

The RP2040 version has four targets, each of which create a similarly named `UF2` file (e.g. `doom_tiny.uf2`). 
These UF2 files contain the executable code/data, but they do not contain the WAD data which is converted into a 
RP2040 Domom 
specific WHD/WHX format by `whd_gen` (for more see below). The WHD/WHX file must also be loaded onto the device at a 
specific address which varies by binary. 

"super-tiny" refers to RP2040 Doom builds that use the more compressed WHX format, and 
required for`DOOM1.
WAD` to 
run 
on a 2M Raspberry Pi Pico. "Non super-tiny" refers to RP2040 Doom builds that use the WHD format which is larger, but 
also is 
required for *Ultimate Doom* and *Doom II* WADs. These binaries are distinct as supporting both formats in the same 
binary would just have made things bigger and slower.


* **doom_tiny** This is a "super tiny" version with no USB keyboard support. You can use
[SDL Event Forwarder](https://github.com/kilograham/sdl_event_forwarder) to tunnel keyboard input from your host 
  computer over UART. The WHX file must be loaded at `0x10040000`. 
* **doom_tiny_usb** This is a "super tiny" version with additional USB keyboard support. Because of the extra USB 
  code, the WHX file must be loaded at `0x10043000` (it was `0x10042000`, but TinyUSB in the current SDK no longer fits). As you can see USB support via TinyUSB causes the binary to 
  grow by 2K (hence the move of the WHX file address) leaving less space for saved games (which are also stored in 
  flash).
* **doom_tiny_nost** This is a "non super tiny" version of `doom_tiny` supporting larger WADs stored as WHD. The WHD 
  file must be loaded at `0x10048000`
* **doom_tiny_nost_usb** This is a "non super tiny" version of `doom_tiny_usb` supporting larger WADs stored as 
  WHD. The WHD
  file must be loaded at `0x10048000`

You can load you WHD/WHX file using [picotool](https://github.com/raspberrypi/picotool). e.g.

```bash
picotool load -v -t bin doom1.whx -o 0x10043000
```

See `whd_gen` further below for generating `WHX` or `WHD` files.

#### USB keyboard support

Note that TinyUSB host mode support for keyboard may not work with all keyboards especially since the RP2040 Doom 
has been built with small limits for number/sizes of hubs etc. I know that Raspberry Pi keyboards work fine, as 
did my ancient 
Dell keyboard. Your keyboard may just do nothing, or may cause a crash. If so, for now, you are stuck forwarding 
keys from another PC via sdl_event_forwarder.

### RP2040 Doom builds not targeting an RP2040 device

You can also build the RP2040 Doom to run on your host computer (Linux or macOS) by using
[pico_host_sdl](https://github.com/raspberrypi/pico-host-sdl) which simulates RP2040 based video/audio output using SDL.

This version currently embeds the WHD/WHX in `src/tiny.whd.h` so you must generate this file.

You can do this via `./cup.sh <whd/whx_file>`

```bash
mkdir host-build
cd host-build
cmake -DPICO_PLATFORM=host -DPICO_SDK_PATH=/path/to/pico-sdk -DPICO_EXTRAS_PATH=/path/to/pico-extras -DPICO_SDK_PRE_LIST_DIRS=/path/to/pico_host_sdl ..
```

... and then `make` as usual.

## whd_gen

`doom1.whx` is included in this repository; for other WADs build `whd_gen` as described in
"Native whd_gen build" above.

To generate a WHX file (you must use this to convert DOOM1.WAD to run on a 2M Raspberry Pi Pico)

```bash
whd_gen <wad_file> <whx_file>
```

The larger WADs (e.g. *Ultimate Doom* or *Doom II* have levels which are too complex to convert into a super tiny 
WHX file. These larger WADs are not going to fit in a 2M flash anywy, so the less compressed WHD format can be used 
given that the device now probably has 8M of flash.

```bash
whd_gen <wad_file> <whd_file> -no-super-tiny
```

Note that `whd_gen` has not been tested with a wide variety of WADs, so whilst it is possible that non Id WADs may 
work, it is by no means guaranteed!

NOTE: You should use a release build of `whd_gen` for the best sound effect fidelity, as the debug build 
deliberately lowers the encoding quality for the sake of speed.

# Running the RP2040 version

See "Quick start" and "Pins (Display Pack builds)" above. `picotool info -a <uf2/elf>` lists the pins any build uses.

# Future

*Evilution* and *Plutonia* are not yet supported. There is an issue tracking it 
[here](https://github.com/kilograham/rp2040-doom/issues/1).

# RP2040 Doom Licenses

* Any code derived from chocolate-doom matinains its existing license (generally GPLv2). 
* New RP2040 Doom specific code not implementing existing chocolate-doom interfaces is licensed BSD-3.
* ADPCM-XA is unmodified and is licensed BSD-3.
* Modified emu8950 derived code retains its MIT license.

