# CDJ-900 FLAC notes

Firmware: `CDJ-900v432.zip` (MAIN Ver4.32, 2014-03-25). Do not commit `firmware/`.

## Reproducing the local analysis

`tools/decompile.sh CDJ-900v432.zip` (or `C900MAIN.UPD`) makes everything that the analysis in these notes uses. It needs Python 3 and Ghidra 12.1 (`GHIDRA_HOME`, default `/opt/ghidra`). Its output stays local: `firmware/`, `ghidra-proj/` and `build/decomp/` are in `.gitignore`, because they are derived from Pioneer firmware.

1. `tools/unpack_main.py` checks the SHA-256 of `C900MAIN.UPD` 4.32, extracts the update files, and writes `firmware/unpacked/main-firmware.bin` and `main-unpacked.bin` (byte-identical to the files that the first analysis used).
2. Headless Ghidra import: Raw Binary loader, `SuperH4:LE:32:default`, base `0x04000000`. `SeedFunctions.java` runs before the auto-analysis.
3. `DecompileAll.java` writes C for every function, one file per 64 KiB, with the strings that each function uses. `clean.py --file` removes the FPSCR bookkeeping.

Check on 2026-10-09 (8 minutes on this PC): 5,467 functions. Compared with the first `build/decomp/` (made by hand on 2026-10-08), 5,355 of the 5,455 common functions are identical. The other 100 differ only in FPSCR bookkeeping lines, which the first, unsaved cleaning step treated a little differently. The first project also has 15 more functions, which `decomp.sh` created during single-address queries.



- Each `.UPD` file has a 32-byte ASCII header, Motorola S-records and a 2-byte trailer.
- The trailer is CRC-16/XMODEM over all bytes except the last 2, stored little-endian.
- The MAIN header byte `0x1f` is the flag. `'0'` writes from `0x40000`. Never use `'1'`.
- The MAIN flash layout is: boot code at `0x0`, LZSS loader at `0x10000`, LZSS application at `0x40000`.
- `cdj2000-emulator`'s `tools.cdj_gui.main_unpack` unpacks `C900MAIN.UPD` with no changes.
- The packed application ends at `0x28a4f7`. The updater erases up to `0x3dffff`. Free: 1366 KiB.

## Application (`firmware/unpacked/main-unpacked.bin`)

- The application is SH-4A, little-endian, 0x3c0000 bytes.
- Data pointers use `0xa4000000 + offset`. Code pointers use `0x04000000 + offset`.
- `tools/sh4.py` finds literal-pool cross-references and disassembles with Capstone 6.

## DSP program

- The program is TI C67x+ code for the Aureus DA710. It starts with `_c_int00` at offset `0x1010`.
- A second block starts at `0xe3d0`. The offsets are the same as in the CDJ-2000.
- The MPEG audio synthesis window table is at `0x5c80`, so the MP3 decoder runs on the DSP.

## File types

The extension check starts at `0xa4164a2a`. It uses the table at `0xa40633d0`.

| Extension | Code |
|---|---|
| MP3 | 1 |
| AAC | 2 |
| MP4 | 3 |
| M4A | 4 |
| WAV | 11 |
| AIF, AIFF | 12 |
| JPG, JPEG | -56 |
| other | -1 |

- The only caller is the FAT directory scan at about `0xa416393e`. The scan skips code -1 and stops at 10000 entries.
- About 23 functions compare a value with both 11 and 12. These are the WAV/AIFF branches. A FLAC type needs to join some of them.
- The header parsers load `ftyp` (`0xa41e41be`), `RIFF` (`0xa41e49c0`), and `AIFF` and `ID3 ` (`0xa41e51fa`).

## DSP send tasks

The task table starts at `0xa406690c`. It holds uITRON task records with entry, priority, stack size, stack and name.

- `tsk_DJcontTxDspPCM` starts at `0x041c33c0`. `tsk_DJcontTxDspDEC` starts at `0x041c38e4`.
- Both tasks write the command word at DSP window `0xac0c8140`. Both use the staging halves `0xac0c81e0` and `0xac0cbea0`.
- DEC copies 8 KiB compressed blocks, at `+0x81e0..+0xa1e0` and `+0xbea0..+0xdea0`.
- PCM reads up to 12 units of raw file data straight into a staging half, through `0xa41c3d86`. The unit size comes from fields +18 and +20 of the message. WAV data is already PCM, so the CPU does not decode it.

## Playback pipeline (traced in Ghidra)

These are the playback tasks, from the task table:

| Task | Entry | Role |
|---|---|---|
| `tsk_DJcontInBufMngSub` | `0x041b1aa4` | Runs the header parser for the track type. |
| `tsk_DJcontInBufMng` | `0x041ac090` | Sets up the stream and picks the route. |
| `tsk_DJcontInBufFile` | `0x041a7770` | Reads the file into SDRAM buffers. |
| `tsk_DJcontTxDspPCM` / `DEC` | `0x041c33c0` / `0x041c38e4` | DMA from SDRAM to the DSP window. |

1. **Parser dispatch.** `FUN_041b1b40` picks the parser from the type code:
   - 11 WAV → `FUN_041b1c7c`
   - 12 AIFF → `FUN_041b1de8`
   - 1 MP3 → `FUN_041b1f2c`
   - 2, 3, 4 → `FUN_041b1fe2`
