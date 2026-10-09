# cdj-flacwards

FLAC playback for the Pioneer DJ CDJ-900.

The CDJ-900 (2009) plays MP3, AAC, WAV and AIFF files from USB devices. rekordbox can export FLAC tracks to a USB device. The CDJ-900 shows these tracks, but it cannot load them.

This project adds a patch to the CDJ-900 MAIN firmware 4.32. The patch decodes FLAC on the CPU of the player. Then the patch sends the decoded audio to the WAV playback of the player.

> **Warning:** Do not install this patch if you cannot accept the risk of an unusable player. A firmware update can make a player unusable. A modified firmware can also void the warranty of the player. Read "Safety" before you install the patch. You use this project at your own risk. The project gives no warranty.

This project has no affiliation with AlphaTheta Corporation, Pioneer DJ or Pioneer Corporation. These companies do not endorse or support this project. They did not approve this modification. "Pioneer DJ", "Pioneer", "CDJ" and "rekordbox" are trademarks of their owners. This document uses these names only to identify the hardware and the file formats.

## Quickstart

We can't legally provide pre-patched firmware, but you can patch it yourself using the web patcher:
https://zdavison.github.io/cdj-flacwards/

## Status

We tested the patch on one CDJ-900 with firmware 4.32.

| Feature | Supported | Tested on the player | Automated tests |
|---|:-:|:-:|:-:|
| **Playback of FLAC tracks** | | | |
| Long play | ✅ | ✅ | — |
| Cue | ✅ | ✅ | — |
| Loops | ✅ | ✅ | — |
| Reverse play | ✅ | ✅ | ✅ |
| Slip mode | ✅ | ✅ | — |
| High tempo settings | ✅ | ✅ | — |
| Search and jumps, forward and backward | ✅ | ✅ | ✅ |
| Hot cues | ✅ | ❌ | — |
| **FLAC files** | | | |
| 44.1 kHz or 48 kHz, 16-bit or 24-bit, stereo | ✅ | ✅ | ✅ |
| Files without a seek table | ✅ | ✅ | ✅ |
| Files with embedded artwork | ✅ | ✅ | ✅ |
| Damaged files: silence for the damaged frames, then playback continues | ✅ | ❔ | ✅ |
| Mono files | ❌ | — | ✅ |
| More than two channels | ❌ | — | ❌ |
| Other sample rates, for example 96 kHz | ❌ | — | ✅ |
| Other bit depths, for example 8-bit | ❌ | — | ✅ |
| Files with an ID3 tag before the FLAC data | ❌ | — | ✅ |
| **Sources** | | | |
| rekordbox USB export | ✅ | ✅ | — |
| Folder browsing without a rekordbox database | ❌ | ❌ | — |
| PRO DJ LINK (track sharing with other players) | ❌ | ❌ | — |
| **Players** | | | |
| CDJ-900 with firmware 4.32 | ✅ | ✅ | — |
| CDJ-900 with other firmware versions | ❌ | — | ✅ |
| CDJ-2000, CDJ-2000NXS, CDJ-900NXS | ❌ | — | — |

The player rejects an unsupported FLAC file with its normal load error. The patch and the patcher page refuse firmware versions other than 4.32.

## How it works

The patch shows each FLAC file to the firmware as a WAV file. The virtual WAV file starts with a 44-byte RIFF header. The decoded PCM audio comes after the header.

The patch redirects a small number of function pointers in the firmware. As a result, the firmware calls the code of the patch instead of its own file functions. If the file is a FLAC file, the patch gives the virtual WAV view. All other files go to the stock file functions without change.

The patch also limits how far the player reads ahead in a FLAC track. Thus the FLAC decoder does not take CPU time from the user interface.

