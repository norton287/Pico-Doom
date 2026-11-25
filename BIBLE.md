# BIBLE: Pico Doom on the Pimoroni Display Pack

The single source of truth for this fork: what it runs on, how it's built and flashed, how input works and
why, and what changed when. **Update it (including the change log) in the same commit as any behaviour, build,
flash-layout or input change.** It overrides the older notes in README/HACKING where they disagree.

## 1. What it is

A fork of [kilograham/rp2040-doom](https://github.com/kilograham/rp2040-doom) (shareware Doom on RP2040/RP2350)
ported to a **Pimoroni Pico Display Pack 2.0 / 2.8** (ST7789 320x240 SPI LCD + A/B/X/Y buttons), with optional
**Bluetooth gamepads** on a **Pico 2 W**. Sound isn't used (I2S still works if a DAC is wired).

- Branch: `pimoroni-display-pack`, pushed to **github.com/norton287/Pico-Doom** (`main`) via remote
  `pico-doom`. `origin` is kilograham's upstream, fetch only. Licence: GPL-2.0 (`COPYING.md`), as for all
  Doom-derived code - never add a different licence file.
- Owner's hardware: Pico 2 W + Display Pack 2.0, **8BitDo Zero 2** controller.
- Backup of the board's original MicroPython arcade: `../pico2w-backup-2026-11-22/`
  (`RESTORE_micropython_arcade.uf2` = exact 4MB flash image; `files/` = its filesystem incl. WiFi config).

## 2. Build

**What builds:** only Pico firmware (`doom_tiny`, `doom_tiny_nost`, `doom_tiny_usb`, `doom_tiny_nost_usb`, and
`doom_tiny_bt` on a Pico 2 W) and the native `whd_gen` converter (`-DWHD_GEN_ONLY=ON`). The PC/SDL build,
Heretic/Hexen/Strife, autotools and installer packaging, and the original VGA display output were removed on
2026-11-24 (all firmware verified byte-identical before/after). `DOOM_DISPLAY` is `PIMORONI_DISPLAY_2` (default) or
`PIMORONI_DISPLAY_28`. USB keyboard/mouse builds stay, on the SDK's TinyUSB. Configuring without the Pico SDK stops with a message saying so.
The Pico build still uses `opl/` (music synth), `src/adpcm-xq` (sound compression) and
`textscreen/fonts/normal.h` (the quit-screen font) from the inherited tree.

Toolchain on this box lives in `/opt/pico`:
`arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi`, `pico-sdk` (develop, with `lib/btstack` and
`lib/cyw43-driver`), `pico-extras`, `bluepad32` (commit 80d6314, 5.0.0-beta0).

```bash
export PICO_SDK_PATH=/opt/pico/pico-sdk PICO_EXTRAS_PATH=/opt/pico/pico-extras
export PATH=/opt/pico/arm-gnu-toolchain-13.2.Rel1-x86_64-arm-none-eabi/bin:$PATH
./build.sh -b pico2_w -t doom_tiny_bt -D BLUEPAD32_ROOT=/opt/pico/bluepad32   # Bluetooth build
./build.sh -b pico2_w -t doom_tiny    -D BLUEPAD32_ROOT=/opt/pico/bluepad32   # buttons only
```

Output: `build-pico2_w-pimoroni_display_2/src/<target>_wad.uf2` (program + WAD in one file).
`build.sh` reuses the CMake cache, so **`-D` options stick** until set back
(e.g. pass `-D DOOM_BUTTON_DEBUG=OFF -D DOOM_BT_DEBUG=OFF` for a release build after a debug one).

Build options (Display Pack builds):

| Option | Default | Effect |
|---|---|---|
| `DOOM_BLUETOOTH` | ON for `PICO_BOARD=pico2_w` | adds the `doom_tiny_bt` target |
| `DOOM_BUTTON_DEBUG` | OFF | top-right strip: raw buttons, chord state, and a bar = longest gap between the game's input polls in the last second (1px/ms; green <50ms, yellow <150, red beyond) |
| `DOOM_BT_DEBUG` | OFF | top-left Bluetooth strip (section 6) |
| `DOOM_LCD_ASPECT_CORRECT` | ON | 320x200 stretched to 4:3; OFF = square pixels with bars |

## 3. Flash

Doom doesn't enumerate USB, so **every flash needs BOOTSEL held while plugging in**. Then:

```bash
/opt/pico/picotool-usb/picotool load -v build-pico2_w-pimoroni_display_2/src/doom_tiny_bt_wad.uf2
/opt/pico/picotool-usb/picotool reboot
```

(`/opt/pico/picotool-usb` is the USB-enabled picotool.) Never leave a background "wait for BOOTSEL and flash"
watcher running at the end of a session.

Flash layout, Bluetooth build (4MB Pico 2 W): program from 0x10000000; WHX (WAD) at **0x10100000**
(Bluetooth + cyw43 firmware make the program ~1MB); saved games just below the top of flash, kept clear of
BTstack's pairing keys by `DOOM_FLASH_RESERVED_TOP=12288` (pairing keys = the 8K below the last 4K sector).
Buttons-only builds keep the WHX at 0x10040000.

## 4. Display + Display Pack buttons

- PIO+DMA ST7789 driver (`src/pico/st7789_lcd.*`); **core 1** generates RGB565 lines in an IRQ and streams
  them; RAM resident so the picture keeps going while saves are written to flash.
- Button state machine (`src/pico/i_buttons.c`) runs on **core 1 every 2ms** (ticked from the LCD fill IRQ),
  because the game polls input only once per tic (30-60ms apart) and chords were missed when it ran there.
  Core 0's `I_ButtonsPoll` drains an SPSC key queue and holds back key-ups whose key went down in the same poll
  (otherwise a quick tap is invisible to the per-tic key state).
- Button controls are in the README "Controls" table.

## 5. Bluetooth gamepads (Pico 2 W, `doom_tiny_bt`)

Files: `src/pico/i_bluetooth.c/.h`, `src/pico/bt/{btstack_config.h,sdkconfig.h}`, pad layer `pad_process`
in `src/pico/i_buttons.c`.

- Bluepad32 + BTstack run in the cyw43 **threadsafe-background async context** (a low-priority IRQ on core 0),
  so the game loop never calls them. Reports are copied into a seqlock snapshot (`bt_pad`) which core 1's 2ms
  tick turns into Doom key events alongside the Display Pack buttons.
- Init (`I_BluetoothInit`, from `d_main.c` after graphics/core 1 are up): claim DMA 9/10/11 first (used
  unclaimed by I2C net and the status bar stream), then `cyw43_arch_init`, then `uni_init`. Core 1 calls
  `multicore_lockout_victim_init` so BTstack can write pairing keys with `flash_safe_execute`.
- `CYW43_PIO_CLOCK_DIV_INT=4`: the system clock is overclocked to 270MHz, so the cyw43 SPI is slowed to ~34MHz.
- No malloc from the Bluetooth IRQ (static BTstack pools) - Doom's allocator isn't re-entrant.
- Pico 2 W onboard LED = a controller is connected (it flashes on during radio init, then goes off while
  scanning).
- Pairing keys persist in flash, so a paired controller reconnects by itself.

**Pairing the 8BitDo Zero 2:** power on with **B + Start** (D-input / Android mode), then hold **Select 3s**
until its LED blinks fast. LED solid = connected. The Zero 2 has no separate pair button. (Other 8BitDo pads:
their pair button, 3s.)

### Layouts

The layout is chosen per connection:

- **D-pad-only pads (8BitDo Zero 2):** their d-pad arrives as the *left stick* at only centre or full
  deflection, with no right stick. Detection: every controller starts in this layout and switches permanently
  to twin-stick the first time a report shows a real analog value (`bt_report_is_analog`: a stick part way
  out, or the right stick / triggers past 25%). A name containing "Zero" also forces it, but names proved
  unreliable (the Zero 2's didn't match), so behaviour is what counts.

| D-pad-only (Zero 2) | Action |
|---|---|
| D-pad up/down | forward / back |
| D-pad left/right | **turn** |
| L / R | strafe left / right |
| X | fire |
| A | use |
| B | run (also: fast turning, see below) |
| Y | next weapon |
| Select | automap |
| Start | menu |

| Twin-stick pads | Action |
|---|---|
| Left stick | move / strafe |
| Right stick or d-pad left/right | turn |
| RT or X | fire |
| A | use |
| B or LT | run |
| LB / RB | previous / next weapon |
| Y or Select | automap |
| Start | menu |

Menus (all pads): d-pad/stick to move, A select / yes, B or Start back / no.

### Turning (all button / d-pad input)

`src/doom/g_game.c`, under `PICO_BUTTON_INPUT`: vanilla's turn (6 tics at 320, then 640 walking / 1280
running) felt laggy, and fixed fast steps felt jerky and overshot. Now a turn starts at **640** (a tap is a
~3.5° fine aim) and ramps linearly over `TURN_RAMP_TICS` = 8 tics to `TURN_MAX` = **896** (~4.9°/tic), or to
**1280** while run is held for quick spins. The LCD shows the game a frame or two late, so overshoot is roughly
release speed x that delay; that's why the normal top speed is capped.