2. **Track info fields.** The parsers fill the track info struct `T`:
   - `T+0x0c` route mode: 1 = PCM (WAV and AIFF), 2 = MP3. AAC uses the DEC route as well.
   - `T+0x10` duration.
   - `T+0x14`, `T+0x18`, `T+0x1c` data offset and data length in the file.
   - `T+0x12c` bytes per position unit.
   - `T+0x130` big-endian flag: WAV 0, AIFF 1, little-endian AIFF-C 0. This answers the AIFF byte-order question.
   - `T+0x131` channels, `T+0x132` sample-rate code, `T+0x133` bytes per sample.
   - The WAV and AIFF parsers read a common header layout: +0x42 bits, +0x49 channels << 2, +0x4a rate index << 4.
3. **Route.** `FUN_041b08da` reads `T+0x0c`. Mode 1 selects the PCM task struct `0x04835ab8`, with SDRAM buffers `0xf000` bytes apart. Modes 2 and 3 select the DEC task struct `0x04835ae0`. The caller is `FUN_041af3a8`.
4. **File task.** `tsk_DJcontInBufFile` handles 4 commands. When stream context `+0x40 == 1`, it uses the PCM handlers:
   - 1 → `FUN_041a7a10` (probably load and first fill)
   - 2 → `FUN_041a7fb0` (probably continued fill)
   - 3, 4 → `FUN_041a8534` (probably search forward and back)
5. **PCM seek.** The PCM handlers call `seek(stream, position * T[0x12c], SEEK_SET)`, then `read`. Seeking is plain byte arithmetic on the PCM data.
6. **Stream object.**
   - Stream API (`DJcontADS*` log strings): `FUN_0419eaa4` ftell, `FUN_0419eb06` **feof** (an earlier note said read, which was wrong), `FUN_0419ebb6` fseek(obj, off, whence, opts), `FUN_0419ed9c` fread(obj, buf, len, &got, opts).
   - The object fields are `[0]` FAT handle, `[1]` data start, `[2]` data length, `[4]` and `[5]` error codes.
   - A seek checks `off <= length`, then seeks the FAT file to `start + off`.
   - These functions have 47 callers. The MP3 path uses them too.

## File and stream layer (mapped 2026-10-08, read-only subagent)

- **Stream object.** A 7-word stack local in the `DJcontInBufMng` task `FUN_041af3a8`: `[0]` file handle, `[1]` data start, `[2]` data length, `[3]` tail, `[4]`/`[5]` error codes, `[6]` written by `0x041a900c`. It is set from `T+0x14..0x1c`, and `FUN_041b08da` stores its pointer at file context `+0x30`.
- **Open and close for playback.** `FUN_041b0376` → `FUN_042e8ea8` resolves an 8-byte track ID (`FUN_042e86fa`), then calls vfs open with mode `"r"` (`0xa40c0840`). Close: `FUN_041b03ce` → vfs close. Argument registers are partly a guess.
- **vfs API** (`042e.c`). The last argument is a 5-word options struct. NULL gives the default `0xa409fcc4`. Playback uses `{0x110003, 0x1b58, 0, 0, 0}` at `0xa409fd14`.
  - `vfs_fopen 0x042e55a4(wchar *path, char *mode, opts)`. The path is UTF-16, for example `L"C:/..."`, with `/` separators. USB is drive `C`.
  - `vfs_fopen_entry 0x042e5854(drive, dirCluster, index, mode, opts)`, `vfs_fclose 0x042e59de(fh, opts)`
  - `vfs_fread 0x042e5c1a(buf, size, n, fh, opts)`, `vfs_fseek 0x042e5e1e(fh, off, whence, opts)`, `vfs_ftell 0x042e5ea8(fh, opts)`, `vfs_feof 0x042e5f3a(fh, opts)`, `vfs_filelen 0x042e65a4(fh, opts)`
  - `vfs_opendir 0x042e673c`, `readdir 0x042e691e`, `closedir 0x042e68be`. The `ex_*` wrappers at `0x041e2938`.. add the drive. `FUN_04190b86` lists a directory and opens files.
- **Parser inputs.** `tsk_DJcontInBufMngSub` (context `0x0483543c`) calls `FUN_041b1b40(ctx, msg)`, which switches on `T+8`. The WAV branch `FUN_041b1c7c(P = ctx+0x54, info = ctx+0xec, T)` calls the real parser `FUN_041e49b2(P, info)`, then fills `T` (`+0xc` = 1, `+0x14` start, `+0x18` length, `+0x1c` tail, rate and format fields).
  - `P` is a separate header stream, set up by `FUN_041b046c`: `P[1]` file handle, `P[2..6]` opts, `P[7]` a 2 KiB buffer at `ctx+0x538`. Helpers: `FUN_041e3340` file length, `FUN_041e324a` seek, `FUN_041e328c` read. These call the vfs layer.
  - Parser result 0 = success, -2 = bad format, -3 = read error. The dispatch replies 0, -0x12 or -0x15.
- **PCM file task pool slots** (stream object at `*(ctx+0x30)`): fseek `0xa41a7bf8`, `0xa41a7e08`, `0xa41a82fc`, `0xa41a898c`, `0xa41a8c58`, `0xa41a8e8c`; feof `0xa41a7bfc`, `0xa41a8018`, `0xa41a8c5c`. All reads go through `FUN_041a8fac`: fread slot `0xa41a90dc`, feof slot `0xa41a90e0`.
  - Image-wide: fread has 5 pool slots, fseek 16, feof 6, ftell 7.
- **Console.** No command opens files. The table is at `0xa405c310` (slot `0xa4101dc4`), 37 entries before `ZZ`. A handler is `void h(void)` and reads its arguments from `0x045a01a8[0..]`.

## Type codes are rekordbox file types

