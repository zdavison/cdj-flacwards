# Third-party code

| Component | Where | License |
|---|---|---|
| dr_flac (mackron/dr_libs), with one local change | `third_party/dr_flac.h` | Public domain (Unlicense) or MIT-0, at your choice. See the end of the file and `third_party/README`. |
| QEMU plugin API header (used by `tools/qemu-plugins/blobcount.c`) | not included; from the QEMU source | GPL-2.0-or-later. `tools/qemu-plugins/blobcount.c` is therefore GPL-2.0-or-later. |
| cdj2000-emulator patch | `patches/cdj2000-emulator-*.patch` | GPL-2.0-or-later, as the cdj2000-emulator project. |

Not included, and never to be included: Pioneer DJ firmware, patched firmware, and decompiler output derived from the firmware.

The rest of this repository is MIT-licensed (see `LICENSE`).
