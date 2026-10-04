#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
CS2 workshop / practice map grenade KV3 (MapAnnotationNode) -> Expectional grenade_lineups.txt

Cikti satiri:
  throw|map|name|sx|sy|sz|ax|ay|az|tx|ty|tz|nadeKind
  nadeKind: smoke | molotov | flash | he | decoy

Alanlar: main Position -> stand; aim_target Angles -> bakis; aim_target Position -> hedef nokta;
  isim Title.Text; throw tipi Desc + JumpThrow tahmini.

Varsayilan cikti (dosya): %USERPROFILE%\\Documents\\Expectional\\configs\\grenade_lineups.txt
Hile gomulu liste: --emit-cpp-array ciktisini Valorant/Game/grenade_lineups_embedded.hpp icine yapistir.

Kullanim:
  python kv3_annotations_to_lineups.py harita.kv3 --emit-cpp-array
  python kv3_annotations_to_lineups.py harita.kv3 --stdout
  python kv3_annotations_to_lineups.py harita.kv3 -o ...
  python kv3_annotations_to_lineups.py harita.kv3
"""

from __future__ import annotations

import argparse
import os
import re
import sys
from collections import defaultdict
from typing import Dict, List, Optional, Tuple

NODE_HEAD = re.compile(r"MapAnnotationNode(\d+)\s*=\s*\{")


def extract_balanced_braces(text: str, open_brace_index: int) -> Optional[str]:
    """open_brace_index points at '{'. Returns inner body (without outer braces) or None."""
    if open_brace_index < 0 or open_brace_index >= len(text) or text[open_brace_index] != "{":
        return None
    depth = 0
    i = open_brace_index
    while i < len(text):
        c = text[i]
        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return text[open_brace_index + 1 : i]
        i += 1
    return None


def split_map_annotation_nodes(text: str) -> Dict[int, str]:
    out: Dict[int, str] = {}
    for m in NODE_HEAD.finditer(text):
        idx = int(m.group(1))
        brace_pos = m.end() - 1
        body = extract_balanced_braces(text, brace_pos)
        if body is not None:
            out[idx] = body
    return out


def first_str(pattern: str, body: str) -> str:
    m = re.search(pattern, body, re.DOTALL)
    return m.group(1).strip() if m else ""


def parse_vec(body: str, key: str) -> Optional[Tuple[float, float, float]]:
    m = re.search(rf"{key}\s*=\s*\[\s*([^\]]+)\s*\]", body)
    if not m:
        return None
    parts = [p.strip() for p in m.group(1).split(",")]
    if len(parts) < 3:
        return None
    try:
        return float(parts[0]), float(parts[1]), float(parts[2])
    except ValueError:
        return None


def parse_block(body: str) -> dict:
    st = first_str(r'SubType\s*=\s*"([^"]*)"', body)
    node_id = first_str(r'Id\s*=\s*"([0-9a-fA-F-]{36})"', body)
    master = first_str(r'MasterNodeId\s*=\s*"([0-9a-fA-F-]{36})"', body)
    title_txt = first_str(r"Title\s*=\s*\{.*?Text\s*=\s*\"((?:\\\\.|[^\"\\\\])*)\"", body)
    desc_txt = first_str(r"Desc\s*=\s*\{.*?Text\s*=\s*\"((?:\\\\.|[^\"\\\\])*)\"", body)
    jt = first_str(r"JumpThrow\s*=\s*(true|false)", body).lower() == "true"
    pos = parse_vec(body, "Position")
    ang = parse_vec(body, "Angles")
    grenade_type = first_str(r'GrenadeType\s*=\s*"([^"]*)"', body).lower()
    return {
        "subtype": st,
        "id": node_id,
        "master": master,
        "title": title_txt,
        "desc": desc_txt,
        "jump_throw": jt,
        "pos": pos,
        "ang": ang,
        "grenade_type": grenade_type,
    }


def normalize_nade_kind(raw: str) -> str:
    if not raw:
        return "smoke"
    t = raw.lower().strip()
    if "inferno" in t or "molotov" in t or "molly" in t or "incend" in t or "fire" in t:
        return "molotov"
    if "flash" in t:
        return "flash"
    if "decoy" in t:
        return "decoy"
    if t == "he" or "frag" in t or "hegrenade" in t:
        return "he"
    if "smoke" in t:
        return "smoke"
    return "smoke"


def extract_map_name(text: str) -> str:
    m = re.search(r'MapName\s*=\s*"([^"]*)"', text)
    return m.group(1).strip() if m else ""


def sanitize_name(s: str) -> str:
    s = s.replace("\r\n", "\n").replace("\\n", " ").replace("\n", " ")
    s = s.replace("|", "_").replace("\t", " ")
    while "  " in s:
        s = s.replace("  ", " ")
    return s.strip() or "Lineup"


def infer_throw_type(desc: str, jump_throw: bool) -> int:
    d = (desc or "").lower()
    d_compact = re.sub(r"[\s\-_+]+", "", d)
    # JumpThrow / JT / spacebar bind
    if "jumpthrow" in d_compact:
        jump_throw = True
    if re.search(r"\bjt\b", d) or re.search(r"\bjt[,\.\)]", d):
        jump_throw = True
    if "spacebar" in d or "space +" in d or "+jump" in d or "jump+" in d:
        jump_throw = True
    # Run + (throw|lmb) patterns
    if ("running" in d and "lmb" in d) or ("lmb" in d and "running" in d):
        return 7
    if "run" in d and "jump" in d:
        return 7
    duck = "duck" in d or "crouch" in d or "ctrl" in d
    jt = "jumpthrow" in d_compact or "jump throw" in d or ("jump" in d and "throw" in d) or jump_throw
    forward = "forward" in d or re.search(r"\bw\b", d) or "+w" in d or " w " in d

    if duck and forward and jt:
        return 5
    if duck and jt:
        return 4
    if duck and not jt:
        return 3
    if forward and jt:
        return 2
    if jt:
        return 1
    if forward:
        return 6
    return 0


def unique_name(base: str, counts: Dict[str, int]) -> str:
    counts[base] = counts.get(base, 0) + 1
    n = counts[base]
    if n == 1:
        return base
    return f"{base}_{n}"


def convert_kv3(text: str) -> List[str]:
    map_name = extract_map_name(text) or "_"
    nodes = split_map_annotation_nodes(text)
    parsed: Dict[int, dict] = {}
    for k, body in nodes.items():
        parsed[k] = parse_block(body)

    by_id: Dict[str, dict] = {}
    aim_by_master: Dict[str, dict] = defaultdict(list)
    for p in parsed.values():
        if p.get("id"):
            by_id[p["id"]] = p
        if p.get("subtype") == "aim_target" and p.get("master"):
            aim_by_master[p["master"]].append(p)

    lines: List[str] = []
    lines.append("# throw|map|name|sx|sy|sz|ax|ay|az|tx|ty|tz|nadeKind (smoke|molotov|flash|he|decoy)")
    name_seen: Dict[str, int] = {}

    for p in parsed.values():
        if p.get("subtype") != "main":
            continue
        mid = p.get("id")
        if not mid:
            continue
        aims = aim_by_master.get(mid, [])
        if not aims:
            continue
        # Birden fazla aim_target varsa ilki (genelde tek)
        aim = aims[0]
        if not p.get("pos") or not aim.get("pos") or not aim.get("ang"):
            continue
        sx, sy, sz = p["pos"]
        ax, ay, az = aim["ang"]
        tx, ty, tz = aim["pos"]
        raw_name = sanitize_name(p.get("title") or aim.get("title") or "Lineup")
        nm = unique_name(raw_name, name_seen)
        text_blob = " ".join(
            [
                aim.get("desc") or "",
                p.get("desc") or "",
                aim.get("title") or "",
                p.get("title") or "",
            ]
        )
        ti = infer_throw_type(
            text_blob,
            bool(p.get("jump_throw")) or bool(aim.get("jump_throw")),
        )
        kind = normalize_nade_kind(p.get("grenade_type") or "")
        map_esc = map_name if map_name else "_"
        name_esc = nm.replace("|", "_")
        lines.append(
            f"{ti}|{map_esc}|{name_esc}|{sx}|{sy}|{sz}|{ax}|{ay}|{az}|{tx}|{ty}|{tz}|{kind}"
        )
    return lines


def emit_cpp_string_literals(lines: List[str]) -> None:
    """C++ dizisine yapistirmak icin kisa string literal satirlari."""
    for ln in lines:
        if ln.startswith("#"):
            continue
        escaped = ln.replace("\\", "\\\\").replace('"', '\\"')
        print(f'    "{escaped}",')
    print("    nullptr,")


def default_lineups_path() -> str:
    """config_io.cpp / ExpectionalConfigDirUtf8 ile ayni yol."""
    return os.path.join(os.environ.get("USERPROFILE", "."), "Documents", "Expectional", "configs", "grenade_lineups.txt")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("input", help="KV3 / annotation text file")
    ap.add_argument(
        "-o",
        "--output",
        default=None,
        help="Cikti dosyasi (varsayilan: Documents\\Expectional\\configs\\grenade_lineups.txt)",
    )
    ap.add_argument(
        "--stdout",
        action="store_true",
        help="Dosyaya yazma; satirlari stdout'a yaz",
    )
    ap.add_argument(
        "--append",
        metavar="OUT",
        help="Belirtilen dosyaya ekle (mevcut dosya varsa baslik satiri tekrarlanmaz)",
    )
    ap.add_argument(
        "--emit-cpp-array",
        action="store_true",
        help="stdout: C++ string literal satirlari (grenade_lineups_embedded.hpp icine)",
    )
    args = ap.parse_args()
    with open(args.input, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()
    out_lines = convert_kv3(text)

    if args.emit_cpp_array:
        emit_cpp_string_literals(out_lines)
        n = sum(1 for ln in out_lines if ln and not ln.startswith("#"))
        print(f"// {n} lineup satiri (mevcut dizideki nullptr oncesine ekle)", file=sys.stderr)
        return 0

    if args.append:
        mode = "a" if os.path.isfile(args.append) and os.path.getsize(args.append) > 0 else "w"
        os.makedirs(os.path.dirname(os.path.abspath(args.append)), exist_ok=True)
        with open(args.append, "w" if mode == "w" else "a", encoding="utf-8") as o:
            if mode == "w":
                o.write("\n".join(out_lines) + "\n")
            else:
                body = [ln for ln in out_lines if not ln.startswith("#")]
                o.write("\n".join(body) + "\n")
        print(f"Yazildi: {args.append} ({len(out_lines) - 1} lineup)", file=sys.stderr)
        return 0

    if args.stdout:
        sys.stdout.write("\n".join(out_lines) + "\n")
        return 0

    out_path = args.output or default_lineups_path()
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as o:
        o.write("\n".join(out_lines) + "\n")
    print(f"Yazildi: {out_path} ({len(out_lines) - 1} lineup)", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
