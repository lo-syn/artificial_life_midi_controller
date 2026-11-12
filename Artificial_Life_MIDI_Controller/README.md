# Artificial Life MIDI Controller

Firmware for the Raspberry Pi Pico (RP2040). The Pico SDK build produces a
UF2 file that can be copied to the board without installing the SDK on the
computer used to flash it.

## Build

1. Install Visual Studio Code and the **Raspberry Pi Pico** extension.
2. Open this `Artificial_Life_MIDI_Controller` folder in VS Code.
3. Let the extension configure the project, then build the `main` target.
   The project is configured for the Raspberry Pi Pico (`PICO_BOARD=pico`).
4. Find the firmware at `build/main.uf2`.

The extension installs/configures the Pico SDK and ARM toolchain. A separate
copy of the SDK does not need to be added to this repository.

## Flash the Pico

1. Disconnect the Pico from USB.
2. Hold **BOOTSEL** while connecting it to the computer. It should appear as
   the `RPI-RP2` drive.
3. Copy `build/main.uf2` to that drive. The Pico reboots and starts the
   firmware.

To share a ready-to-flash build, attach `main.uf2` from the `build` directory
to a GitHub Release. The `build` directory is ignored by Git, so generated
build files are not included in source commits.

The VS Code **Run Project** task uses picotool to load and execute the ELF over
USB; use the UF2/BOOTSEL steps above for a straightforward flash that persists
after power is disconnected.
