#pragma once
/** Catalyst player.cpp get_weapon_icon ile ayni glyph harfi (fonts/weapons_font_data). */
#include <cstring>
#include <unordered_map>
#include <string>

inline const char* WeaponIconGlyphForInternalName(const char* name) {
	if (!name || !name[0])
		return "?";
	static const std::unordered_map<std::string, const char*> kMap = {
		{ "deagle", "A" }, { "elite", "B" }, { "fiveseven", "C" }, { "glock", "D" },
		{ "revolver", "J" }, { "p2000", "E" }, { "p250", "F" }, { "usp", "G" }, { "tec9", "H" },
		{ "cz75a", "I" }, { "mac10", "K" }, { "ump45", "L" }, { "bizon", "M" },
		{ "mp7", "N" }, { "mp9", "R" }, { "p90", "O" }, { "mp5sd", "N" },
		{ "galilar", "Q" }, { "famas", "R" }, { "m4a1", "S" }, { "m4a4", "S" },
		{ "aug", "U" }, { "sg556", "V" }, { "ak47", "W" },
		{ "g3sg1", "X" }, { "scar20", "Y" }, { "awp", "Z" }, { "ssg08", "a" },
		{ "xm1014", "b" }, { "sawedoff", "c" }, { "mag7", "d" }, { "nova", "e" },
		{ "negev", "f" }, { "m249", "g" }, { "zeus", "h" }, { "taser", "h" },
		{ "flashbang", "i" }, { "hegrenade", "j" }, { "smokegrenade", "k" },
		{ "molotov", "l" }, { "decoy", "m" }, { "incgrenade", "n" }, { "c4", "o" },
		{ "ct_knife", "]" }, { "t_knife", "[" }, { "knife", "]" }, { "knife_ct", "]" },
		{ "bayonet", "]" }, { "css", "]" }, { "flip", "]" }, { "gut", "]" },
		{ "karambit", "]" }, { "m9_bayonet", "]" }, { "tactical", "]" },
		{ "falchion", "]" }, { "survival_bowie", "]" }, { "butterfly", "]" },
		{ "push", "]" }, { "cord", "]" }, { "canis", "]" }, { "ursus", "]" },
		{ "gypsy_jackknife", "]" }, { "outdoor", "]" }, { "stiletto", "]" },
		{ "widowmaker", "]" }, { "skeleton", "]" }, { "kukri", "]" },
	};
	const auto it = kMap.find(name);
	return it != kMap.end() ? it->second : "?";
}