The firmware's type codes are the `file_type` values of the rekordbox `export.pdb` track rows (see rekordcrate `src/util.rs`, `enum FileType`): 1 MP3, 4 M4A, **5 FLAC**, 11 WAV, 12 AIFF.

- **Deck test, 2026-10-08:** on stock firmware, FLAC tracks from a rekordbox export appear in the browser. Loading one gives an error.
- **Likely cause:** the database gives type 5. `FUN_041b1b40` has no branch for 5, so it returns `0xffffffeb` (-21).
- **Consequence:** FLAC uses type **5**, not a new code. For database browsing, no extension-table change is needed. For folder browsing without a database, `FLAC` → 5 still has to go into the extension table.

## FLAC hooks (built 2026-10-08)

Spec: `docs/superpowers/specs/2026-10-08-flac-virtual-wav-design.md`. Plan: `docs/superpowers/plans/2026-10-08-flac-virtual-wav.md`.

A FLAC track becomes a WAV file at the file-handle level:

- `src/vwav.c` shows one FLAC file as a WAV file: a 44-byte RIFF header (tag 1), then decoded PCM.
- `src/vfs_hook.c` holds 4 slots. The open wrapper registers a handle if `vwav_open` accepts the file. All other handles go to the stock functions without change.
- The type hook wraps the message send in `FUN_041b046c` and in its copy `FUN_041b0648`. If the message is `{1, T}`, `T+8` is 5 and the handle from the latest `hook_open` is registered, it sets `T+8` to 11. `FUN_041b1b40` is called by `bsr`, so it cannot be wrapped. The two send functions get different contexts, so the hook does not read a fixed context address.
- **Open ABI:** `FUN_042e8ea8(r4, track ID)`. The caller at `0x041b038c` copies the 8-byte track ID to `@r15` and `@(4,r15)`. It is a struct by value on the stack (Renesas convention), not 3 register arguments as the decompile shows. `tests/abi` checks `hook_open` against this call sequence in `qemu-sh4-static`.
- If a FLAC file cannot register (no free slot, read error), `hook_open` closes it and returns 0, so the load fails. Reason: the header cache in `FUN_041b046c` can hold type 11 for the track, and then the stock PCM path would play the raw FLAC bytes.
- A fresh handle that is still in the table frees its old slot first. Only one of the 37 fclose pool words is wrapped.
- The firmware opens a track once. The header stream `P` and the stream object use the same handle.
- The stock WAV parser accepts only 44100 Hz and 48000 Hz, 16 or 24 bits, 2 channels or fewer, and tag 1. The `data` chunk must run to the end of the file.
- vfs errno: a short read is accepted only with errno -14 (EOF). Setter `0x041f26d0`, getter `0x041f26ca`.

Pool words that `tools/build_patch.py flac` changes (all in playback code):

| Pool word | Stock target | Wrapper | Caller |
|---|---|---|---|
| `a41b0460` | `042e8ea8` | `hook_open` | `FUN_041b0376`, playback open |
| `a41b0468` | fclose | `hook_fclose` | `FUN_041b0402` |
| `a41e3428`, `a419efcc` | fseek | `hook_fseek` | header seek, stream seek |
| `a41e3438`, `a419effc` | fread | `hook_fread` | header read, stream read |
| `a41e343c`, `a41e51b8`, `a419ec88`, `a419eff4` | ftell | `hook_ftell` | header helper, WAV parser, stream API |
| `a41e3440` | filelen | `hook_filelen` | `FUN_041e3340`, `FUN_041e33c8` |
| `a41e3444`, `a41e4f30`, `a41e51c8`, `a419ec94` | feof | `hook_feof` | header helper, WAV parser, stream API |
| `a41b0738`, `a41b0a1c` | `042f01e8` (message send) | `hook_send` | `FUN_041b046c`, `FUN_041b0648` |

The header helpers and the stream API also serve the AIFF, MP3, AAC and MP4 code. That code gets the stock functions, because its handles are not registered.

Task stacks: `tsk_DJcontInBufMng` and `tsk_DJcontInBufMngSub` have `0xc00` bytes, `tsk_DJcontInBufFile` has `0x800`. Each slot has a 16 KiB stack, and `call_with_stack` switches to it.

**Emulator result (2026-10-08):** version 4.35 boots. The console command `1,VW` reads `C:/T16.FLA` through the wrappers. The CRC-32 values of a sequential read and of 300 jumps are equal to the PC values (`host_vwav check`). The 16-bit file takes 16 s and the 24-bit file 32 s in the emulator. After the review fixes, the values are the same. The emulator timing is not the deck timing.

### Pre-deck checks (2026-10-09)

1. **No hook runs at boot.** A gdb probe on the 8 hook entries, set before the first instruction (QEMU `-S -gdb`), counted 0 calls during boot, stick mount and 150 s idle. In the same run, `1,VW` gave 1 call each on `hook_fclose` and `hook_filelen` (positive control).
2. **Stock parsers through the hooks.** The console command `N,VP` (`src/vp_cmd.c`) runs the stock WAV, AIFF or MP3 header parser branch with the header stream set up as in `FUN_041b046c` (buffer `0x8008` bytes, playback opts `{0x110003, 0x1b58, 0, 0, 0}`), and prints `T` and a CRC-32 of the data region.
   - A FLAC file through the hooks gives the same line as the WAV file with the same audio (16-bit and 24-bit), and the data CRC equals the PC value.
   - WAV, AIFF and MP3 give the same lines on `out/flac` and on `out/flacref` (the same blob without hooks). So the hooks do not change these formats.
   - MP3 gives -3 on both builds: the command does not set up what the MP3 parser needs. This is a test limit, not a regression.
   - The stream API (`0419.c`) was not called: its signature is not clear from the code.
