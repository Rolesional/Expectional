#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
KV3 iceren .txt dosyalari (workshop lineup) -> grenade_lineups_embedded.hpp

Kullanim:
  python embed_lineups_from_folder.py "C:\\path\\lineups"
  python embed_lineups_from_folder.py "C:\\path\\lineups" -o ..\\Valorant\\Game\\grenade_lineups_embedded.hpp
"""

from __future__ import annotations

import argparse
import glob
import os
import sys

# tools/ icinden kv3 modulunu yukle
_TOOLS = os.path.dirname(os.path.abspath(__file__))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from kv3_annotations_to_lineups import convert_kv3  # noqa: E402


def default_out_hpp() -> str:
    return os.path.normpath(os.path.join(_TOOLS, "..", "Valorant", "Game", "grenade_lineups_embedded.hpp"))


def cpp_escape_line(ln: str) -> str:
    return ln.replace("\\", "\\\\").replace('"', '\\"')


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("lineups_dir", help="KV3 .txt dosyalari klasoru")
    ap.add_argument("-o", "--output", default=None, help="Cikti .hpp (varsayilan: Valorant/Game/grenade_lineups_embedded.hpp)")
    args = ap.parse_args()

    d = os.path.abspath(args.lineups_dir)
    if not os.path.isdir(d):
        print(f"Klasor yok: {d}", file=sys.stderr)
        return 1

    paths = sorted(glob.glob(os.path.join(d, "*.txt")))
    if not paths:
        print("*.txt bulunamadi.", file=sys.stderr)
        return 1

    all_pipe: list[str] = []
    skipped: list[str] = []
    for p in paths:
        try:
            with open(p, "r", encoding="utf-8", errors="replace") as f:
                text = f.read()
        except OSError as e:
            print(f"Okunamadi {p}: {e}", file=sys.stderr)
            skipped.append(os.path.basename(p))
            continue
        if "MapAnnotationNode" not in text:
            skipped.append(os.path.basename(p))
            continue
        lines = convert_kv3(text)
        n = 0
        for ln in lines:
            if not ln.strip() or ln.startswith("#"):
                continue
            all_pipe.append(ln)
            n += 1
        if n == 0:
            skipped.append(os.path.basename(p))

    out = os.path.abspath(args.output or default_out_hpp())
    os.makedirs(os.path.dirname(out), exist_ok=True)

    with open(out, "w", encoding="utf-8", newline="\n") as w:
        w.write("#pragma once\n\n")
        w.write("/**\n")
        w.write(" * Gomulu grenade lineup satirlari:\n")
        w.write(" *   throw|map|name|sx|sy|sz|ax|ay|az|tx|ty|tz|nadeKind\n")
        w.write(" * nadeKind: smoke | molotov | flash | he | decoy (kucuk harf)\n")
        w.write(" * Calisma zamani otomatik duplike silme yok; liste ne ise o yuklenir.\n")
        w.write(f" * Kaynak: {d} ({len(paths)} txt, {len(all_pipe)} lineup satiri)\n")
        w.write(" */\n")
        w.write("namespace expectional_embedded_lineups {\n\n")
        w.write("inline constexpr const char* const kBundledPipeLines[] = {\n")
        for ln in all_pipe:
            w.write(f'\t"{cpp_escape_line(ln)}",\n')
        w.write("\tnullptr,\n")
        w.write("};\n\n")
        w.write("} // namespace expectional_embedded_lineups\n")

    print(f"Yazildi: {out}", file=sys.stderr)
    print(f"  lineup satiri: {len(all_pipe)}", file=sys.stderr)
    print(f"  islenen txt: {len(paths)}", file=sys.stderr)
    if skipped:
        print(f"  atlanan / 0 cikti: {len(skipped)} dosya", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
