#pragma once
#include "../../Includes/Imgui/imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

/** Player ESP parca ofsetleri (piksel). Kutu ve dolgu burada yok. */
namespace EspLayout {

enum Id : int {
	Name = 0,
	C4,
	HealthBar,
	Armor,
	Ammo,
	Weapon,
	Distance,
	HealthText,
	Zoom,
	Flashed,
	Head,
	Bones,
	Snap,
	Eye,
	Count
};

struct Off {
	float x = 0.f;
	float y = 0.f;
};

inline Off g[Count]{};

inline const char* Key(Id id)
{
	switch (id) {
	case Name: return "name";
	case C4: return "c4";
	case HealthBar: return "hpbar";
	case Armor: return "armor";
	case Ammo: return "ammo";
	case Weapon: return "weapon";
	case Distance: return "dist";
	case HealthText: return "hptext";
	case Zoom: return "zoom";
	case Flashed: return "flash";
	case Head: return "head";
	case Bones: return "bones";
	case Snap: return "snap";
	case Eye: return "eye";
	default: return nullptr;
	}
}

inline ImVec2 Get(Id)
{
	return ImVec2(0.f, 0.f);
}

inline void Add(Id id, float dx, float dy)
{
	if (id < 0 || id >= Count)
		return;
	g[id].x = std::clamp(g[id].x + dx, -280.f, 280.f);
	g[id].y = std::clamp(g[id].y + dy, -280.f, 280.f);
}

inline ImVec2 Shift(Id id, float x, float y)
{
	const ImVec2 o = Get(id);
	return ImVec2(x + o.x, y + o.y);
}

struct Hit {
	int id = -1;
	ImVec2 a{};
	ImVec2 b{};
	bool line = false;
};

inline std::vector<Hit> g_hits;

inline void ClearHits()
{
	g_hits.clear();
}

inline void AddRect(Id id, float x, float y, float w, float h)
{
	if (w < 1.f)
		w = 1.f;
	if (h < 1.f)
		h = 1.f;
	Hit hit;
	hit.id = id;
	hit.a = ImVec2(x - 3.f, y - 3.f);
	hit.b = ImVec2(x + w + 3.f, y + h + 3.f);
	g_hits.push_back(hit);
}

inline void AddLine(Id id, ImVec2 a, ImVec2 b)
{
	Hit hit;
	hit.id = id;
	hit.a = a;
	hit.b = b;
	hit.line = true;
	g_hits.push_back(hit);
}

inline float DistSeg(ImVec2 p, ImVec2 a, ImVec2 b)
{
	const float abx = b.x - a.x;
	const float aby = b.y - a.y;
	const float ab2 = abx * abx + aby * aby;
	float t = 0.f;
	if (ab2 > 1e-4f)
		t = std::clamp(((p.x - a.x) * abx + (p.y - a.y) * aby) / ab2, 0.f, 1.f);
	const float dx = p.x - (a.x + abx * t);
	const float dy = p.y - (a.y + aby * t);
	return std::sqrt(dx * dx + dy * dy);
}

inline int HitTest(ImVec2 p)
{
	for (int i = static_cast<int>(g_hits.size()) - 1; i >= 0; --i) {
		const Hit& h = g_hits[static_cast<size_t>(i)];
		if (!h.line) {
			if (p.x >= h.a.x && p.y >= h.a.y && p.x <= h.b.x && p.y <= h.b.y)
				return h.id;
		} else if (DistSeg(p, h.a, h.b) <= 7.f) {
			return h.id;
		}
	}
	return -1;
}

inline void WriteConfig(void (*writeFloat)(void*, const char*, float), void* file)
{
	char key[64];
	for (int i = 0; i < Count; ++i) {
		const char* n = Key(static_cast<Id>(i));
		if (!n)
			continue;
		snprintf(key, sizeof key, "Visuals.lay.%s.x", n);
		writeFloat(file, key, g[i].x);
		snprintf(key, sizeof key, "Visuals.lay.%s.y", n);
		writeFloat(file, key, g[i].y);
	}
}

inline bool ParseConfig(const std::string& key, float v)
{
	if (key.rfind("Visuals.lay.", 0) != 0)
		return false;
	const bool axisX = key.size() >= 2 && key[key.size() - 2] == '.' && key.back() == 'x';
	const bool axisY = key.size() >= 2 && key[key.size() - 2] == '.' && key.back() == 'y';
	if (!axisX && !axisY)
		return false;
	const std::string name = key.substr(12, key.size() - 12 - 2);
	for (int i = 0; i < Count; ++i) {
		const char* n = Key(static_cast<Id>(i));
		if (n && name == n) {
			if (axisX)
				g[i].x = std::clamp(v, -280.f, 280.f);
			else
				g[i].y = std::clamp(v, -280.f, 280.f);
			return true;
		}
	}
	return true;
}

} // namespace EspLayout