3. **The updater still works on 4.35.** In the emulator, with RELOOP and USB held at power-on (`CDJ_PANEL_FRAME=00000000000000000000000000000000040000020000`) and only the rollback `C900MAIN.UPD` on the stick, the updater printed `*** Update END ! ***`. The flash then booted as 4.32, and the unpacked application was the stock application byte for byte.
   - CDJ-900 update mode: payload byte 16 bit 2 (RELOOP) and byte 19 bit 1 (USB) at the first panel frame (`0x04292b80`). Updater task `UpDtae_TASK` at `0x042ddb1c`.
   - Save the flash with the monitor command `pmemsave 0 0x400000 FILE`. Use a short relative path, because the monitor line editor breaks long paths.
4. **Stack margin.** After `1,VW`, `2,VW`, `1,VP` and `2,VP`, the deepest use of a slot stack was 5,772 of 16,384 bytes. (The stacks are in `.bss`, so the lowest non-zero byte marks the deepest point.)
5. **CPU cost (instruction counts).** `tools/qemu-plugins/blobcount.c` counts the instructions in our blob between marker addresses (build: `gcc -shared -fPIC -I build/qemu/include/plugins $(pkg-config --cflags glib-2.0) tools/qemu-plugins/blobcount.c -o build/blobcount.so`, run with `-plugin build/blobcount.so,lo=0x04347400,hi=0x043bfffc,mark=ADDR,...`). Results for 20 s test files:

   | File | Open | Decode, per second of audio | Each jump (seek and a read of up to 5,000 bytes) |
   |---|---|---|---|
   | 16-bit 44.1 kHz | 0.15 M | 16.1 M | 14.1 M |
   | 24-bit 48 kHz | 0.15 M | 22.6 M | 18.0 M |
   | 16-bit 48 kHz, no SEEKTABLE | 0.15 M | 17.7 M | 24.1 M |

   - The counts do not include the stock FAT and USB reads. A jump reads the FLAC data from the seek point forward, so on the deck the USB time can be larger than the decode time.
   - The CPU clock of the deck is not in these notes, so these counts are not times yet.
6. **Deck builds.** `tools/build_patch.py deck1` gives version 4.36: all hooks in place, but the data word `vh_flac_enabled` is 0, so every file goes to the stock path. `deck2` gives version 4.37 with FLAC on. Both boot in the emulator. The rollback file (4.99) is above both versions.

## Read-ahead policy (found 2026-10-09)

- The large playback buffer is in the DSP, not in CPU SDRAM. The CPU stages 61,440-byte halves at `0xa450baf0`; `tsk_DJcontTxDspPCM` copies them to the DSP.
- The DSP status block at `0xac0c7cc8`: `[1]` units behind the play position, `[2]` units ahead. One unit is `T+0x12c` bytes, 1/75 s.
- `tsk_DJcontInBufMng` calls the policy `FUN_041ac360(Mng ctx)` after each file-task reply, in its idle poll (at most 100 ms apart) and after a backward fill (pool words `a41adf68`, `a41ae2c0`, `a41af38c`; the first call after a load is a `bsr` at `0x041ace34`). Result: 0 idle, 1 fill forward (40 units), 2 fill backward (76 units for PCM), 3 other (uncertain).
- Thresholds are code immediates: urgent forward fill below 135 units ahead (1.8 s); forward fill below 1875 (25 s); 1875–2250 continues the current direction; backward fill below 135, or below 1200 (16 s). One branch (`FUN_041b43f4(0x4835bec)` < 2) only balances ahead against behind, with no fixed cap (uncertain); this may explain the 78 s and 114 s refills seen on the deck.
- `hook_fill_policy` (4.42) changes "fill forward" to "idle" for a FLAC stream with 1,500 units (20 s) or more ahead. A boot probe found no call of the policy during boot, stick mount and 150 s idle.
- Not known: what the DSP does at 0 units ahead (wait, mute or stop).

## Scope (decided 2026-10-08)

Support only the common formats: 44.1 kHz and 48 kHz, 16-bit and 24-bit, stereo.
Pioneer documents WAV/AIFF on the CDJ-900 as up to 48 kHz and 24-bit.
Add mono FLAC only after a mono WAV plays correctly on the deck.
The parser rejects any other FLAC file, so the deck treats it like an unsupported WAV file.
Do not resample and do not reduce bit depth.

The rate table at `0xa4068f9c` lists 12 rates from 96000 down to 8000. The WAV and AIFF parsers share this table.
The table alone does not prove that the deck plays 88.2/96 kHz, so the scope above does not depend on it.

## SDRAM map

The SDRAM is 64 MiB at physical `0x04000000`–`0x08000000`. The reset code sets `r15 = 0xa8000000`, as on the CDJ-2000.
The start-up code at `0x04000800` reads a linker table at `0xa409ccb8`. The firmware writes its own marker strings at the area ends.

