# cdj-flacwards

FLAC playback for the Pioneer DJ CDJ-900.

The CDJ-900 (2009) plays MP3, AAC, WAV and AIFF from USB devices. rekordbox exports FLAC tracks to a USB device, and the CDJ-900 shows them, but it cannot load them. This project adds a patch to the CDJ-900 MAIN firmware 4.32. The patch decodes FLAC on the player's own CPU and gives the decoded audio to the player's own WAV playback. Cue points, loops, reverse play, slip mode, the waveform and tempo control stay the stock functions.

> **Warning:** This patch changes the firmware of your player. A firmware update can make a player unusable, and a modified firmware can void its warranty. Read "Safety" before you install anything. You use this project at your own risk. There is no warranty.

This project is not affiliated with, endorsed by or supported by AlphaTheta Corporation, Pioneer DJ or Pioneer Corporation. It is in no way a sanctioned modification. "Pioneer DJ", "Pioneer", "CDJ" and "rekordbox" are trademarks of their owners. They are used here only to name the hardware and the file formats that this project works with.

## Status

- Tested on one CDJ-900 with firmware 4.32. Long play, cue, loops, reverse play, slip mode and high tempo settings work.
- Supported FLAC files: 44.1 kHz and 48 kHz, 16-bit and 24-bit, stereo. The player rejects other FLAC files with its normal load error.
- Tracks load from a rekordbox USB export. Folder browsing without a rekordbox database is not supported yet.
- PRO DJ LINK (sharing tracks with other players) is not tested. Do not expect FLAC tracks to load over LINK.
- Other players (CDJ-2000, CDJ-2000NXS, CDJ-900NXS) are not supported yet. See `docs/release-tasks.md`.

## How it works

The patch shows each FLAC file to the firmware as a WAV file: a 44-byte RIFF header, then the decoded PCM. The firmware calls our code instead of its file functions through a small number of redirected function pointers. A FLAC file gets the virtual WAV view. All other files go to the stock functions without change. The patch also limits how far the player reads ahead for FLAC tracks, so that decoding does not take the CPU from the user interface.

The FLAC decoder is [dr_flac](https://github.com/mackron/dr_libs). All new code goes into unused space in the firmware image. No stock instruction changes.

- `docs/superpowers/specs/2026-10-08-flac-virtual-wav-design.md`: the design.
- `NOTES.md`: the firmware analysis, with addresses and measurements.
- `STATUS.md`: the project status and how to continue the work.

## This repository holds no firmware

This repository contains no AlphaTheta or Pioneer code. It holds only our own code, tools and documentation: no firmware, no patched firmware and no decompiler output. The project does not distribute patched firmware, also not as a release download. To build an update file, download the official CDJ-900 firmware 4.32 package (`CDJ-900v432.zip`) from the [AlphaTheta support site](https://support.alphatheta.com/en-US/articles/21708238994585). The tools check its SHA-256 and patch your own copy on your computer. See `docs/legal-review.md`.

## Build

You need Linux, Python 3, `sh-elf-binutils`, and the GCC build dependencies (GMP, MPFR, MPC). For the tests you also need `ffmpeg` and `flac`. The SH-4 calling-convention test needs `qemu-sh4-static` (optional).

1. Build the SH-4 compiler. It goes into `toolchain/`:

   ```sh
   tools/build-toolchain.sh
   ```

2. Build the code and run the tests:

   ```sh
   make
   make test
   make check-abi
   ```

3. Put the official firmware package in the repository root, and unpack it:

   ```sh
   python3 tools/unpack_main.py CDJ-900v432.zip
   ```

4. Make the update file:

   ```sh
   python3 tools/build_patch.py release
   ```

   The result is `out/release/C900MAIN.UPD`. Do not share this file: it holds Pioneer DJ's firmware.

## Install

1. Format a USB stick as FAT32. Do not use exFAT or a Ventoy stick: the CDJ-900 does not see their files.
2. Copy only `out/release/C900MAIN.UPD` to the root of the stick.
3. Turn off the player. Put the stick in the USB slot.
4. Hold [RELOOP/EXIT] and [USB], and turn on the power. The player updates its MAIN firmware.
5. When the update is complete, turn the player off and on. Check the version: the patched build reports a version above 4.32 (for example 4.44).

## Safety

- **Make the rollback stick before you install.** Run `python3 tools/build_patch.py rollback`. It makes `out/rollback/C900MAIN.UPD`: the stock 4.32 firmware with header version 4.99, which the updater accepts over any patched version. Copy it to a second FAT32 stick.
- **To go back to the stock firmware,** install the rollback stick in the same way as an update. The player then reports 4.32.
- The patch does not run during start-up. It runs only when a track loads or plays. So if a FLAC track causes a problem, the player still starts, and the updater still works.
- If the player does not start, the rollback stick cannot help. Only a hardware programmer for the main flash chip can then recover it. If you cannot accept that risk, do not install the patch.
- Turn off the player only when the update is complete.

## Development

- `tools/decompile.sh CDJ-900v432.zip` makes a local Ghidra project and C code for every firmware function in `build/decomp/`. It needs Ghidra 12.1. Its output is derived from Pioneer DJ firmware: keep it local and never commit it.
- `tools/build-emulator.sh` builds QEMU with the [cdj2000-emulator](https://github.com/cdj2k-revival/cdj2000-emulator) board, which also runs the CDJ-900 MAIN firmware. `tools/emu.sh` starts it.
- `tools/build_patch.py` has debug targets (`flac`, `deck1` to `deck7`) with console commands (`N,VW`, `N,VP`, `N,TK`) and a statistics file on the USB stick. See the docstrings.

## License

MIT, see `LICENSE`. Third-party code and its licenses are listed in `THIRD_PARTY.md`.