The FLAC decoder is [dr_flac](https://github.com/mackron/dr_libs). The patch puts all new code into unused space in the firmware image. The patch changes no stock instruction.

`NOTES.md` contains the firmware analysis, with addresses and measurements.

## This repository holds no firmware

This repository contains no AlphaTheta or Pioneer code. It contains only our own code, tools and documentation. It contains no firmware, no patched firmware and no decompiler output. The project does not distribute patched firmware, also not as a release download.

To make an update file, you need the official package of the CDJ-900 firmware 4.32 (`CDJ-900v432.zip`). Download the package from the [AlphaTheta support site](https://support.alphatheta.com/en-US/articles/21708238994585). The tools check the SHA-256 of the package. Then the tools patch your own copy on your computer.

## Patch in the browser

The [patcher page](https://zdavison.github.io/cdj-flacwards/) makes two update files from your own `CDJ-900v432.zip`:

- the patched update file
- the rollback update file

The page makes the files in your browser. The page does not upload your file. For this method, you do not need the build tools. The page gives the same files as `tools/build_patch.py release` and `tools/build_patch.py rollback`.

## Build

You need these tools:

- Linux
- Python 3
- `sh-elf-binutils`
- the GCC build dependencies (GMP, MPFR, MPC)
- `ffmpeg` and `flac`, for the tests
- `qemu-sh4-static`, for the SH-4 calling-convention test (optional)

Do these steps:

1. Build the SH-4 compiler. The script puts the compiler into `toolchain/`:

   ```sh
   tools/build-toolchain.sh
   ```

2. Build the code and run the tests:

   ```sh
   make
   make test
   make check-abi
   ```

3. Put the official firmware package in the root of the repository.
4. Unpack the firmware:

   ```sh
   python3 tools/unpack_main.py CDJ-900v432.zip
   ```

5. Make the update file:

   ```sh
   python3 tools/build_patch.py release
   ```

   The result is `out/release/C900MAIN.UPD`.

6. Do not share `out/release/C900MAIN.UPD`. The file contains Pioneer DJ firmware.

## Install

1. Make the rollback stick. See "Safety".
2. Get the patched `C900MAIN.UPD` from the patcher page or from `out/release/`.
3. Format a USB stick as FAT32. Do not use exFAT or a Ventoy stick, because the CDJ-900 does not see the files on these sticks.
4. Copy only `C900MAIN.UPD` to the root of the stick.
5. Turn off the player.
6. Put the stick in the USB slot.
7. Hold [RELOOP/EXIT] and [USB], and turn on the power. The player updates its MAIN firmware.
8. Wait until the update is complete.

   > **Caution:** Do not turn off the player before the update is complete. An interrupted update can make the player unusable.

9. Turn the player off and on.
10. Check the version. The patched firmware reports a version above 4.32, for example 4.44.

## Safety

### Make the rollback stick

Make the rollback stick before you install the patch:

1. Get the rollback file. Use the patcher page, or run `python3 tools/build_patch.py rollback`. The script writes `out/rollback/C900MAIN.UPD`.
2. Format a second USB stick as FAT32.
3. Copy the rollback `C900MAIN.UPD` to the root of the stick.
4. Keep the stick.

The rollback file is the official file. Only the version in its header changes, to 4.99. The updater accepts only a version above the installed version. Thus the updater accepts the rollback file over any patched version.

### Go back to the stock firmware

Install the rollback stick with the same procedure as an update. See "Install", steps 5 to 9. After the update, the player reports version 4.32.

### Limits of the rollback

The patch does not run during start-up. The patch runs only when a track loads or plays. Thus, if a FLAC track causes a problem, the player still starts. The updater also still works.

> **Warning:** Do not install the patch if you cannot accept the risk of a player that does not start. If the player does not start, the rollback stick cannot recover the player. Then only a hardware programmer for the main flash chip can recover the player.

## Development

- `tools/decompile.sh CDJ-900v432.zip` makes a local Ghidra project. The script also writes C code for every firmware function to `build/decomp/`. The script needs Ghidra 12.1.
- Keep the output of `tools/decompile.sh` local. Do not commit it. The output comes from Pioneer DJ firmware.
- `tools/build-emulator.sh` builds QEMU with the board from [cdj2000-emulator](https://github.com/cdj2k-revival/cdj2000-emulator). This board also runs the CDJ-900 MAIN firmware. `tools/emu.sh` starts the emulator.
- `tools/build_patch.py` has debug targets: `flac`, and `deck1` to `deck7`. These targets add console commands (`N,VW`, `N,VP`, `N,TK`). They also write a statistics file to the USB stick. The docstrings in `tools/build_patch.py` give the details.

## License

The code of this project uses the MIT license (see `LICENSE`). Some files use other licenses. `THIRD_PARTY.md` lists these files and their licenses.

## Thanks

This project would not be possible without the excellent work of:
- [CDJ2000-emulator](https://github.com/cdj2k-revival/cdj2000-emulator): Without this, it would have been impossible to iterate quickly.
- [dr_libs](https://github.com/mackron/dr_libs): Very useful to have a lightweight FLAC decoder already usable.