| Range | Size | Use | Source |
|---|---|---|---|
| `0x04000000`–`0x043471c8` | 3.27 MiB | Loaded image: DSP blob, code, rodata, `.data` source | linker table |
| `0x043471c8`–`0x043bfffc` | 473 KiB | `0xff` padding inside the loaded image | image bytes |
| `0x043bfffc`–`0x043c0000` | 4 B | Image checksum: 32-bit sum of all earlier big-endian words | tested |
| `0x043c0000`–`0x04500000` | 1.25 MiB | No literal-pool references found | literal scan |
| `0x04500000`–about `0x0456b000` | | Buffer (GUI update and other users) | literal scan |
| `0x045a0000`–`0x05766578` | 17.8 MiB | `.bss`, cleared at start | linker table |
| `0x05766578`–`0x060e6578` | 9.5 MiB | `OS_MPLMEM` (RTOS memory pools, `edbmalloc`) | fill loop at `0x042649dc` |
| `0x060e6578`–`0x07a00000` | 25 MiB | Unknown. No aligned literal pointers. Could hold buffers built from computed addresses. | |
| `0x07a00000`–`0x07de0000` | | Boot-time copy of the packed image and a stub | CDJ-2000 notes |
| `0x07d66578`– | | `OS_STKMEM` (interrupt stack), then `OS_STACK` | marker strings |
| `0x07d66f34`–`0x07d89268` | 137 KiB | `.data` | linker table |
| top `0x08000000` | | Reset stack | reset code |

## Where the FLAC code goes

1. **Code and static buffers go in the `0xff` padding, `0x043471c8`–`0x043bfffc`.**
   - The boot loader already decompresses this area into RAM with the rest of the image.
   - No code references this area.
   - `0xff` runs compress well. New code makes the packed image grow by less than its own size, and the flash has 1366 KiB free.
   - Budget: about 64 KiB code, plus static decoder contexts.
2. **Hooks: redirect literal-pool words.** An SH-4 call is `mov.l @(disp,PC),Rn` then `jsr @Rn`. A changed pool word redirects the call, and no instruction changes. For example, the pool word at `0x041a7bf8` holds `seek = 0x0419ebb6`.
3. **Checksums to fix after each patch:**
   - the 32-bit image sum at `0x043bfffc`
   - the 16-bit sum of the packed region at flash `0x40000`
   - the CRC-16/XMODEM trailer of the `.UPD` file
4. **Check on the deck:** the first test patch confirms that the padding stays `0xff` while the deck runs.

## Toolchain

- **Binutils:** `sh-elf-binutils` from the AUR. It is installed.
- **Compiler:** `tools/build-toolchain.sh` builds a C-only `sh-elf-gcc` 15.2 with `libgcc` into `toolchain/`.
  - The AUR packages `sh-elf-gcc` and `sh-elf-newlib` cannot build, because each one needs the other first.
  - GCC 16 hosts need `-fno-char8_t` for GCC 15's `libcody`. Do not force `-std`, because `libcody` checks for exactly C++11.
- **Flags:** `-m4-nofpu -ml -mrenesas -mdiv=call-div1 -Os -ffreestanding -fno-builtin -nostdlib -Isrc/libc`.
  - Arguments go in `r4`–`r7` and the return value in `r0`, the same as the firmware.
  - `-m4-nofpu`: plain `-m4` code sets FPSCR.PR (double precision) before each call. The firmware runs the FPU in single-precision mode, so our code must not touch FPSCR. The toolchain is built with `--with-cpu=m4-nofpu` for this (2026-10-08). The old `-m4` toolchain is in `build/toolchain-m4-backup`.
  - `-mrenesas`: the firmware's calling convention keeps MACH and MACL across calls. With this flag, GCC saves MACL in each function that multiplies. `call_with_stack` saves MACH and MACL around the decoder.
  - `make check-abi` checks both rules in the compiler's assembler output.
  - `-mdiv=call-div1` uses the integer division routines.
- **Decoder:** dr_flac, pinned in `third_party/README`. Its license is public domain or MIT-0.
  - Build defines: `NO_STDIO`, `NO_OGG`, `NO_SIMD`, `NO_WCHAR`, `NO_CRC`, and custom `MALLOC`, `COPY_MEMORY` and `ZERO_MEMORY`.
  - Size at `-Os`: 33 KB code and 80 B data, before `--gc-sections`.
  - Undefined symbols: `memcpy`, `memset`, `__udivsi3`, `__ashrdi3`, `__movmemSI12_i4` and the pool allocator.
- **Memory per decoder:** about 32 KiB for a 4096-sample stereo block as int32, plus about 5 KiB of state and the seek table. Plan 64 KiB per context, with 4 contexts. That is 256 KiB in the padding.
- The padding holds `0xff`, not zeros. Link `.bss` into the image as zeros, so that no start-up code is needed.

## Version block and update rules

- The version block is at `0x04000700`. It holds `PIONEER`, `CDJ-900`, `4.32` (at `0x04000740`) and `20140325` (at `0x04000760`). One function near `0x042e552e` reads it.
- The updater takes a file only if the header version is greater than the running version.
- **Rollback:** `tools/build_patch.py rollback` writes the stock image with header `4.99`. After the update, the deck reports `4.32` again.

## Safety rules

1. The recovery loader at flash `0x10000` starts only when the packed application checksum fails. A broken application with valid checksums can keep the deck out of the updater.
2. So **no hook runs on the boot path**. FLAC code may run only while a FLAC file loads or plays. A bad decoder then crashes only on FLAC files, and the updater still works.
3. Before the first patch that adds code, have a hardware recovery path: a flash programmer or a clip on the main flash chip, and a full backup.
4. Each step adds one change, and each step has a rollback file ready on a second USB stick.

## Patch builder

- `tools/upd.py` holds the format code: S-records, CRC, LZSS, region sum and image sum.
  - The S-record writer rebuilds the stock `C900MAIN.UPD` byte for byte.
  - The LZSS compressor round-trips the full application. Its output is 0x2b9 bytes bigger than Pioneer's.
