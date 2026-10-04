#!/usr/bin/env python3
"""r69-driver.sys -> Valorant/Driver.h (XOR + chunked constexpr arrays)."""

from __future__ import annotations

import argparse
import math
import sys
from pathlib import Path

XOR_KEY = bytes(
    [
        0xD4, 0x81, 0xC5, 0xF5, 0x6E, 0x7C, 0x29, 0xF7, 0x80, 0x31, 0x8F, 0xD9,
        0x5A, 0xD9, 0xE2, 0x1B, 0x15, 0x0D, 0x2F, 0x66, 0xB5, 0x14, 0x13, 0x90,
        0xD7, 0x82, 0x0A, 0xFC, 0x53, 0xD2, 0xD5, 0xDF,
    ]
)

PART_COUNT = 8
BYTES_PER_LINE = 12


def xor_data(plain: bytes) -> bytes:
    return bytes(b ^ XOR_KEY[i % len(XOR_KEY)] for i, b in enumerate(plain))


def format_part(name: str, data: bytes) -> list[str]:
    lines = [f"inline constexpr uint8_t {name}[] = {{"]
    row: list[str] = []
    for i, b in enumerate(data):
        row.append(f"0x{b:02X}")
        if len(row) >= BYTES_PER_LINE:
            lines.append("\t" + ", ".join(row) + ",")
            row = []
    if row:
        lines.append("\t" + ", ".join(row) + ",")
    lines.append("};")
    lines.append("")
    return lines


def emit_header(cipher: bytes, out_path: Path) -> None:
    part_size = math.ceil(len(cipher) / PART_COUNT)
    parts: list[bytes] = []
    for i in range(PART_COUNT):
        start = i * part_size
        chunk = cipher[start : start + part_size]
        if chunk:
            parts.append(chunk)

    body: list[str] = [
        "#pragma once",
        "// Surucu goruntusu: XOR + parca parca stream (disk/mtlsiz, yalnizca .h).",
        "// Yeniden uretmek: python tools/gen_driver_h.py ..\\..\\drayvir\\r69-driver\\build\\r69-driver.sys",
        "",
        "#include <cstddef>",
        "#include <cstdint>",
        "",
        "namespace expectional_km_driver_stream {",
        "",
        "constexpr uint8_t kXorKey[32] = { "
        + ", ".join(f"0x{b:02X}" for b in XOR_KEY)
        + " };",
        "",
        f"constexpr size_t kPlainSize = {len(cipher)};",
        "",
    ]

    for idx, part in enumerate(parts):
        body.extend(format_part(f"kPart{idx}", part))

    body.append("struct Part { const uint8_t* data; size_t size; };")
    body.append("")
    body.append("inline constexpr Part kParts[] = {")
    for idx, part in enumerate(parts):
        body.append(f"\t{{ kPart{idx}, sizeof(kPart{idx}) }},")
    body.append("};")
    body.append("")
    body.append(f"inline constexpr size_t kPartCount = {len(parts)};")
    body.append("")
    body.append("}  // namespace expectional_km_driver_stream")
    body.append("")

    out_path.write_text("\n".join(body), encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description="Embed .sys into Driver.h")
    parser.add_argument("sys_path", type=Path, help="Path to r69-driver.sys")
    parser.add_argument(
        "-o",
        "--out",
        type=Path,
        default=Path(__file__).resolve().parent.parent / "Valorant" / "Driver.h",
        help="Output Driver.h path",
    )
    args = parser.parse_args()

    plain = args.sys_path.read_bytes()
    if len(plain) < 2 or plain[0:2] != b"MZ":
        print("error: input is not a PE (.sys) file", file=sys.stderr)
        return 1

    cipher = xor_data(plain)
    emit_header(cipher, args.out)
    print(f"Wrote {args.out} ({len(plain)} bytes plain, {len(cipher)} xor, {PART_COUNT} parts)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