## 6. Bluetooth debug strip (`-DDOOM_BT_DEBUG=ON`)

Top left, squares left to right. Counters are dark until they move, then cycle red, yellow, cyan, magenta per step.

1. Stage: grey not started, yellow inside radio init (stuck = driver hang), red radio failed, blue radio OK,
   magenta stack starting, green scanning.
2. Heartbeat: cycles every 250ms while the Bluetooth context runs (frozen = Bluetooth hung).
3. Discovered (after Bluepad32's allow-list / RSSI / class filters).
4. Connected. 5. Ready (**green** = d-pad-only layout active). 6. Reports received.
- gap - raw HCI, before any filtering: 7. inquiry cycles, 8. classic inquiry results, 9. BLE adverts,
  10. rejected HCI commands (should stay dark), 11. incoming connection requests.
- gap - white while the latest report has: left X out, left Y out, hat d-pad pressed, right X out.

## 7. Tests

Host tests compile the real `i_buttons.c` against small stubs for the Pico SDK / Doom headers (pad mapping:
Zero 2 turn/strafe/weapon/map, dead zone, twin-stick unchanged; and `bt_report_is_analog`). They currently live
in the session scratchpad, not the repo; rebuild them if `pad_process` or `i_bluetooth.h` changes. Hardware is the
final test: flash and play.

## 8. Status / open items

- Working on the owner's board (Pico 2 W, release build, 2026-11-24): Display Pack build, Bluetooth build,
  Zero 2 pairing, controls, turning, **save and load across a power cycle**, and the Zero 2 **reconnecting by
  itself** afterwards (saves don't disturb the pairing keys).
- Repo: public **github.com/norton287/Pico-Doom**, remote `pico-doom`, branch `pimoroni-display-pack` pushed
  as `main`. `origin` = kilograham upstream, fetch only (push URL disabled). Licence GPL-2.0 (`COPYING.md`).
- The README's main "Controls" table covers the Display Pack buttons; Bluetooth controls are summarised there and
  detailed here.

## 9. Change log

- **2026-11-22** - Display Pack port (ST7789 driver, core-1 button state machine, debug strip), Bluetooth
  gamepad build, build fixes (commit a2908e07). Display Pack build confirmed on hardware.
- **2026-11-24** - Bluetooth confirmed on hardware with an 8BitDo Zero 2. "No pairing" was the controller not
  being in pairing mode (Select 3s on the Zero 2). Added the `DOOM_BT_DEBUG` bring-up strip; d-pad-only
  layout with behaviour-based detection (the Zero 2's d-pad arrives as the left stick and was strafing); turn
  acceleration with capped top speed for button / d-pad turning; BIBLE.md started.
  Release build verified on hardware: save/load across a power cycle works and the Zero 2 reconnects by
  itself. Published to github.com/norton287/Pico-Doom (replacing GitHub's MIT-licence initial commit).
- **2026-11-24** - Trim stage 1: removed the never-built Heretic/Hexen/Strife sources, the PC/SDL build
  (setup, textscreen except its font, pcsound, midiproc, SDL platform/net sources, OPL PC backends, win32, man,
  data, packaging), autotools, Travis, stale scripts/images and the unused TinyUSB submodule (USB builds use
  the SDK's TinyUSB). All 17 firmware binaries byte-identical to before.
- **2026-11-24** - Trim stage 2: removed the VGA (vgaboard / scanvideo) display path - `video_doom.pio`, the
  non-LCD half of `src/pico/i_video.c` (-528 lines, resolved with `unifdef -DPICO_DOOM_LCD=1`), the VGA
  CMake/build.sh options and README VGA build/pin sections. The `PICO_SCANVIDEO_*` defines stay: the LCD code
  uses them to size its text buffers. All 17 binaries byte-identical again. Host (`PICO_PLATFORM=host` / pico_host_sdl)
  builds went with it: they used the VGA path, and the README section describing them was removed.
- **2026-11-24** - Trim cleanup: removed the dead `PICO_NO_HARDWARE` (host) branches in `src/pico/i_input.c` /
  `i_system.c`, the `chocolate-doom` and host-mode leftovers in `src/CMakeLists.txt`, and the broken `.lvimrc`
  (it looked for autotools' `configure.in`); `build.sh -h` no longer prints a stray line; README lists
  `doom_tiny_bt` and points to upstream for its `upstream` branch. Firmware byte-identical again.
