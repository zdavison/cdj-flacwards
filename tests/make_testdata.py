#!/usr/bin/env python3
"""Make the FLAC test files and their reference WAV files in build/testdata.

A reference WAV file holds the PCM that ffmpeg decodes, in the 44-byte header
that Python's wave module writes. vwav must give the same bytes.
The script does not make the files again if build/testdata/done exists.
Delete build/testdata to make them again.
"""
from __future__ import annotations

import subprocess
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / "build/testdata"
SECONDS = 20

# name: (rate, bits, extra flac options). "PIC" becomes the picture path.
VALID = {
    "t16_44": (44100, 16, []),
    "t24_48": (48000, 24, []),
    "t16_48_noseek": (48000, 16, ["--no-seektable"]),
    "t24_44_b1152": (44100, 24, ["--blocksize=1152"]),
    "t16_44_pic": (44100, 16, ["--picture=PIC"]),
}
# name: expected vwav_open result (VWAV_E_FORMAT = -2, VWAV_E_SCOPE = -3)
REJECT = {
    "mono": -3,
    "r96": -3,
    "b8": -3,
    "zero_total": -3,
    "notflac": -2,
    "id3": -2,
    "bad_info": -2,
}


def run(*args) -> None:
    subprocess.run([str(a) for a in args], check=True,
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def source_wav(path: Path, rate: int, bits: int, channels: int) -> None:
    """Two tones with noise, so that each FLAC frame is different."""
    tones = ["0.5*sin(440*2*PI*t)+0.1*(random(0)-0.5)",
             "0.3*sin(661*2*PI*t)+0.1*(random(1)-0.5)"]
    codec = {8: "pcm_u8", 16: "pcm_s16le", 24: "pcm_s24le"}[bits]
    run("ffmpeg", "-y", "-f", "lavfi",
        "-i", f"aevalsrc={'|'.join(tones[:channels])}:s={rate}:d={SECONDS}",
        "-c:a", codec, path)


def encode(name: str, rate: int, bits: int, opts=(), channels: int = 2) -> None:
    src = OUT / f"{name}.src.wav"
    source_wav(src, rate, bits, channels)
    run("flac", "-s", "-f", *opts, "-o", OUT / f"{name}.flac", src)
    src.unlink()


def reference(name: str, rate: int, bits: int) -> None:
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(OUT / f"{name}.flac"), "-f", f"s{bits}le", "-"],
        check=True, capture_output=True).stdout
    with wave.open(str(OUT / f"{name}.ref.wav"), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(bits // 8)
        w.setframerate(rate)
        w.writeframes(raw)


def picture() -> Path:
    """A noise image of about 1 MB, for a large PICTURE block."""
    pic = OUT / "pic.png"
    run("ffmpeg", "-y", "-f", "lavfi",
        "-i", "nullsrc=s=1000x1000,geq=lum='random(1)*255':cb=128:cr=128",
        "-frames:v", "1", pic)
    return pic


def long_noseek() -> None:
    """Two minutes of tones and noise, 4608-sample blocks, no SEEKTABLE. Its
    audio holds byte patterns that look like frame headers, so some seeks hit
    a false header (as on a real deck track)."""
    src = OUT / "long_noseek.src.wav"
    run("ffmpeg", "-y", "-f", "lavfi", "-i",
        "aevalsrc=0.4*sin(220*2*PI*t)*sin(0.3*t)+0.3*(random(0)-0.5)|"
        "0.4*sin(331*2*PI*t)+0.3*(random(1)-0.5):s=44100:d=120",
        "-c:a", "pcm_s16le", src)
    run("flac", "-s", "-f", "--no-seektable", "--blocksize=4608", "-o", OUT / "long_noseek.flac", src)
    src.unlink()


def make_all() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    if not (OUT / "long_noseek.flac").exists():
        long_noseek()
    if (OUT / "done").exists():
        return
    pic = picture()
    for name, (rate, bits, opts) in VALID.items():
        encode(name, rate, bits, [o.replace("PIC", str(pic)) for o in opts])
        reference(name, rate, bits)
    encode("mono", 44100, 16, channels=1)
    encode("r96", 96000, 24)
    encode("b8", 44100, 8)
    base = (OUT / "t16_44.flac").read_bytes()
    # STREAMINFO starts at byte 8. The 36-bit total sample count is the low
    # 4 bits of byte 21 and bytes 22 to 25.
    zero = bytearray(base)
    zero[21] &= 0xF0
    zero[22:26] = bytes(4)
    (OUT / "zero_total.flac").write_bytes(zero)
    # Byte 4 is the type of the first metadata block. 4 is VORBIS_COMMENT,
    # so the first block is not STREAMINFO.
    bad = bytearray(base)
    bad[4] = 0x04
    (OUT / "bad_info.flac").write_bytes(bad)
    (OUT / "notflac.flac").write_bytes((OUT / "t16_44.ref.wav").read_bytes())
    # An ID3v2.4 tag of 10 bytes before the FLAC stream.
    (OUT / "id3.flac").write_bytes(b"ID3\x04\x00\x00\x00\x00\x00\x0a" + bytes(10) + base)
    (OUT / "truncated.flac").write_bytes(base[: len(base) * 6 // 10])
    mid = bytearray(base)
    m = len(base) // 2
    mid[m:m + 4096] = bytes(4096)
    (OUT / "corrupt_mid.flac").write_bytes(mid)
    (OUT / "done").write_bytes(b"")


if __name__ == "__main__":
    make_all()