- `tools/build_patch.py hello` gives header 4.33, version string `4.33`, date `20261008`, and a marker at `0x043471d0`. No code changes.
- `tools/build_patch.py rollback` gives the stock application with header 4.99.
- The output goes to `out/<name>/C900MAIN.UPD`. Copy only that file to the stick. Do not copy the stock GUI, PANL or DRIV files.

## Deck test log

- **2026-10-08, rollback (header 4.99):** installed from a FAT32 stick. The deck showed 4.32.
- **2026-10-08, hello (header 4.33):** first skipped. The stick was a Ventoy stick: the data partition is exFAT, and the CDJ-900 does not see files on exFAT. From a FAT32 stick, the update installed and the deck showed **4.33**. This proves the builder, the LZSS compressor and all 3 checksums on the hardware.
- **Stick rule:** use a plain FAT32 stick for updates. Do not use Ventoy or exFAT sticks.

## Emulator (cdj2000-emulator on the CDJ-900 firmware)

- **Build:** `tools/build-emulator.sh` builds QEMU **v11.1.0** with the emulator's board.
  - v11.1.0 is pinned because later QEMU removed `use_exit_tb()`.
  - The first emulator patch needs fuzz, because one line of `trace-events` context differs.
  - `ninja` comes from pip in `build/venv`, so no sudo is needed.
- **Run:** `qemu-system-sh4 -M cdj2000-main -bios firmware/unpacked/main-firmware.bin`. The third `-serial` is the firmware's debug console (SCIF0).
- **Result:** the stock CDJ-900 MAIN boots on the CDJ-2000 board model (same CPU, R5S77641). The idle task runs its stack check, and the panel exchanges frames.
- **Console:** `tools/console.py PORT CMD...`. Arguments come first, then the 2-letter command. Output is Shift-JIS (cp932).
  - `?V` prints the version.
  - `ADDR,N,LR` reads N longwords. `LW` writes them.
  - `1,3,GU` turns on the debug log at verbose level.
  - `ff,KY` lists the keys. `29,KY` presses DevUSB, `2c` Browse, `32` RotaryPush, `40`/`41` turn the rotary.
- **GUI link:** there is no GUI board model. MAIN logs, in a loop, a GUI-link send message that asks the Blackfin to resend the fixed part, and a receive message that says the answer was discarded. The CDJ-900 GUI board is also a Blackfin.
- **USB works.** With `-device usb-storage`, the firmware enumerates the stick, reads the FAT file system in 32 KiB DMA blocks and writes `/PIONEER/USBANLZ/USBMNG.DAT` (400 KB, its own analysis store). Then it polls with TEST UNIT READY. `CDJ_USBH_TRACE=1` traces registers, and `CDJ_USBH_TRACE=scsi` traces SCSI commands. My first test showed nothing only because the trace was off.
- **Reading the stick image:** `build/venv` has `pyfatfs` (needs `setuptools<81`). The image from `make_sd_image` has an MBR, and FAT starts at LBA 2048 (`offset=2048*512`).
- **Track loading needs the GUI side.** On the CDJ-2000, the GUI board sends the browse and load requests to MAIN over the link. Panel keys alone probably do not load a track.
- **CDJ-900 GUI file:** `C900GUI.UPD` (Ver4.200) is S-records for flash `0xc0000`–`0x100000`. It holds fonts, menu strings (`FOLDER`, `ARTIST`, `PLAYLIST`, `VERSION No.`) and about 64 KiB of high-entropy data at `0xcc000`–`0xdc000` (probably packed code). It is not a CDJ-2000-style Blackfin stream, and `tools.cdj_gui.extract` rejects it.
- `tools/monitor.py` samples PC and PR through the QEMU monitor socket.

## Step A: decoder self-test (done 2026-10-08)

- **Build:** `make` builds `build/blob.bin` from `src/` (dr_flac, a pool allocator, a minimal runtime, the console command). It also builds `build/host_fltest`, the PC reference.
- **Patch:** `tools/build_patch.py fltest` places the blob at `0x04347400` in the padding. It copies the console command table to `0x04347200` with `FL` added before `ZZ`, and points the one table slot (`0xa4101dc4`) at the copy. Version 4.34.
- **Command:** `ADDR,LEN,FL` decodes the FLAC file at ADDR and prints the format, the frame count and a CRC-32 of the int32 samples.
- **Emulator test:** QEMU's `-device loader,file=X.flac,addr=0x06800000,force-raw=on` puts a file into RAM. `tools/emu.sh` starts a run.
- **Result:** the SH-4 CRCs equal the PC CRCs for 16-bit/44.1 kHz (`9bff1247`) and 24-bit/48 kHz (`9a685a3c`). The PC CRC equals ffmpeg's decode. dr_flac needs only about 12.6 KiB of pool memory.
- **Stack lesson:** the first run overflowed the console task's stack (about 4 KiB). `drflac_open_with_metadata_private` alone uses 4.5 KiB, and the chain needs about 5.5 KiB. The overflow zeroed the console's object ID at `0x045a01ec`, so its loop got `E_ID` (-18) forever.
  - **Rule:** every entry into our code switches to a private stack with `call_on_stack` (`src/stack.S`). Task stacks are small: `tsk_DJcontInBufFile` has 2 KiB.
  - Use one private stack per entry point and per task, with a busy flag.
- **Not measured:** decode speed on the real CPU. The emulator's timing does not match the hardware.

## Step B: GUI link stand-in (in progress, 2026-10-08)

