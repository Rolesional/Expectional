#pragma once

#include <cstdint>
#include <cstring>

inline const char* WeaponNameFromDefIndex(uint16_t id) {
	switch (id) {
	case 1: return "deagle";
	case 2: return "elite";
	case 3: return "fiveseven";
	case 4: return "glock";
	case 7: return "ak47";
	case 8: return "aug";
	case 9: return "awp";
	case 10: return "famas";
	case 11: return "g3sg1";
	case 13: return "galilar";
	case 14: return "m249";
	case 16: return "m4a4";
	case 17: return "mac10";
	case 19: return "p90";
	case 23: return "mp5sd";
	case 24: return "ump45";
	case 25: return "xm1014";
	case 26: return "bizon";
	case 27: return "mag7";
	case 28: return "negev";
	case 29: return "sawedoff";
	case 30: return "tec9";
	case 31: return "zeus";
	case 32: return "p2000";
	case 33: return "mp7";
	case 34: return "mp9";
	case 35: return "nova";
	case 36: return "p250";
	case 38: return "scar20";
	case 39: return "sg556";
	case 40: return "ssg08";
	case 42: return "ct_knife";
	case 43: return "flashbang";
	case 44: return "hegrenade";
	case 45: return "smokegrenade";
	case 46: return "molotov";
	case 47: return "decoy";
	case 48: return "incgrenade";
	case 49: return "c4";
	case 59: return "t_knife";
	case 500: return "bayonet";
	case 503: return "css";
	case 505: return "flip";
	case 506: return "gut";
	case 507: return "karambit";
	case 508: return "m9_bayonet";
	case 509: return "tactical";
	case 512: return "falchion";
	case 514: return "survival_bowie";
	case 515: return "butterfly";
	case 516: return "push";
	case 517: return "cord";
	case 518: return "canis";
	case 519: return "ursus";
	case 520: return "gypsy_jackknife";
	case 521: return "outdoor";
	case 522: return "stiletto";
	case 523: return "widowmaker";
	case 525: return "skeleton";
	case 526: return "kukri";
	case 60: return "m4a1";
	case 61: return "usp";
	case 63: return "cz75a";
	case 64: return "revolver";
	default: return nullptr;
	}
}

inline uint16_t DefIndexFromWeaponClassName(const char* raw) {
	if (!raw || !raw[0])
		return 0;
	const char* s = raw;
	if (std::strncmp(s, "weapon_", 7) == 0)
		s += 7;
	struct Row { const char* n; uint16_t id; };
	static const Row kRows[] = {
		{"deagle", 1}, {"elite", 2}, {"fiveseven", 3}, {"glock", 4},
		{"ak47", 7}, {"aug", 8}, {"awp", 9}, {"famas", 10}, {"g3sg1", 11},
		{"galilar", 13}, {"m249", 14}, {"m4a4", 16}, {"m4a1", 16},
		{"mac10", 17}, {"p90", 19}, {"mp5sd", 23}, {"ump45", 24},
		{"xm1014", 25}, {"bizon", 26}, {"mag7", 27}, {"negev", 28},
		{"sawedoff", 29}, {"tec9", 30}, {"taser", 31}, {"hkp2000", 32},
		{"mp7", 33}, {"mp9", 34}, {"nova", 35}, {"p250", 36},
		{"scar20", 38}, {"sg556", 39}, {"sg553", 39}, {"ssg08", 40},
		{"knife", 42}, {"knife_t", 59},
		{"bayonet", 500}, {"knife_bayonet", 500},
		{"knife_css", 503}, {"knife_classic", 503},
		{"knife_flip", 505}, {"knife_gut", 506}, {"knife_karambit", 507},
		{"knife_m9_bayonet", 508}, {"knife_m9", 508},
		{"knife_tactical", 509}, {"knife_huntsman", 509},
		{"knife_falchion", 512},
		{"knife_survival_bowie", 514}, {"knife_bowie", 514},
		{"knife_butterfly", 515},
		{"knife_push", 516}, {"knife_daggers", 516},
		{"knife_cord", 517}, {"knife_paracord", 517},
		{"knife_canis", 518}, {"knife_survival", 518},
		{"knife_ursus", 519},
		{"knife_gypsy_jackknife", 520}, {"knife_navaja", 520},
		{"knife_outdoor", 521}, {"knife_nomad", 521},
		{"knife_stiletto", 522},
		{"knife_widowmaker", 523}, {"knife_talon", 523},
		{"knife_skeleton", 525},
		{"knife_kukri", 526},
		{"flashbang", 43}, {"hegrenade", 44},
		{"smokegrenade", 45}, {"molotov", 46}, {"decoy", 47},
		{"incgrenade", 48}, {"incendiary", 48}, {"c4", 49},
		{"m4a1_silencer", 60}, {"m4a1_silencer_off", 60},
		{"usp_silencer", 61}, {"usp_silencer_off", 61},
		{"cz75a", 63}, {"revolver", 64},
	};
	for (const Row& r : kRows) {
		if (std::strcmp(s, r.n) == 0)
			return r.id;
	}
	if (std::strstr(s, "knife_t") == s)
		return 59;
	if (std::strstr(s, "knife"))
		return 42;
	return 0;
}

