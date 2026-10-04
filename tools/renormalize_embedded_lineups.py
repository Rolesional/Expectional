#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Bir kerelik: grenade_lineups_embedded.hpp icindeki pipe satirlarini
  - geometrik duplike temizler
  - isimleri Ingilizce (workshop RU/ES token cevirisi + temizlik) yapar
Cikti: ayni hpp dosyasinin uzerine yazar.
"""
from __future__ import annotations

import math
import os
import re
import sys
from typing import List, Optional, Tuple

HPP = os.path.normpath(
    os.path.join(os.path.dirname(__file__), "..", "Valorant", "Game", "grenade_lineups_embedded.hpp")
)

# --- Rusca / Kiril: kelime bazli (uzun once) ---
_RU_PHRASES: List[Tuple[str, str]] = [
    ("Молик от правого упора c ловера", "Molly right default from lower"),
    ("Молик от правого упора", "Molly right default"),
    ("Молик от правого упора c ловера", "Molly right default from lower"),
    ("Mid to B с лонга, дым просвет и дым шорт", "Mid to B from long, gap and short smokes"),
    ("Mid to B с лонга 2", "Mid to B from long 2"),
    ("Mid to B с лонга", "Mid to B from long"),
    ("Mid to B с титаника", "Mid to B from Tetris"),
    ("Дым двери с суицида", "Doors smoke from suicide"),
    ("Дым лонг угол и стенка", "Long corner and wall smoke"),
    ("Дым Б с Титаника и Дым банка", "B site from Tetris + bank smoke"),
    ("Дым Б с титаника 2", "B smoke from Tetris 2"),
    ("Дым Mid to B и молик", "Mid to B smoke + molly"),
    ("Дым Mid to B", "Mid to B smoke"),
    ("Дым двери Б и флэш", "B doors smoke + flash"),
    ("Дым двери и окно Б", "B doors and window smoke"),
    ("Дым окно Б", "B window smoke"),
    ("Дым банка и флэш", "Bank smoke + flash"),
    ("Дым банка самый быстрый", "Fastest bank smoke"),
    ("Дым шорт", "Short smoke"),
    ("Дым лонг стенка", "Long wall smoke"),
    ("Дым двери Б", "B doors smoke"),
    ("Кт просвет с банки 2", "CT gap from quad 2"),
    ("Кт просвет с банки", "CT gap from quad"),
    ("Кт просвет_2", "CT gap 2"),
    ("Кт просвет", "CT gap smoke"),
    ("Мид створки 3", "Mid double doors 3"),
    ("Мид створки 2", "Mid double doors 2"),
    ("Мид створки", "Mid double doors smoke"),
    ("Молик шорт", "Short molly"),
    ("Молик за двойными", "Molly behind doubles"),
    ("Молик кар и дым мид ту б", "Car molly + mid to B smoke"),
    ("Соло молик банка кт", "Solo CT bank molly"),
    ("Фаст дым Б", "Fast B smoke"),
    ("Флэш от броки", "Flash from broken wall"),
    ("Флэш Б над темкой", "B flash over dark"),
    ("Флэш лонг кт_2", "Long CT flash 2"),
    ("Флэш лонг кт", "Long CT flash"),
    ("Лучшие флэшки б", "B site pop flashes"),
    ("Развей мид", "Mid HE reveal"),
    ("Моменталка мид двери", "Instant mid doors flash"),
    ("Люрк Б", "B lurk smoke"),
    ("Просвет быстрый", "Fast gap smoke"),
    ("Просвет", "Gap smoke"),
    ("Флэш лонг кт", "Long CT flash"),
    ("Лучшие флэшки б", "B pop flashes"),
]

_RU_TOKENS: List[Tuple[str, str]] = [
    ("створки", "double doors"),
    ("просвет", "gap"),
    ("Молик", "Molly"),
    ("молик", "molly"),
    ("Дым", "Smoke"),
    ("дым", "smoke"),
    ("Флэш", "Flash"),
    ("флэш", "flash"),
    ("Флэшки", "Flashes"),
    ("флэшки", "flashes"),
    ("Моменталка", "Instant"),
    ("моменталка", "instant"),
    ("Люрк", "Lurk"),
    ("люрк", "lurk"),
    ("Кт", "CT"),
    ("кт", "CT"),
    ("Титаника", "Tetris"),
    ("титаника", "Tetris"),
    ("лонга", "long"),
    ("Лонг", "Long"),
    ("лонг", "long"),
    ("шорт", "short"),
    ("Шорт", "Short"),
    ("мид", "mid"),
    ("Мид", "Mid"),
    ("двери", "doors"),
    ("Двери", "Doors"),
    ("окно", "window"),
    ("Окно", "Window"),
    ("банка", "bank"),
    ("Банка", "Bank"),
    ("Стенка", "wall"),
    ("стенка", "wall"),
    ("угол", "corner"),
    ("Угол", "corner"),
    ("суицида", "suicide"),
    ("Суицида", "suicide"),
    ("Быстрый", "fast"),
    ("быстрый", "fast"),
    ("самый", "most"),
    ("Самый", "most"),
    ("быстрый", "fast"),
    ("от", "from"),
    ("От", "From"),
    ("и", "and"),
    ("И", "and"),
    ("с", "from"),
    ("С", "from"),
    ("над", "over"),
    ("Над", "over"),
    ("за", "behind"),
    ("За", "behind"),
    ("к", "to"),
    ("К", "to"),
    ("ту", "to"),
    ("кар", "car"),
    ("Кар", "Car"),
    ("двойными", "doubles"),
    ("правого", "right"),
    ("упора", "default"),
    ("ловера", "lower"),
    ("Развей", "Reveal"),
    ("развей", "reveal"),
    ("Лучшие", "Best"),
    ("лучшие", "best"),
    ("броки", "broken"),
    ("темкой", "dark"),
    ("Соло", "Solo"),
    ("соло", "solo"),
    ("Фаст", "Fast"),
    ("фаст", "fast"),
    ("Моменталка", "Instant"),
    ("двери", "doors"),
]

# --- Ispanyolca / workshop tek kelime ---
_ES_REPLACE: List[Tuple[str, str]] = [
    ("fondo", "deep"),
    ("Fondo", "Deep"),
    ("humo", "smoke"),
    ("Humo", "Smoke"),
    ("Puerta", "Door"),
    ("puerta", "door"),
    ("ventana", "window"),
    ("Ventana", "Window"),
    ("bomba", "bomb"),
    ("Bomba", "Bomb"),
    ("desde", "from"),
    ("Desde", "From"),
    ("hasta", "to"),
    ("Hasta", "To"),
    ("corto", "short"),
    ("Corto", "Short"),
    ("corta", "short"),
    ("largo", "long"),
    ("Largo", "Long"),
    ("techo", "roof"),
    ("Techo", "Roof"),
    ("planta", "floor"),
    ("Planta", "Floor"),
    ("solo", "solo"),
    ("Solo", "Solo"),
    ("molotov", "molly"),
    ("granada", "grenade"),
]


def round_key(x: float, places: int = 2) -> float:
    return round(x, places)


def parse_hpp_strings(text: str) -> List[str]:
    out: List[str] = []
    for m in re.finditer(r'^\t"((?:\\.|[^"\\])*)",\s*$', text, re.MULTILINE):
        s = m.group(1)
        s = bytes(s, "utf-8").decode("unicode_escape") if "\\" in s else s.replace('\\"', '"').replace("\\\\", "\\")
        if "|" in s and not s.startswith("#"):
            out.append(s)
    return out


def parse_pipe(s: str) -> Optional[Tuple[int, str, str, List[float], str]]:
    parts = s.split("|")
    if len(parts) < 13:
        return None
    try:
        throw = int(parts[0])
        mp = parts[1]
        name = parts[2]
        nums = [float(parts[i]) for i in range(3, 12)]
        kind = parts[12].strip()
    except (ValueError, IndexError):
        return None
    return throw, mp, name, nums, kind


def build_line(throw: int, mp: str, name: str, nums: List[float], kind: str) -> str:
    sx, sy, sz, ax, ay, az, tx, ty, tz = nums
    return f"{throw}|{mp}|{name}|{sx}|{sy}|{sz}|{ax}|{ay}|{az}|{tx}|{ty}|{tz}|{kind}"


def has_cyrillic(s: str) -> bool:
    return bool(re.search(r"[\u0400-\u04FF]", s))


def normalize_name(name: str) -> str:
    s = name.strip()
    if not s:
        return "Lineup"
    for ru, en in _RU_PHRASES:
        if ru in s:
            s = s.replace(ru, en)
    for ru, en in _RU_TOKENS:
        s = s.replace(ru, en)
    for es, en in _ES_REPLACE:
        s = s.replace(es, en)
    # Kalan Kiril: kelime sinirinda parcalara bolup generic etiket
    if has_cyrillic(s):
        s = re.sub(r"[\u0400-\u04FF]+", " lineup", s)
    s = re.sub(r"\s+", " ", s).strip(" -_|")
    # Bos veya anlamsiz
    if not s or s == "lineup":
        s = "Workshop lineup"
    # Cok uzun HUD
    if len(s) > 72:
        s = s[:69] + "..."
    return s


def dedupe_key(row: str) -> Tuple:
    p = parse_pipe(row)
    if not p:
        return ()
    throw, mp, _, nums, kind = p
    sx, sy, sz, ax, ay, az, tx, ty, tz = nums
    return (
        mp.lower(),
        kind.lower(),
        throw,
        round_key(sx),
        round_key(sy),
        round_key(sz),
        round_key(ax),
        round_key(ay),
        round_key(az),
        round_key(tx),
        round_key(ty),
        round_key(tz),
    )


def main() -> int:
    if len(sys.argv) > 1:
        global HPP
        HPP = os.path.abspath(sys.argv[1])
    with open(HPP, "r", encoding="utf-8") as f:
        text = f.read()

    rows = parse_hpp_strings(text)
    if not rows:
        print("Satir bulunamadi", file=sys.stderr)
        return 1

    seen: set = set()
    unique_rows: List[str] = []
    for r in rows:
        k = dedupe_key(r)
        if not k or k in seen:
            continue
        seen.add(k)
        p = parse_pipe(r)
        if not p:
            continue
        throw, mp, name, nums, kind = p
        new_name = normalize_name(name)
        unique_rows.append(build_line(throw, mp, new_name, nums, kind))

    header = """#pragma once

/**
 * Gomulu grenade lineup satirlari:
 *   throw|map|name|sx|sy|sz|ax|ay|az|tx|ty|tz|nadeKind
 * nadeKind: smoke | molotov | flash | he | decoy (kucuk harf)
 * Isimler workshop kaynaklarindan Ingilizceye normalize edildi; geometrik duplikeler cikarildi.
 */
namespace expectional_embedded_lineups {

inline constexpr const char* const kBundledPipeLines[] = {
"""

    def cpp_esc(s: str) -> str:
        return s.replace("\\", "\\\\").replace('"', '\\"')

    body = "".join(f'\t"{cpp_esc(r)}",\n' for r in unique_rows)
    footer = """\tnullptr,
};

} // namespace expectional_embedded_lineups
"""

    out = header + body + footer
    with open(HPP, "w", encoding="utf-8", newline="\n") as f:
        f.write(out)

    print(f"OK {HPP}", file=sys.stderr)
    print(f"  onceki: {len(rows)}  sonra: {len(unique_rows)}  cikarilan: {len(rows)-len(unique_rows)}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