- **Hardware path.** The CDJ-900 GUI shares the panel's M16C bus: SCIF `0xffe20000` with DMAC ch3 (receive) and ch4 (transmit), run by `M16C_Task` (`0x04248528`, exchange function `0x04247f44`).
  - GPIO `0xfff10048` bit 4 selects the device: set = panel, clear = GUI.
  - Bit 3 is the GUI acknowledge input. It must read clear at start-up (`0x042171bc`), and set after an exchange, otherwise MAIN logs a resend request (`0x04215430`).
  - A GUI exchange sends MAIN's 64-byte status record from `0xa4500800` and receives 66 bytes into `0xa4500000`. The first 48 bytes are the request, with the CRC (same routine as the UPD file) of bytes 0..45 at offset `0x2e`.
- **Emulator change.** Branch `cdj900-gui-bus` in `emu/cdj2000-emulator`, uncommitted. `CDJ_GUI_BUS=<chardev id>` routes GUI exchanges to a chardev ("CDJL"/"CDJI" frames) and drives bit 3. Without the variable, CDJ-2000 behaviour is unchanged.
  - Run with `-chardev socket,id=gui,host=127.0.0.1,port=5560,server=on,wait=off`.
  - Upstream tests for both CDJ-2000 profiles have not been run yet.
- **Fake GUI.** `tools/fake_gui.py PORT --inject SECONDS:TYPE:W2:...`. Result: about 13000 exchanges in 40 s, every CRC valid, and MAIN's GUI log is silent.
- **Request types** (word 1 & 0x3fff; dispatcher `0x0421552e`):
  - 1 = list (to DB, message `0x709`), 3 = artwork JPEG (`0x70b`)
  - 4 = waveform, 5 = cue/loop, 7 = settings, 8 = service mode, 9 = utility (`0x713`)
  - 0 and 6 do nothing.
  - No type is named "load". A load is probably an "enter" on a track row.
- **List answers** are 50-byte frames. Word 0 = `0x10` + cursor (cursor 11 gives `0x1b`), and the frames carry ASCII text.
  - With the rekordbox image, MAIN still answers `Not Loaded.` (cursor 0 and 3) and `Total Track 0` (cursor 11). The database is not loaded yet.
- **Keys.** `29,KY` (DevUSB) switches the source (status word 14 `0x17` → `0x1d`). Rotary push is forwarded to the GUI as status word 16 = 1.
- **Request words (measured 2026-10-08).** The CDJ-900 uses the CDJ-2000 request layout: word 1 = type (the CDJ-2000 injector also sets bit 15), word 2 = cursor, words 3.. = arguments.
  - **Load:** type 7 (settings) passes `request+2` to a second dispatcher at `0x04215f6c`, which switches on word 2. Value **1 = track load** (the debug log calls it a load request from the Blackfin): message `0x711`, index = (word 3 << 16) | word 4 into MAIN's current list. A load sent while the current list is the disc source starts a load (status word 18 = `0x8012`, word 24 = 1). Then task 62 logs `err : fnc=1011808h - 3` and `fnc=1010815h - 15`, and the deck returns to idle.
  - **Source list:** type 1, cursor 1, word 4 = KIND: 0 `LINK`, 1 `USB`, 2 `NO CARD`, 3 `NO DISC`. The `USB` row has word 3 = 8 and word 6 = 1.
  - **ENTER:** type 1, cursor 3, word 3 = 7, word 4 = KIND, word 5 = row. After the USB row is selected, each ENTER answers with header word 5 = 2, then 3 (the list level), but the row text is empty. Cursor 11 answers `Total Track 0`.
  - Each list answer is one 50-byte frame: word 0 = `0x10` + cursor, word 8 = 1, word 9 = row kind (`0x63` LINK, `0x54` USB, `0x55` CARD, `0x58` DISC message), word 11 = text length, then ASCII text.
- **Database reads.** With `CDJ_USBH_TRACE=scsi`, all reads happen 3.4–4.2 s after mount: about 4.3 MiB of `export.pdb` (3.2 MiB file) and the deck's own `USBMNG.DAT`. Browse requests later cause no reads. So the database is in MAIN's memory, but the lists are empty.
- **Open:** why the USB library lists are empty. Either MAIN rejects this export (it comes from a recent rekordbox), or word 3 (list type) must differ from 7 for the category list. Next: find the GUI → database translation of message `0x709` (not handled in `TotalCnt_MainTASK`; the database task `0x041697bc` handles only codes `0x13bb`+), or build a small export with an older rekordbox layout.
- **Cancel rule (dispatcher `0x0421552e`).** Word 1 bit 15 means "cancel the open request" (the debug log reports a cancel), and the dispatcher then also handles the type, so `0x8001` cancels and starts a new list request. Repeating the same list request while one is open also cancels it. The all-zero frame does nothing, so it is the right idle frame. The CDJ-2000 injector's `0x8000|TYPE` is wrong for the CDJ-900. Each answer is MAIN's "extended part" (its debug log gives the send size in words and the database size in bytes): one 50-byte frame per request.
- **Debug logs.** `1,2,GU` (GUI link), `1,3,TT` (general control), `1,3,DJ`. Output is Shift-JIS.
- **Rows seen without bit 15, after USB + Browse (folder stick):** cursor 0 → `USB` (level 1, kind `0x3f50`); cursor 2 → `TAG LIST` (word 8 = 2, kind `0x3f00`); cursor 11 → `Total Track 0`. In another run the USB key did not take effect, and cursor 2 returned `DISC`/`NO DISC`. Key timing is not reliable: check the status record (word 14 = `0x1d` after USB) before list requests.
- **Full decompilation (2026-10-08).** `tools/ghidra/DecompileAll.java` writes all 5482 functions to `build/decomp/<bucket>.c`, with referenced strings above each function and FPSCR noise removed. Local only, never publish. Some functions are missing because the seed script did not find them (for example `0x042172c2`). `tools/ghidra/decomp.sh ADDR` creates and decompiles a missing one.
- **List server (from the decompilation).** GUI type-1 requests become message `0x709`. The browse task loop is `FUN_0413cc64`, and it dispatches codes `0x709`–`0x70d` to `FUN_0414f7fe` and type 1 to `FUN_0414f868`. That function switches on **word 2 (cursor)**: 0 title, 1 select device, 2 BACK, 3 ENTER, 4/5/8/9 others, 11 tag list. Cursors 2, 3 and 5 branch on the **browse mode** at `*0x046cfe9c` (0, 1 or 2).
  - **Cursor 1** (`0x04150b6a`): sets mode = 2 and device = word 4 (0 LINK, 1 USB, 2 SD, 3 DISC). It also stores word 3.
  - **ENTER** (mode 2, `0x0415167a`): word 4 must be non-zero, and word 5 = row in the visible-row cache (`param + device*0x129*4 + 0xf2b*4`, 8 rows of 0x25 words). It then calls `FUN_04152724`.
  - **Word 3** = number of visible rows + 1 (`FUN_04152724`).
  - **Item count** comes from `FUN_041529e6`. It returns 0 when the device's media slot pointer (`param + 0x2e58 + device*4`) is null.
  - **Load** (message `0x711`, `FUN_041417a0`): in mode 2 it takes row index − scroll offset from the same row cache.
