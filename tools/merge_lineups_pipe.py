#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Birden fazla grenade lineup kaynagini (pipe satirlari) birlestir.

Satir formati:
  throw|map|name|sx|sy|sz|ax|ay|az|tx|ty|tz|nadeKind

Varsayilan: duplike silmez (tum gecerli satirlar kalir). Istenirse --dedupe.

Kullanim:
  python merge_lineups_pipe.py a.txt b.txt c.txt -o merged.txt
  python merge_lineups_pipe.py *.txt --emit-cpp-array > literals.txt
  python merge_lineups_pipe.py *.txt -o out.txt --dedupe
"""

from __future__ import annotations

import argparse
import glob
import math
import os
import sys
from dataclasses import dataclass
from typing import Iterable, List, Optional, Sequence, Tuple

K_STAND_NAME_DUP = 10.0
K_STAND_COPY_PASTE_DUP = 1.5
K_TARGET_COPY_PASTE_DUP = 3.0


def dist3(a: Tuple[float, float, float], b: Tuple[float, float, float]) -> float:
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2 + (a[2] - b[2]) ** 2)


def maps_compatible(ma: str, mb: str) -> bool:
    if not ma or not mb:
        return True
    return ma.lower() == mb.lower()


def nade_key(kind: str) -> str:
    t = (kind or "smoke").lower().strip()
    if "molotov" in t or "molly" in t or "inferno" in t or "incend" in t:
        return "molotov"
    if "flash" in t:
        return "flash"
    if "decoy" in t:
        return "decoy"
    if t == "he" or "frag" in t or "hegrenade" in t:
        return "he"
    return "smoke"


@dataclass
class Row:
    raw: str
    throw: int
    map: str
    name: str
    stand: Tuple[float, float, float]
    target: Tuple[float, float, float]
    nade: str


def parse_line(line: str) -> Optional[Row]:
    s = line.strip()
    if not s or s.startswith("#"):
        return None
    parts = s.split("|")
    if len(parts) < 12:
        return None
    try:
        throw = int(parts[0].strip())
    except ValueError:
        return None
    if throw < 0 or throw >= 8:
        return None
    m = parts[1].strip()
    name = parts[2].strip()
    if not name:
        return None
    try:
        sx, sy, sz = map(float, parts[3:6])
        tx, ty, tz = map(float, parts[9:12])
    except ValueError:
        return None
    kind = parts[12].strip() if len(parts) >= 13 else "smoke"
    return Row(raw=s, throw=throw, map=m, name=name, stand=(sx, sy, sz), target=(tx, ty, tz), nade=nade_key(kind))


def is_duplicate(e: Row, existing: List[Row]) -> bool:
    for o in existing:
        if not maps_compatible(e.map, o.map):
            continue
        d_stand = dist3(e.stand, o.stand)
        d_target = dist3(e.target, o.target)
        if e.name.lower() == o.name.lower() and d_stand < K_STAND_NAME_DUP:
            return True
        if e.nade == o.nade and d_stand < K_STAND_COPY_PASTE_DUP and d_target < K_TARGET_COPY_PASTE_DUP:
            return True
    return False


def iter_paths(patterns: Sequence[str]) -> List[str]:
    out: List[str] = []
    for p in patterns:
        if any(ch in p for ch in "*?["):
            out.extend(sorted(glob.glob(p, recursive=True)))
        elif os.path.isfile(p):
            out.append(p)
    return out


def read_all_lines(paths: Iterable[str]) -> List[str]:
    lines: List[str] = []
    for path in paths:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            for ln in f:
                lines.append(ln.rstrip("\n\r"))
    return lines


def merge_rows(lines: Iterable[str], dedupe: bool) -> List[Row]:
    kept: List[Row] = []
    for ln in lines:
        r = parse_line(ln)
        if r is None:
            continue
        if dedupe and is_duplicate(r, kept):
            continue
        kept.append(r)
    return kept


def emit_cpp(rows: List[Row]) -> None:
    for r in rows:
        escaped = r.raw.replace("\\", "\\\\").replace('"', '\\"')
        print(f'    "{escaped}",')
    print("    nullptr,")


def main() -> int:
    ap = argparse.ArgumentParser(description="Pipe lineup dosyalarini birlestir (varsayilan: duplike yok)")
    ap.add_argument("inputs", nargs="+", help="txt veya glob (ornek: lineups/*.txt)")
    ap.add_argument("-o", "--output", help="Birlestirilmis pipe txt")
    ap.add_argument("--emit-cpp-array", action="store_true", help="C++ string literal ciktisi (stdout)")
    ap.add_argument(
        "--dedupe",
        action="store_true",
        help="Istege bagli: ayni isim+yakin ayak veya milimetrik kopya satirlari ele",
    )
    args = ap.parse_args()

    paths = iter_paths(args.inputs)
    if not paths:
        print("Girdi dosyasi yok.", file=sys.stderr)
        return 1

    lines = read_all_lines(paths)
    rows = merge_rows(lines, dedupe=args.dedupe)

    if args.emit_cpp_array:
        emit_cpp(rows)
        tag = "duplike elendi" if args.dedupe else "tum satirlar"
        print(f"// {len(rows)} satir ({tag}), kaynak dosya: {len(paths)}", file=sys.stderr)
        return 0

    if not args.output:
        print("-o veya --emit-cpp-array gerekli.", file=sys.stderr)
        return 1

    os.makedirs(os.path.dirname(os.path.abspath(args.output)) or ".", exist_ok=True)
    with open(args.output, "w", encoding="utf-8") as f:
        f.write("# throw|map|name|sx|sy|sz|ax|ay|az|tx|ty|tz|nadeKind\n")
        for r in rows:
            f.write(r.raw + "\n")
    print(f"Yazildi: {args.output} ({len(rows)} satir), okunan: {len(paths)} dosya", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
