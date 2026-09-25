




<img width="4000" height="3000" alt="inkterface now playing" src="https://github.com/user-attachments/assets/2416b7df-2457-450f-980d-5646910e5a54" />

# Inkterface: Now Playing Edition

An e-ink faceplate for your Steam Machine that shows the box art, playtime,
and achievement progress of whatever you're playing — plus all the original
system telemetry readouts.

<img width="426" height="240" alt="Video Project 2" src="https://github.com/user-attachments/assets/fad7e269-4a9d-436a-a6c9-2e3d17211624" />

> This is a community fork of
> [Valve's Inkterface](https://gitlab.steamos.cloud/SteamHardware/SteamMachine/inkterface),
> created by NaKyle Wright at Valve and released under the MIT license.
> All of the hardware design, assembly docs, and the original firmware and
> companion app are their work — this fork adds the artwork/"now playing"
> features and easier install tooling on top. Thank you Valve for open
> sourcing it!

## What this fork adds

* **Now Playing artwork mode**: when you launch a game, the panel switches to
  that game's box art with your total playtime and achievement progress
  rendered next to it. E-paper panels receive the existing dithered monochrome
  frame; the Waveshare LCD-5B receives a full-color 1024×600 frame. When you
  quit, it goes back to the regular telemetry layout. Toggle it with the
  `Box Art` button in the configure screen.
* **Game collectors**: `Game Time` and `Achievements` readouts you can put in
  any panel field, alongside the stock CPU/GPU/fan stats.
* **Browser firmware flasher**: flash a supported Feather or LCD-5B from
  Chrome/Edge at the project's GitHub Pages site — no toolchain install needed.
* **One-line app install**: paste a single command on your Steam Machine and
  you're done — no building, no USB sticks.

Achievement data comes from the Steam Community profile endpoint and requires
no API key, but your profile's game details need to be public for it to show.

## Known issues

* **A game's box art can stay "stuck" on the panel after you quit it.** Some
  titles (DREDGE is a known one) leave a background process running under
  Proton, so Steam still reports the game as active even though you're back in
  the library — the panel faithfully keeps showing its art. Restarting Steam
  (or the machine) clears the leftover process and the panel returns to normal.
  Most games quit cleanly; this only affects the few that linger.

## Quick Install (this fork)

**Firmware** — plug the Feather or Waveshare LCD-5B into a PC over USB and
open this fork's [web flasher](https://wully616.github.io/inkterface/) in
Chrome or Edge, then click Install for your board. The LCD-5B firmware is also
built from source with the `lcd5b` PlatformIO environment below.

**App** — on the Steam Machine, open a terminal in desktop mode and run this
after a release containing the LCD-5B support is published:

```sh
curl -fsSL https://raw.githubusercontent.com/Wully616/inkterface/main/scripts/install.sh | sh
```

That downloads the latest release from this fork to `~/Applications`, launches
it, and from there you pick your panel and click Install (bottom right) to set
up the background service. Until a release containing this feature is
published, build and install the current branch using the instructions below.
Prebuilt AppImages are also on the
[fork's releases page](https://github.com/Wully616/inkterface/releases) if you'd
rather grab one manually.

Everything below is the original upstream documentation for building the
hardware and working from source.


## Required Hardware

* 1 x Adafruit ESP32-S3 Feather w/ 2MB PSRAM
    * [Link to V1 Board, as shown in the assembly video and document!](https://www.adafruit.com/product/5477)
    * [Link to V2 Board, accidentally the only link previously!](https://www.adafruit.com/product/5400)
    * Both the V1 and V2 modules work, but they have different pinouts, make sure
      to check if you see the "V2" silkscreen on your board and choose the correct
      pinout sheet and environment when building the firmware!
* 1 x [Adafruit eInk Breakout Friend](https://www.adafruit.com/product/4224)
* 1 x [Adafruit 5.83" Monochrome eInk Panel](https://www.adafruit.com/product/6397)
* 1 x [LP803860 Battery](https://www.adafruit.com/product/2011)
* 13 x M2.5 x 5mm Pan Head Machine Screws
    * [McMaster](https://www.mcmaster.com/92000A103/)
    * [Amazon](https://www.amazon.com/dp/B0GC58C2V5)
* 4 x [1/4" x 1/4" x 3/16" Stepped Magnet SB443-OUT](https://www.kjmagnetics.com/sb443-out-neodymium-stepped-block-magnet)

### Alternative Display: Waveshare ESP32-S3-LCD-5B

The Waveshare ESP32-S3-LCD-5B can run Inkterface directly, without the Feather,
eInk breakout, or eInk panel listed above. It uses an ESP32-S3-WROOM-1-N16R8,
an ST7262 RGB display at 1024×600, and the CH422G I/O expander for LCD reset
and backlight control. The LCD firmware draws the status layout on the native
1024×600 canvas with a four-color palette. Game artwork is composed at native
resolution on the Steam Machine, JPEG-compressed for BLE, then decoded to
RGB565 on the panel. Feather firmware and its monochrome artwork protocol are
unchanged. Touch input is not needed by Inkterface.

Waveshare groups the LCD-5B and Touch-LCD-5B in its [resources and
documents](https://docs.waveshare.com/ESP32-S3-Touch-LCD-5/Resources-And-Documents).
The LCD-5B target is configured for 16 MB QIO flash and 8 MB OPI PSRAM.
Its PlatformIO environment uses the pinned pioarduino Arduino 3 / ESP-IDF 5
platform for double-buffered RGB presentation with SRAM bounce buffers;
the Feather targets continue to use the existing PlatformIO ESP32 platform.


## Assembly

Check out the assembly tutorial video in
[the upstream repo](https://gitlab.steamos.cloud/SteamHardware/SteamMachine/inkterface/-/blob/main/docs/Inkterface%20Assembly.mp4)
(kept out of this fork because it exceeds GitHub's file size limit),
or if you'd prefer there is a PDF version too `./docs/Inkterface Assembly.pdf`.

**_Warning: The screws will thread themselves into the plastic, but be VERY gentle
as it is easy to strip the plastic away and you may need to re-print parts!_**

1. Print parts from `./cad` folder.
    * Individual parts are included as separate STEP files, but a combined file
      with all parts that is ready for printing on a Stratasys F370 build plate
      is also provided as `Inkterface - Print Plate.step`.
2. Attach the ESP32 Feather and eInk Breakout to the board plate layer using 4x screws.
    * Having them held in place helps with soldering the connections and sizing your wires.
3. Solder wires between the pins of the two boards.
    * MOSI/MISO/SCK/GND all connect directly as expected.
    * 3V3 from the feather connects to VIN on the breakout.
        * The 3V3 pin on the breakout is a 3V3 output.
    * For the **_V1_** feather connect these pins, like Feather -> Breakout:
        * PIN 6 -> ENA
        * PIN 9 -> BUSY
        * PIN 10 -> RST
        * PIN 11 -> SRCS
        * PIN 12 -> D/C
        * PIN 13 -> ECS
    * For the **_V2_** feather connect these pins, like Feather -> Breakout:
        * PIN 32 -> ENA
        * PIN 15 -> BUSY
        * PIN 33 -> RST
        * PIN 27 -> SRCS
        * PIN 12 -> D/C
        * PIN 13 -> ECS
    * **_NOTE:_** On both feather versions we connect to pins in the same physical
        positions, but they are different logical pins in the firmware.
4. Place the e-ink panel into the matching recess in the faceplate part.
5. Fit the two midplates in.
6. Place the 4 magnets into the recesses in each corner of of the midplates.
    * Magnetic field orientation doesn't matter, they connect to metal slugs
      in the Steam Machine chassis.
7. Place the board plate over the midplates, aligning the magnets and screw holes.
8. Fasten the boardplate with 8x screws in the screw holes along the edges.
9. Connect the e-ink panel to the breakout board.
    * Be careful when handling the flex that comes out of the panel, it is fragile
      and should not be bent or folded.
10. Place the battery in the recess, tucking its wire under the board plate and
    up into the clearance hole beside the feather.
11. Insert and fasten down the battery retainer/cover.
12. Plug the battery connecor into the feather.

Once you've assembled the unit it should be fairly solid and easily magnet to the
front of a Steam Machine!

You will need to build/flash the firmware onto the feather using the steps below.


## Usage

[Eventually we'll have an app up on Steam](https://store.steampowered.com/app/1222770)
but until then you can build an AppImage using the instructions further down in
this readme.

Once you have it built, take a look at `./docs/Inkterface Setup.pdf`, but the
basics are:
1. Enable bluetooth.
2. In desktop mode register as an app in Steam.
    * Or you can just run it directly.
3. Wait for it to discover your panel.
    * If it doesn't appear try clicking the reset button on the back and checking
      that bluetooth is enabled.
4. Select your panel, the name shown should match what's displayed on the inkterface.
    * They all start with an `INKTF-` prefix and then use a unique portion of the
      panels BLE MAC address.
5. On the configure screen, install the service using the button at the bottom
   right.
    * This will set itself up as a user service, so don't move the AppImage or
      that will break, no biggie though, you'll just need to re-run it.
    * Once the service is installed it might take 10-20 seconds for it to connect
      to the inkterface, that's fine, it just needs to go through discovery.
6. Finally you can click any of the readouts to adjust what they display.
    * We have several stats built in, but if you check out `include/panel-state.hpp`
      you can add any function that can return a `QString` or `double` and then
      `registerCollector()` to have it show up in the list.
      * There are some examples of stateful and idempotent collectors in `SysStats`,
        they get registered in the `PanelState` constructor.
7. Exit the configuration app.
    * The service is lightweight and runs in the background, you only need to
      run the config software to select a panel or to change what it displays.


## Building Interface

If you have Qt installed you should be able to open the cmake project in Qt Creator
and use that.

For better distributable builds you can setup a container for wider platform
support using the included `Containerfile`.

1. `podman build -t qt69-builder -f ./Containerfile .`
2. `distrobox create --image qt69-builder --name qt69`
3. `distrobox enter qt69`
4. `./scripts/build.sh deploy`

That should produce an AppImage for you in the `./dist-linux-x86_64` folder.

This container uses an older version of Ubuntu and Qt 6.9 which should let us
build AppImages that will work on a wide range of modern systems.

### Build and install on a Steam Machine

First commit and push the `feature/esp32-s3-lcd-5b` branch to your fork from the
development PC. Then, in SteamOS Desktop Mode, clone the branch and build it:

```sh
git clone https://github.com/Wully616/inkterface.git ~/inkterface
cd ~/inkterface
git checkout feature/esp32-s3-lcd-5b
podman build -t qt69-builder -f ./Containerfile .
distrobox create --image qt69-builder --name qt69
distrobox enter qt69
cd ~/inkterface
./scripts/build.sh deploy
exit
mkdir -p ~/Applications
cp dist-linux-x86_64/Inkterface.AppImage ~/Applications/Inkterface.AppImage
chmod +x ~/Applications/Inkterface.AppImage
~/Applications/Inkterface.AppImage
```

In Inkterface, select the `INKTF-...` panel and click **Install** to configure
the background service. Keep the AppImage at
`~/Applications/Inkterface.AppImage` after the service is installed. Enable
Bluetooth on the Steam Machine before launching the app.


## Building Firmware

For a mix of convenience and approachability this project uses
[PlatformIO](https://platformio.org), and in particular here you'll use the
[pio run](https://docs.platformio.org/en/latest/core/userguide/cmd_run.html)
command. Reading up on them can be useful, especial options like `--upload-port`
which could help if you have multiple serial ports.

If you're using the container from above, build and upload for the board you
have connected:

1. `distrobox enter qt69`
2. `cd ./firmware`
3. If you have a **_V1_** feather: `pio run -e featherv1 -t upload`
4. If you have a **_V2_** feather: `pio run -e featherv2 -t upload`
5. For the **Waveshare ESP32-S3-LCD-5B**: `pio run -e lcd5b -t upload`

And it should build and flash the firmware, if you want to get setup manually:

1. Install [PlatformIO](https://platformio.org) for your system.
2. `cd ./firmware`
3. If you have a **_V1_** feather: `pio run -e featherv1 -t upload`
4. If you have a **_V2_** feather: `pio run -e featherv2 -t upload`
5. For the **Waveshare ESP32-S3-LCD-5B**: `pio run -e lcd5b -t upload`

The first LCD-5B build downloads its pinned Arduino 3.3.12 / ESP-IDF 5.5.5
PlatformIO platform and toolchain. Feather builds continue using the existing
PlatformIO ESP32 platform.

On Windows PowerShell, if `pio` is not on `PATH`, install PlatformIO in a
temporary Python environment and call its executable directly:

```powershell
$pioEnv = Join-Path $env:TEMP 'inkterface-pio-venv'
python -m venv $pioEnv
& (Join-Path $pioEnv 'Scripts\python.exe') -m pip install --upgrade pip platformio
$pio = Join-Path $pioEnv 'Scripts\platformio.exe'
Set-Location .\firmware
& $pio device list
$port = 'COM3' # replace with the LCD-5B port listed above
& $pio run -e lcd5b -t upload --upload-port $port
& $pio device monitor --port $port --baud 115200
```

The port can change after reconnecting or resetting the board, so run
`pio device list` again if it cannot be opened. Close the monitor with Ctrl+C
before another upload. To capture startup and animation logs, start the monitor
and then press RESET on the board.

Connect the selected board by USB and allow PlatformIO to access its serial
port. For the LCD-5B, use a data-capable USB-C cable. If upload cannot find the
board, hold **BOOT**, reconnect USB, and release **BOOT** after power-up to enter
download mode; then rerun the upload command and press **RESET** after it finishes.
Waveshare documents this recovery sequence in its
[LCD-5B user guide](https://docs.waveshare.com/ESP32-S3-Touch-LCD-5/FAQ).

For manual selection of the LCD-5B serial port, append
`--upload-port COM5` on Windows or `--upload-port /dev/ttyACM0` on Linux (replace
the example port with the one shown by your system). To build without flashing,
run `pio run -e lcd5b`; the firmware is written to
`firmware/.pio/build/lcd5b/firmware.bin`. The release workflow also creates a
merged image named `inkterface-lcd5b.bin` that can be installed from the web
flasher or with esptool. The web flasher becomes available after the branch's
Pages workflow has been run.

For a prebuilt release image, download `inkterface-lcd5b.bin` from the GitHub
release, put the board into download mode if needed, and flash it with esptool
(replace `COM5` with the board's port; on Linux use a path such as
`/dev/ttyACM0`):

```sh
python -m esptool --chip esp32s3 --port COM5 --baud 460800 write-flash \
  --flash-mode qio --flash-size 16MB 0x0 inkterface-lcd5b.bin
```

### LCD-5B animation diagnostic

To check the LCD rendering path without BLE or the Steam Machine app, build and
flash the standalone animation target:

```sh
pio run -e lcd5b_animation -t upload --upload-port COM5
```

In Windows PowerShell, use the executable path above with
`-e lcd5b_animation` in place of `-e lcd5b`.

Replace `COM5` with the LCD-5B serial port (`/dev/ttyACM0` on Linux). The screen
shows a moving high-contrast bar while cycling through 15, 26, and 41 FPS target
rates; it changes the target every eight seconds. A still image between frames
is expected, but visible tearing is not. The serial monitor at 115200 baud
reports achieved FPS and average/maximum full-frame, raster, and framebuffer-swap
wait times once per second:

```sh
pio device list
pio device monitor --port COM5 --baud 115200
```

Start the monitor before pressing RESET so it catches the boot banner and display
initialization messages. Windows may assign a different COM number after reset;
use the port shown by `pio device list`. This test uses the same native-size
RGB565 framebuffers and complete-frame presentation path as the regular LCD
firmware. The LCD driver feeds the panel from two internal-SRAM bounce buffers
to avoid RGB scanout stalls while the full framebuffers remain in PSRAM. The
test skips BLE and telemetry.

The normal BLE-enabled `lcd5b` firmware also reports one `LCD update:` line for
each display refresh. Its `compose` time covers status drawing,
`raster` is the palette expansion or JPEG decode into RGB565, and
`present-wait` is the wait for the RGB driver to finish using the previous
framebuffer. Use the same 115200-baud monitor while connected to the Steam
Machine to see these lines.

The four-color indexed status canvas is allocated from internal SRAM after the
RGB driver has reserved its DMA memory; if that heap is too tight, the firmware
falls back to PSRAM. The displayed layout and color artwork both use the full
1024×600 resolution. E-paper artwork remains 648×480 and one bit per pixel.

Waveshare's [performance PDF](https://files.waveshare.com/wiki/common/Performance.pdf)
benchmarks LVGL on different screen sizes and RGB/parallel interfaces, so its FPS
figures are not directly comparable to this renderer. This target uses
the applicable settings: performance compiler optimization, a 240 MHz CPU, QIO
flash at 80 MHz, octal PSRAM at 80 MHz, a 64-byte data cache line, PSRAM
instruction/rodata fetch, experimental feature support, and a 1 kHz FreeRTOS
tick. The standard
ESP32-S3-WROOM-1-N16R8 module is rated for 80 MHz flash and PSRAM; Waveshare's
generic 120 MHz settings are not supported by that module. Its LVGL-specific
memory options do not apply because this firmware uses Adafruit_GFX and a custom
RGB565 renderer, not LVGL.

The LCD-5B board exposes its backlight enable through CH422G `EXIO2`; variable
brightness needs a wire from the AP3032 `CTRL`/PWM point to ESP32-S3 GPIO43. The
firmware keeps `EXIO2` enabled and drives GPIO43 with 30 kHz LEDC PWM. The
Steam Machine app's LCD Backlight slider saves its value and sends it over BLE.
The slider only dims the panel after this hardware mod; an unmodified board
continues to run at full brightness. GPIO43 is also the board's RS485 receive
signal, so that input is no longer available while it is used for PWM.

Waveshare's [schematic](https://files.waveshare.com/wiki/ESP32-S3-Touch-LCD-5/ESP32-S3-Touch-LCD-5-Sch.pdf)
maps GPIO43 to `RS485_RXD` and shows the AP3032 `CTRL` input; the
[AP3032 datasheet](https://www.diodes.com/datasheet/download/AP3032.pdf)
recommends PWM above 25 kHz to avoid audible noise.
A [Home Assistant community report](https://community.home-assistant.io/t/esp32-s3-7inch-capacitive-touch-display-adjust-brightness/771030/21?page=2)
from an owner of the 5-inch 1024×600 board reports successful GPIO43 dimming.
Note that the RS485 A/B screw-terminal bus pins are separate from the ESP32's
GPIO43/RXD signal. Follow the GPIO43 connection in the marked reference image;
do not assume that the differential A terminal is GPIO43.

The LCD runs at a 21 MHz pixel clock with the configured 1368 × 637 total timing,
which gives a nominal scan rate of about 24 frames per second. Waveshare's
41-FPS interface benchmark is not a promise that this panel can scan 41 complete
frames per second. The 15-FPS animation phase is a useful stability check; 26 and
41 FPS are load targets that can exceed the panel's scan rate. A still frame
between updates is expected, but tearing is not. The serial timings distinguish
software frame-render time from wait-for-presentation time. For example, a
194 ms render can only produce about 5 frames per second even if the requested
target is 15 FPS. The renderer reuses the framebuffer margins instead of
clearing the full PSRAM frame for every update. Afterward, restore the normal
firmware with
`pio run -e lcd5b -t upload --upload-port COM5`.

### Test the LCD-5B with the Steam Machine

The original Steam Machine app image remains BLE-compatible and will continue to
send its existing monochrome artwork and 30-second telemetry updates. To get
two-second LCD telemetry updates and full-color, native-resolution game artwork,
build and install the Steam Machine app from this branch. Its LCD Backlight
slider is shown for the LCD-5B and remembers the chosen percentage.

1. Flash the board with `pio run -e lcd5b -t upload`, or use the LCD-5B button
   on the browser flasher when that branch's Pages build has been deployed.
2. Press RESET. The 1024×600 screen should show the Inkterface status layout;
   the LCD-5B advertises as `INKTF-5B-` followed by six hex digits. The Steam
   Machine app built from this branch recognizes that marker and uses a 2 second
   state-send interval, matching its telemetry sampling rate; the Feather
   e-paper panels keep their 30 second interval.
3. Enable Bluetooth on the Steam Machine and launch Inkterface. Select the
   matching `INKTF-...` panel. The host name and system telemetry should update.
4. Enable **Box Art**, launch a Steam game, then quit it. The panel should show
   the game's artwork while it is running and return to telemetry afterward.
5. Reset the LCD-5B and check that Inkterface rediscovers it and reconnects.

If the LCD-5B does not appear as a serial port, hold BOOT, connect USB, and
release BOOT to enter download mode. After flashing, press RESET. See Waveshare's
[USB download instructions](https://docs.waveshare.com/ESP32-S3-Touch-LCD-5/Instructions-For-Use)
for the board-specific recovery sequence.

**_NOTE:_** You can add build definitions and support for other boards by modifying
the `platformio.ini`, check it out to see the V1 and V2 feather environments.


## Design Docs

Planning out the UI and panel design is done in a Lunacy document you can find
in the `./design` folder.

Lunacy is an open source alternative to tools like Figma that can work entirely
offline.


## License

This project is licensed under the MIT License, see LICENSE.

This project depends on third-party software that is distributed under its own
licenses. Those licenses remain applicable to the respective components and
are not modified by this project's license.