- **Answers.** MAIN announces each answer in status words 29 (kind) and 30 (size in words), then sends it as the "extended part". The 50-byte frames are complete 48-byte answers: a header plus the rows. The cursor-1 USB answer holds `USB` and `FOLDER`.
- **Old blocker, corrected (2026-10-08, later).** The note "`FUN_042b938c` never runs, so the USB slot is never registered" was wrong.
  - `FUN_042b938c` is the **CD drive** handler. The read-manager task loop `0x042b6a24` calls it for codes 2, `0x1389` and `0x138f`. Its "state 4" is the word at `+0x24` of a code-2 message. Task `0x04199594` sends that message after it polls the drive with an ATAPI vendor command (`0xe0 0x08`, built by `0x04105dd4`). The emulator has no drive, so the handler correctly never runs.
  - The read manager has three device groups: code 2/`0x1389`/`0x138f` (disc, `0x042b938c`), code `0xb`/`0x138a`/`0x1390` (USB, `0x042b801a`), and code `0x15`/`0x138b`/`0x1391` (SD, `0x042b89cc`).
  - **Mount notifier** `FUN_042e7e8c(letter, attached)`: drive `B` sends code `0x15`, drive `C` sends code `0xb` to the read manager. Message layout: code at `+8`, arguments from `+0x24`.
- **USB mount works in the emulator** (measured with `tools/probe.py` and a message probe, both sticks):
  1. About 3.3 s: `FUN_042e7e8c('C', 1)` from `0x0413163e`. The read manager gets code `0xb`.
  2. About 3.6 s: the browse task (`0x0414f140`) gets `0x3ec` (`C`, attached).
  3. About 4.5 s: the browse task sends `0x138a` back to the read manager, with a pointer at `+0x24`.
  4. After that, no media or database event reaches the browse task. Only the GUI's `0x709` requests arrive.
- **Browse state.** Browse struct at `0x05954a34`. The USB slot (`+0x2e58 + 1*4` = `0x059605c4`) is not null: it holds media record `0x05954ae4` (record ID 1, size `0x5f0`, `FUN_0414e9e4`) and drive number 3 (`C`). The per-drive entry in the record shows `01 43` (present, `C`).
  - The list query (`FUN_04153cc0` → `FUN_04138748`) uses the database only when record `+4` is non-zero. It stays 0 with both sticks, so every list has 0 rows.
- **Real blocker.** The database load after `0x138a` never reports "ready" to the browse task. The `0x138a` branch is at `0x042b8334`. It starts a load only when `FUN_042d2fb8()` (`*(... + 0x1660)`) returns 4, then sets event flag `0x2000000`. Not traced further.
- **Probe lessons.** A gdb watchpoint on browse data froze the firmware (only 92 GUI records in 45 s). A breakpoint on a hot function (`0x042ef0d4`, about 100 hits) delayed the mount events past the probe window. Use few, cold breakpoints.
- **Decision (2026-10-08).** Stop the browse bring-up. Test the FLAC hooks with a console command that bypasses the GUI and the database (see `STATUS.md`), then test on the deck.
- **Earlier pointer.** `DBSA_Task` (`0x041697bc`) dispatches on message codes `0x13bb`… (`FUN_0416a1a8` checks first). Find how the GUI's type-1 request (`0x709`) reaches it and which words select the root list and the device. Then check with `CDJ_USBH_TRACE=scsi` whether `export.pdb` is read.
- **Test media.** `tools/pdb.py` reads `export.pdb` track rows (file type at row `+0x5a`; FLAC = 5 confirmed on the user's export). `runs/rb/root` holds the copied database, four tracks (FLAC 4776, FLAC 4552 24-bit, WAV 1638, MP3 4730) and their ANLZ folders. `runs/rb/stick.img` is a 256 MB FAT32 image. This is the user's music: keep `runs/` out of git and never publish it. The user's stick was only read.

## Open questions

- Which of the 23 type 11/12 branches type 5 must join. Some branches are probably UI, for example the format label on screen.
- Which component verifies the image sum at `0x043bfffc`. Nothing in the application loads that address.
- Decode cost: decoding forward from a seek point must finish inside the time budget for cue and loop jumps.