inline const char* WeaponDisplayNameFromKey(const char* key) {
	if (!key || !key[0])
		return nullptr;
	struct Row { const char* k; const char* n; };
	static const Row kRows[] = {
		{"deagle", "Desert Eagle"}, {"elite", "Dual Berettas"}, {"fiveseven", "Five-SeveN"},
		{"glock", "Glock-18"}, {"ak47", "AK-47"}, {"aug", "AUG"}, {"awp", "AWP"},
		{"famas", "FAMAS"}, {"g3sg1", "G3SG1"}, {"galilar", "Galil AR"}, {"m249", "M249"},
		{"m4a4", "M4A4"}, {"mac10", "MAC-10"}, {"p90", "P90"}, {"mp5sd", "MP5-SD"},
		{"ump45", "UMP-45"}, {"xm1014", "XM1014"}, {"bizon", "PP-Bizon"}, {"mag7", "MAG-7"},
		{"negev", "Negev"}, {"sawedoff", "Sawed-Off"}, {"tec9", "Tec-9"}, {"zeus", "Zeus x27"},
		{"taser", "Zeus x27"}, {"p2000", "P2000"}, {"hkp2000", "P2000"}, {"mp7", "MP7"},
		{"mp9", "MP9"}, {"nova", "Nova"}, {"p250", "P250"}, {"scar20", "SCAR-20"},
		{"sg556", "SG 553"}, {"sg553", "SG 553"}, {"ssg08", "SSG 08"},
		{"ct_knife", "Knife"}, {"t_knife", "Knife"}, {"knife", "Knife"},
		{"flashbang", "Flashbang"}, {"hegrenade", "HE Grenade"}, {"smokegrenade", "Smoke Grenade"},
		{"molotov", "Molotov"}, {"decoy", "Decoy Grenade"}, {"incgrenade", "Incendiary Grenade"},
		{"c4", "C4"}, {"m4a1", "M4A1-S"}, {"usp", "USP-S"}, {"cz75a", "CZ75-Auto"},
		{"revolver", "R8 Revolver"},
		{"bayonet", "Bayonet"}, {"css", "Classic Knife"}, {"flip", "Flip Knife"},
		{"gut", "Gut Knife"}, {"karambit", "Karambit"}, {"m9_bayonet", "M9 Bayonet"},
		{"tactical", "Huntsman Knife"}, {"falchion", "Falchion Knife"},
		{"survival_bowie", "Bowie Knife"}, {"butterfly", "Butterfly Knife"},
		{"push", "Shadow Daggers"}, {"cord", "Paracord Knife"}, {"canis", "Survival Knife"},
		{"ursus", "Ursus Knife"}, {"gypsy_jackknife", "Navaja Knife"}, {"outdoor", "Nomad Knife"},
		{"stiletto", "Stiletto Knife"}, {"widowmaker", "Talon Knife"},
		{"skeleton", "Skeleton Knife"}, {"kukri", "Kukri Knife"},
	};
	for (const Row& r : kRows) {
		if (std::strcmp(key, r.k) == 0)
			return r.n;
	}
	return key;
}

inline bool IsKnifeCodeName(const char* name) {
	if (!name || !name[0])
		return false;
	static const char* kNames[] = {
		"ct_knife", "t_knife", "knife", "bayonet", "css", "flip", "gut", "karambit",
		"m9_bayonet", "tactical", "falchion", "survival_bowie", "butterfly", "push",
		"cord", "canis", "ursus", "gypsy_jackknife", "outdoor", "stiletto",
		"widowmaker", "skeleton", "kukri",
	};
	for (const char* n : kNames) {
		if (std::strcmp(name, n) == 0)
			return true;
	}
	return std::strstr(name, "knife") != nullptr;
}
