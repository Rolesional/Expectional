#include "grenade_lineup.hpp"

#include "config_io.hpp"
#include "esp_extras.hpp"
#include "globals.hpp"
#include "expectional_render_scheduler.hpp"
#include "grenade_esp.hpp"
#include "grenade_lineup_workshop.hpp"
#include "grenade_lineups_embedded.hpp"
#include "offsets_runtime.hpp"
#include "steam_workshop_discovery.hpp"
#include "tri_loader.hpp"
#include "../Driver/driver.hpp"

#include "../../Includes/Imgui/imgui.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string.h>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace UE4Structs;

namespace {

enum class LineupNadeKind : std::uint8_t {
	kSmoke = 0,
	kMolotov,
	kHe,
	kFlash,
	kDecoy,
};

static LineupNadeKind ParseNadeKindField(std::string t) {
	for (char& c : t)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	if (t.find("molotov") != std::string::npos || t.find("molly") != std::string::npos ||
	    t.find("inferno") != std::string::npos || t.find("incend") != std::string::npos)
		return LineupNadeKind::kMolotov;
	if (t.find("flash") != std::string::npos)
		return LineupNadeKind::kFlash;
	if (t.find("decoy") != std::string::npos)
		return LineupNadeKind::kDecoy;
	if (t == "he" || t.find("hegrenade") != std::string::npos || t.find("frag") != std::string::npos)
		return LineupNadeKind::kHe;
	return LineupNadeKind::kSmoke;
}

static void NadeKindRgb(LineupNadeKind k, int& r, int& g, int& b) {
	switch (k) {
	case LineupNadeKind::kMolotov: r = 255; g = 105; b = 35; break;
	case LineupNadeKind::kHe: r = 255; g = 215; b = 55; break;
	case LineupNadeKind::kFlash: r = 255; g = 255; b = 245; break;
	case LineupNadeKind::kDecoy: r = 130; g = 195; b = 255; break;
	default: r = 200; g = 220; b = 235; break; // smoke
	}
}

static const char* NadeKindHudLabel(LineupNadeKind k) noexcept {
	switch (k) {
	case LineupNadeKind::kMolotov: return "Molotov";
	case LineupNadeKind::kHe: return "HE";
	case LineupNadeKind::kFlash: return "Flash";
	case LineupNadeKind::kDecoy: return "Decoy";
	default: return "Smoke";
	}
}

static ImU32 NadeKindImU32(LineupNadeKind k, float alpha) {
	int r, g, b;
	NadeKindRgb(k, r, g, b);
	return IM_COL32(r, g, b, static_cast<int>(std::clamp(alpha, 0.f, 1.f) * 255.f));
}

/** CS2 econ item def (tutulan nade): 43 flash, 44 HE, 45 smoke, 46 molotov, 47 decoy, 48 inc. */
static LineupNadeKind HeldNadeKindFromDefIndex(std::uint16_t def) noexcept {
	switch (def) {
	case 43: return LineupNadeKind::kFlash;
	case 44: return LineupNadeKind::kHe;
	case 45: return LineupNadeKind::kSmoke;
	case 46:
	case 48: return LineupNadeKind::kMolotov;
	case 47: return LineupNadeKind::kDecoy;
	default: return LineupNadeKind::kSmoke;
	}
}

struct LineupEntry {
	std::string map;
	std::string name;
	std::string desc;
	Vector3 stand{};
	Vector3 angles{};
	Vector3 target{};
	/** CS2 TextPositionOffset: Title/Desc kutusu bu dunya noktasinda (genelde stand + Z). */
	Vector3 label_world{};
	/** 0 center, 1 left, 2 right — TextHorizontalAlign. */
	std::uint8_t label_h_align = 0;
	ExpectionalGrenadeThrowType throw_type = ExpectionalGrenadeThrowType::kNormal;
	LineupNadeKind nade_kind = LineupNadeKind::kSmoke;
	/** Bu lineup hangi pack'ten geldi (dosya yolu) — render aktif pack'leri kullanir. */
	std::string pack_id;
};

/** Pack: bir .txt dosyasi = bir guide paketi. */
struct LineupPack {
	std::string id;                       /**< Tam dosya yolu (UTF-8). Benzersiz key. */
	std::string title;                    /**< Goruntuleme adi (KV3 'Title' veya dosya adi). */
	std::string map;                      /**< Paketin bagli oldugu map (cogu zaman tek map). */
	std::vector<std::size_t> lineup_idx;  /**< g_lineups icindeki indexler. */
};

std::mutex g_lineup_mtx;
std::vector<LineupEntry> g_lineups;
std::unordered_map<std::string, std::vector<size_t>> g_lineups_by_map;
std::vector<LineupPack> g_packs;                                  /**< Tum paketler. */
std::unordered_map<std::string, std::size_t> g_pack_by_id;        /**< pack_id -> g_packs idx. */
std::unordered_map<std::string, std::vector<std::size_t>> g_packs_by_map;
std::atomic<bool> g_loaded{false};
std::atomic<bool> g_load_started{false};

/** Browser staging + active set: pack_id'leri tutar (kullanici secimi). */
std::mutex g_browser_mtx;
std::vector<std::string> g_browser_staged;  /**< pack_id'ler */
std::vector<std::string> g_browser_active;  /**< pack_id'ler */
/** Global hard cap yok; sadece map basina 2'den fazla olunca uyari verilir. */

static void RebuildLineupMapIndex() {
	g_lineups_by_map.clear();
	for (size_t i = 0; i < g_lineups.size(); ++i) {
		std::string key = g_lineups[i].map;
		if (key.empty())
			key = "*";
		g_lineups_by_map[std::move(key)].push_back(i);
	}
}

/** g_lineups -> g_packs aggregation. Embedded (pack_id bos) lineup'lar paket olusturmaz. */
static void RebuildPackIndex() {
	g_packs.clear();
	g_pack_by_id.clear();
	g_packs_by_map.clear();
	for (std::size_t i = 0; i < g_lineups.size(); ++i) {
		const LineupEntry& le = g_lineups[i];
		if (le.pack_id.empty())
			continue;  /** built-in / embedded lineup'lar pakette gosterilmez */
		auto it = g_pack_by_id.find(le.pack_id);
		if (it == g_pack_by_id.end()) {
			LineupPack p;
			p.id = le.pack_id;
			p.map = le.map;
			g_packs.push_back(std::move(p));
			g_pack_by_id[le.pack_id] = g_packs.size() - 1;
			g_packs.back().lineup_idx.push_back(i);
		} else {
			LineupPack& p = g_packs[it->second];
			p.lineup_idx.push_back(i);
			if (p.map.empty() && !le.map.empty())
				p.map = le.map;
		}
	}
	for (std::size_t pi = 0; pi < g_packs.size(); ++pi) {
		std::string key = g_packs[pi].map;
		if (key.empty()) key = "*";
		g_packs_by_map[std::move(key)].push_back(pi);
	}
}

static bool SplitPipeFields(std::string line, std::vector<std::string>& out) {
	out.clear();
	size_t pos = 0;
	while (true) {
		const size_t n = line.find('|', pos);
		if (n == std::string::npos) {
			out.emplace_back(line.substr(pos));
			break;
		}
		out.emplace_back(line.substr(pos, n - pos));
		pos = n + 1;
	}
	return !out.empty();
}

static float Dist3(const Vector3& a, const Vector3& b) {
	const float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
	return std::sqrt(dx * dx + dy * dy + dz * dz);
}

static float Smoothstep01(float edge0, float edge1, float x) noexcept {
	if (edge0 >= edge1)
		return x < edge0 ? 1.f : 0.f;
	const float t = std::clamp((x - edge0) / (edge1 - edge0), 0.f, 1.f);
	return t * t * (3.f - 2.f * t);
}

/** save_fps: ayak halkasi kenarinda aim HUD'un git-gel etmesini azaltir. */
static float SmoothStandBlend(uintptr_t key, float target) noexcept {
	if (!Settings::misc::save_fps || key == 0)
		return target;
	static std::unordered_map<uintptr_t, float> s_blend;
	if (s_blend.size() > 128u)
		s_blend.clear();
	float& v = s_blend[key];
	const float k = 0.28f;
	v += (target - v) * k;
	return v;
}

static uintptr_t LineupBlendKey(const Vector3& stand) noexcept {
	const auto q = [](float f) -> std::uint32_t {
		return static_cast<std::uint32_t>(std::lround(f * 4.f));
	};
	const std::uint64_t hx = q(stand.x);
	const std::uint64_t hy = q(stand.y);
	const std::uint64_t hz = q(stand.z);
	return static_cast<uintptr_t>((hx * 0x9E3779B1ull) ^ (hy * 0x85EBCA6Bull) ^ (hz * 0xC2B2AE35ull));
}

/** Ayak zemini (m_vecOrigin) — aim nisani sadece stand halkasi uzerindeyken. */
static Vector3 ReadLocalPawnFeetWorld(std::uintptr_t pawn) noexcept {
	if (!pawn || !offsets::m_vecOrigin)
		return {};
	return g_GameMem.readv<Vector3>(pawn + static_cast<std::uintptr_t>(offsets::m_vecOrigin));
}

static bool ParsePipeLine(const char* line, LineupEntry& e) {
	if (!line || !line[0] || line[0] == '#')
		return false;
	std::vector<std::string> fld;
	if (!SplitPipeFields(std::string(line), fld) || fld.size() < 12)
		return false;
	const int ti = std::atoi(fld[0].c_str());
	if (ti < 0 || ti >= static_cast<int>(ExpectionalGrenadeThrowType::kCount))
		return false;
	e.throw_type = static_cast<ExpectionalGrenadeThrowType>(ti);
	e.map = (fld[1] == "_") ? std::string{} : fld[1];
	e.name = (fld[2] == "_") ? std::string{} : fld[2];
	e.stand = Vector3(std::stof(fld[3]), std::stof(fld[4]), std::stof(fld[5]));
	e.angles = Vector3(std::stof(fld[6]), std::stof(fld[7]), std::stof(fld[8]));
	e.target = Vector3(std::stof(fld[9]), std::stof(fld[10]), std::stof(fld[11]));
	e.nade_kind = (fld.size() >= 13) ? ParseNadeKindField(fld[12]) : LineupNadeKind::kSmoke;
	e.desc.clear();
	if (fld.size() >= 14) {
		e.desc = fld[13];
		std::string norm;
		norm.reserve(e.desc.size());
		for (size_t i = 0; i < e.desc.size(); ++i) {
			if (e.desc[i] == '\r') {
				if (i + 1 < e.desc.size() && e.desc[i + 1] == '\n')
					++i;
				norm.push_back('\n');
			} else
				norm.push_back(e.desc[i]);
		}
		e.desc = std::move(norm);
	}
	e.label_world = Vector3(e.stand.x, e.stand.y, e.stand.z + 60.f);
	e.label_h_align = 0;
	return !e.name.empty();
}

static void AppendParsedRowsTo(std::vector<LineupEntry>& tmp,
                               const std::vector<grenade_lineup_workshop::ParsedRow>& ws,
                               std::unordered_map<std::string, std::string>* pack_titles = nullptr) {
	for (const auto& pr : ws) {
		LineupEntry e{};
		e.map = pr.map;
		e.name = pr.name;
		e.desc = pr.desc;
		e.stand = pr.stand;
		e.angles = pr.angles;
		e.target = pr.target;
		const int ti = pr.throw_idx;
		e.throw_type = (ti >= 0 && ti < static_cast<int>(ExpectionalGrenadeThrowType::kCount))
			? static_cast<ExpectionalGrenadeThrowType>(ti)
			: ExpectionalGrenadeThrowType::kNormal;
		if (pr.nade_kind >= 0 && pr.nade_kind <= static_cast<int>(LineupNadeKind::kDecoy))
			e.nade_kind = static_cast<LineupNadeKind>(pr.nade_kind);
		else
			e.nade_kind = LineupNadeKind::kSmoke;
		e.label_world = pr.label_world;
		e.label_h_align = pr.label_h_align;
		e.pack_id = pr.source_pack_id;
		if (pack_titles && !pr.source_pack_id.empty() && !pr.source_pack_title.empty())
			pack_titles->emplace(pr.source_pack_id, pr.source_pack_title);
		tmp.push_back(std::move(e));
	}
}

static bool LineupMapMatchesCurrent(const std::string& lineupMap, const std::string& curMap)
{
	if (lineupMap.empty() || lineupMap == "*" || curMap.empty() || curMap == "*")
		return true;
	if (lineupMap == curMap)
		return true;
	return grenade_lineup_workshop::NormalizeWorkshopMapName(lineupMap) ==
	       grenade_lineup_workshop::NormalizeWorkshopMapName(curMap);
}

static void LoadBundledLineupsOnce() {
	if (g_loaded.load(std::memory_order_acquire))
		return;
	ExpectionalEnsureLineupsDirAndReadme();
	std::vector<LineupEntry> tmp;
	std::unordered_map<std::string, std::string> pack_titles;
	tmp.reserve(8192);
	for (size_t i = 0; expectional_embedded_lineups::kBundledPipeLines[i]; ++i) {
		LineupEntry e{};
		if (!ParsePipeLine(expectional_embedded_lineups::kBundledPipeLines[i], e))
			continue;
		tmp.push_back(std::move(e));
	}
	/** Legacy: kullanici klasoru (Belgeler\Expectional\lineups) geriye uyumluluk. */
	{
		std::vector<grenade_lineup_workshop::ParsedRow> ws;
		grenade_lineup_workshop::AppendWorkshopKv3FromDirectory(ExpectionalLineupsDirWide(), ws);
		AppendParsedRowsTo(tmp, ws, &pack_titles);
	}
	/** Steam workshop: tum kutuphanelerdeki workshop\\content\\730 agacini tara. */
	{
		for (const std::wstring& ws730 : steam_ws::FindAllWorkshopContent730Roots()) {
			std::vector<grenade_lineup_workshop::ParsedRow> ws;
			grenade_lineup_workshop::AppendAllFromWorkshopContent730(ws730, ws);
			AppendParsedRowsTo(tmp, ws, &pack_titles);
		}
	}
	{
		std::lock_guard<std::mutex> lk(g_lineup_mtx);
		g_lineups = std::move(tmp);
		RebuildLineupMapIndex();
		RebuildPackIndex();
		/** pack_id -> KV3 dosyasindan cikartilmis baslik veya dosya adi. */
		for (LineupPack& p : g_packs) {
			auto it = pack_titles.find(p.id);
			if (it != pack_titles.end() && !it->second.empty())
				p.title = it->second;
			if (p.title.empty()) {
				/** Yedek: dosya adini cikar. */
				std::string n = p.id;
				const size_t s1 = n.find_last_of("/\\");
				if (s1 != std::string::npos) n = n.substr(s1 + 1);
				const size_t s2 = n.find_last_of('.');
				if (s2 != std::string::npos) n = n.substr(0, s2);
				p.title = n.empty() ? std::string("Lineup pack") : n;
			}
		}
		g_loaded = true;
	}
}

static void EnsureLineupsLoadAsync() {
	if (g_loaded.load(std::memory_order_acquire))
		return;
	if (g_load_started.exchange(true))
		return;
	std::thread([] {
		LoadBundledLineupsOnce();
	}).detach();
}

static bool W2sOut(const Vector3& pos, Vector3& out, const view_matrix_t& m) {
	const auto& M = m.matrix;
	out.x = M[0][0] * pos.x + M[0][1] * pos.y + M[0][2] * pos.z + M[0][3];
	out.y = M[1][0] * pos.x + M[1][1] * pos.y + M[1][2] * pos.z + M[1][3];
	const float w = M[3][0] * pos.x + M[3][1] * pos.y + M[3][2] * pos.z + M[3][3];
	if (w < 0.01f)
		return false;
	const float inv_w = 1.f / w;
	out.x *= inv_w;
	out.y *= inv_w;
	const ImGuiIO& io = ImGui::GetIO();
	float sw = io.DisplaySize.x;
	float sh = io.DisplaySize.y;
	if (sw < 1.f)
		sw = static_cast<float>(GetSystemMetrics(SM_CXSCREEN));
	if (sh < 1.f)
		sh = static_cast<float>(GetSystemMetrics(SM_CYSCREEN));
	const float x = sw * 0.5f + 0.5f * out.x * sw + 0.5f;
	const float y = sh * 0.5f - 0.5f * out.y * sh + 0.5f;
	out.x = x;
	out.y = y;
	out.z = w;
	return true;
}

static void DrawFlatCircle(
	ImDrawList* dl,
	const view_matrix_t& vm,
	const Vector3& center,
	float radiusWorld,
	int segments,
	ImU32 col,
	float lineThickness) {
	if (segments < 8)
		segments = 8;
	const float twoPi = 6.28318530718f;
	Vector3 prevScreen{};
	bool havePrev = false;
	for (int i = 0; i <= segments; ++i) {
		const float t = (static_cast<float>(i % segments) / static_cast<float>(segments)) * twoPi;
		const Vector3 wp(
			center.x + std::cosf(t) * radiusWorld,
			center.y + std::sinf(t) * radiusWorld,
			center.z);
		Vector3 sp{};
		if (!W2sOut(wp, sp, vm))
			continue;
		if (havePrev)
			dl->AddLine(ImVec2(prevScreen.x, prevScreen.y), ImVec2(sp.x, sp.y), col, lineThickness);
		prevScreen = sp;
		havePrev = true;
	}
}

static void DrawLineupHud(
	ImDrawList* dl,
	ImVec2 anchorTop,
	const std::string& title,
	const std::string& desc,
	const char* footer,
	ImU32 textCol,
	float alpha,
	float textScale,
	std::uint8_t label_h_align) {
	const ImFont* font = ImGui::GetFont();
	const float baseFs = ImGui::GetFontSize();
	const float fs = (std::max)(8.5f, baseFs * textScale);
	const float wrapW = std::clamp(520.f * textScale, 200.f, 780.f);
	const float pad = (std::max)(2.f, 3.f * textScale);
	const float blockGap = (std::max)(3.f, 4.f * textScale);
	const int a = static_cast<int>(std::clamp(alpha, 0.f, 1.f) * 235.f + 20.f);
	const float corner = (std::max)(1.f, 1.5f * textScale);

	ImVec2 szTitle(0.f, 0.f), szDesc(0.f, 0.f), szFoot(0.f, 0.f);
	const char* tBegin = title.c_str();
	const char* dBegin = desc.empty() ? nullptr : desc.c_str();
	if (font) {
		szTitle = font->CalcTextSizeA(fs, FLT_MAX, wrapW, tBegin, nullptr);
		if (dBegin)
			szDesc = font->CalcTextSizeA(fs, FLT_MAX, wrapW, dBegin, nullptr);
		if (footer && footer[0])
			szFoot = font->CalcTextSizeA(fs, FLT_MAX, 0.f, footer, nullptr);
	} else {
		szTitle = ImGui::CalcTextSize(tBegin);
		if (dBegin)
			szDesc = ImGui::CalcTextSize(dBegin);
		if (footer && footer[0])
			szFoot = ImGui::CalcTextSize(footer);
	}

	float mw = szTitle.x;
	if (dBegin && szDesc.x > mw)
		mw = szDesc.x;
	if (footer && footer[0] && szFoot.x > mw)
		mw = szFoot.x;
	const float totalW = mw + pad * 2.f;
	float totalH = pad + szTitle.y;
	if (dBegin)
		totalH += blockGap + szDesc.y;
	if (footer && footer[0])
		totalH += blockGap + szFoot.y;
	totalH += pad;

	ImVec2 p0;
	if (label_h_align == 1)
		p0 = ImVec2(anchorTop.x, anchorTop.y);
	else if (label_h_align == 2)
		p0 = ImVec2(anchorTop.x - totalW, anchorTop.y);
	else
		p0 = ImVec2(anchorTop.x - totalW * 0.5f, anchorTop.y);
	const ImVec2 p1(p0.x + totalW, p0.y + totalH);
	dl->AddRectFilled(p0, p1, IM_COL32(0, 0, 0, a), corner);

	float y = p0.y + pad;
	const float xText = p0.x + pad;
	if (font) {
		dl->AddText(font, fs, ImVec2(xText, y), textCol, tBegin, nullptr, wrapW);
		y += szTitle.y;
		if (dBegin) {
			y += blockGap;
			dl->AddText(font, fs, ImVec2(xText, y), textCol, dBegin, nullptr, wrapW);
			y += szDesc.y;
		}
		if (footer && footer[0]) {
			y += blockGap;
			dl->AddText(font, fs, ImVec2(xText, y), textCol, footer, nullptr, 0.f);
		}
	} else {
		dl->AddText(ImVec2(xText, y), textCol, tBegin);
		y += szTitle.y;
		if (dBegin) {
			y += blockGap;
			dl->AddText(ImVec2(xText, y), textCol, dBegin);
			y += szDesc.y;
		}
		if (footer && footer[0]) {
			y += blockGap;
			dl->AddText(ImVec2(xText, y), textCol, footer);
		}
	}
}

/** BOM / bozuk UTF-8 sonrasi ImGui'de ? gorunen karakterleri kirp. */
static std::string SanitizeLineupTitleForDisplay(std::string s) {
	while (s.size() >= 3 && (unsigned char)s[0] == 0xEFu && (unsigned char)s[1] == 0xBBu && (unsigned char)s[2] == 0xBFu)
		s.erase(0, 3);
	while (s.size() >= 3 && (unsigned char)s[0] == 0xEFu && (unsigned char)s[1] == 0xBFu && (unsigned char)s[2] == 0xBDu)
		s.erase(0, 3);
	while (!s.empty() && s.front() == '?')
		s.erase(0, 1);
	while (!s.empty() && (unsigned char)s.front() <= 32u && s.front() != '\n')
		s.erase(0, 1);
	return s;
}

/** CS2 practice: gokyuzu nisani — siyah kutu, ASCII bullet + hedef adi, altta throw; nokta tam aim'de. */
static std::string LineupNameFirstLine(const std::string& s) {
	const std::string cleaned = SanitizeLineupTitleForDisplay(s);
	size_t n = 0;
	while (n < cleaned.size() && (cleaned[n] == ' ' || cleaned[n] == '\t'))
		++n;
	const size_t end = cleaned.find('\n', n);
	std::string t = (end == std::string::npos) ? cleaned.substr(n) : cleaned.substr(n, end - n);
	while (!t.empty() && (t.back() == ' ' || t.back() == '\t'))
		t.pop_back();
	return t.empty() ? std::string("Lineup") : t;
}

static void DrawCs2StyleAimMarker(
	ImDrawList* dl,
	const ImVec2& aimScreen,
	float standBlend,
	float textScale,
	const std::string& nameFirstLine,
	const char* throwHow) {
	const float ar = std::clamp(standBlend, 0.f, 1.f);
	if (ar < 0.02f || !throwHow || !throwHow[0])
		return;
	const ImFont* font = ImGui::GetFont();
	const float fs = (std::max)(9.f, ImGui::GetFontSize() * textScale * 0.9f);
	const float wrap = std::clamp(400.f * textScale, 170.f, 540.f);
	const std::string line1 = std::string("* ") + nameFirstLine;
	const char* line2 = throwHow;
	const int textA = static_cast<int>(255.f * ar);
	const int bgA = static_cast<int>(215.f * ar);
	const ImU32 colText = IM_COL32(255, 255, 255, textA);
	const ImU32 colBg = IM_COL32(8, 8, 8, bgA);
	const float pad = (std::max)(5.f, 6.f * textScale);
	const float lineGap = (std::max)(2.f, 2.5f * textScale);
	ImVec2 sz1(0.f, 0.f), sz2(0.f, 0.f);
	if (font) {
		sz1 = font->CalcTextSizeA(fs, FLT_MAX, wrap, line1.c_str(), nullptr);
		sz2 = font->CalcTextSizeA(fs, FLT_MAX, wrap, line2, nullptr);
	} else {
		sz1 = ImGui::CalcTextSize(line1.c_str());
		sz2 = ImGui::CalcTextSize(line2);
	}
	const float mw = (std::max)(sz1.x, sz2.x);
	const float totalW = mw + pad * 2.f;
	const float totalH = pad + sz1.y + lineGap + sz2.y + pad;
	const float dotR = (std::max)(3.2f, 3.8f * textScale);
	const float gap = (std::max)(5.f, 6.f * textScale);
	const ImVec2 boxTopCenter(aimScreen.x, aimScreen.y - dotR - gap - totalH);
	const ImVec2 p0(boxTopCenter.x - totalW * 0.5f, boxTopCenter.y);
	const ImVec2 p1(p0.x + totalW, p0.y + totalH);
	const float corner = (std::max)(2.f, 2.5f * textScale);
	dl->AddRectFilled(p0, p1, colBg, corner);
	float y = p0.y + pad;
	const float xText = p0.x + pad;
	if (font) {
		dl->AddText(font, fs, ImVec2(xText, y), colText, line1.c_str(), nullptr, wrap);
		y += sz1.y + lineGap;
		dl->AddText(font, fs, ImVec2(xText, y), colText, line2, nullptr, wrap);
	} else {
		dl->AddText(ImVec2(xText, y), colText, line1.c_str());
		y += sz1.y + lineGap;
		dl->AddText(ImVec2(xText, y), colText, line2);
	}
	const int dotA = static_cast<int>(255.f * ar);
	dl->AddCircleFilled(aimScreen, dotR + 1.2f, IM_COL32(0, 0, 0, static_cast<int>(140.f * ar)));
	dl->AddCircleFilled(aimScreen, dotR, IM_COL32(255, 255, 255, dotA));
}

} // namespace

/** Workshop isimlerinde gecen jump / jt / crouch vb. -> HUD throw etiketi (embed 0 kalsa bile). */
static ExpectionalGrenadeThrowType ThrowTypeFromLineupName(const std::string& name, ExpectionalGrenadeThrowType base) noexcept {
	std::string s;
	s.reserve(name.size());
	for (unsigned char ch : name) {
		if (ch <= 127u)
			s.push_back(static_cast<char>(std::tolower(ch)));
		else
			s.push_back(static_cast<char>(ch));
	}
	const auto has = [&s](const char* t) -> bool { return s.find(t) != std::string::npos; };
	const bool crouch = has("crouch") || has("duck") || has("ctrl");
	const bool jumpWord = has("jump") || has("spacebar") || has("space +") || has("+jump") || has("jump+") ||
	    has("jump-throw") || has("jumpthrow");
	bool jtToken = false;
	for (size_t i = 0; i + 1 < s.size(); ++i) {
		if (s[i] != 'j' || s[i + 1] != 't')
			continue;
		if (i > 0 && std::isalnum(static_cast<unsigned char>(s[i - 1])))
			continue;
		if (i + 2 < s.size() && std::isalnum(static_cast<unsigned char>(s[i + 2])))
			continue;
		jtToken = true;
		break;
	}
	const bool jump = jumpWord || jtToken;
	const bool walkFwd = has("walk") || has("+w") || has(" w ") || has("forward");
	const bool runJump = (has("run") && jump) || (has("running") && (has("lmb") || has("throw")));

	if (runJump)
		return ExpectionalGrenadeThrowType::kRunJumpThrow;
	if (crouch && walkFwd && jump)
		return ExpectionalGrenadeThrowType::kCrouchWalkJumpThrow;
	if (crouch && jump)
		return ExpectionalGrenadeThrowType::kCrouchJumpThrow;
	if (crouch && has("throw"))
		return ExpectionalGrenadeThrowType::kCrouchThrow;
	if (walkFwd && jump)
		return ExpectionalGrenadeThrowType::kWalkJumpThrow;
	if (jump)
		return ExpectionalGrenadeThrowType::kJumpThrow;
	if ((has("walk") || has("+w") || has(" w ")) && has("throw") && !jump)
		return ExpectionalGrenadeThrowType::kWalkThrow;
	return base;
}

const char* ExpectionalGrenadeThrowTypeLabel(ExpectionalGrenadeThrowType t) noexcept {
	switch (t) {
	case ExpectionalGrenadeThrowType::kNormal: return "Throw";
	case ExpectionalGrenadeThrowType::kJumpThrow: return "Jump + Throw";
	case ExpectionalGrenadeThrowType::kWalkJumpThrow: return "W + Jump + Throw";
	case ExpectionalGrenadeThrowType::kCrouchThrow: return "Crouch + Throw";
	case ExpectionalGrenadeThrowType::kCrouchJumpThrow: return "Crouch + Jump + Throw";
	case ExpectionalGrenadeThrowType::kCrouchWalkJumpThrow: return "Crouch + W + Jump + Throw";
	case ExpectionalGrenadeThrowType::kWalkThrow: return "W + Throw";
	case ExpectionalGrenadeThrowType::kRunJumpThrow: return "Run + Jump + Throw";
	default: return "Throw";
	}
}

void ExpectionalGrenadeLineupRender(
	const view_matrix_t& vm,
	std::uintptr_t localPawn,
	const Vector3& localEyeWorld) {
	if (!Settings::Visuals::grenadeLineups || !localPawn)
		return;
	if (!g_loaded.load(std::memory_order_acquire)) {
		EnsureLineupsLoadAsync();
		return;
	}

	const std::uint16_t heldDef = ex_esp::ReadWeaponDefIndex(localPawn);
	const bool holdingNade = heldDef >= 43u && heldDef <= 48u;
	if (!holdingNade)
		return;
	const LineupNadeKind heldKind = HeldNadeKindFromDefIndex(heldDef);

	static std::string s_curMap;
	if (ex_sched::RunGrenadeLineupMapPollThisFrame() || s_curMap.empty())
		s_curMap = tri_loader::ReadMapName();
	std::string curMap = s_curMap;
	if (curMap.empty())
		curMap = "*";

	/**
	 * Render politikasi: kullanici Browser'dan aktif ettigi PAKETLERIN (max 2)
	 * tum lineup'larini ciz. Pack'in map'i suanki map ile uyusmuyorsa atla
	 * (wildcard / bos olan paketler her map'te gosterilir).
	 */
	std::vector<size_t> map_indices;
	{
		std::vector<std::string> active_pack_ids;
		{
			std::lock_guard<std::mutex> bk(g_browser_mtx);
			if (g_browser_active.empty())
				return;
			active_pack_ids = g_browser_active;
		}
		std::lock_guard<std::mutex> lk(g_lineup_mtx);
		for (const std::string& pid : active_pack_ids) {
			auto it = g_pack_by_id.find(pid);
			if (it == g_pack_by_id.end()) continue;
			const LineupPack& p = g_packs[it->second];
			if (!p.map.empty() && p.map != "*" && !LineupMapMatchesCurrent(p.map, curMap))
				continue;
			for (size_t idx : p.lineup_idx) {
				if (idx >= g_lineups.size()) continue;
				const std::string& lm = g_lineups[idx].map;
				if (!LineupMapMatchesCurrent(lm, curMap))
					continue;
				map_indices.push_back(idx);
			}
		}
	}
	if (map_indices.empty())
		return;

	ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
	/** Mesafe filtresi yok: ekranda gorunuyorsa (w2s) cizilir. */
	constexpr float kStandAlpha = 0.72f;
	/** Ayak alti yer halkasi — kucuk tutulur. */
	constexpr float kStandRingRadiusWorld = 5.25f;
	constexpr int kStandRingSegments = 32;
	constexpr float kStandRingLineThick = 0.85f;
	/** Aim: ayak XY mesafesi stand halkasi yaricapina gore (crosshair degil). */
	const float kFootAimHorizInner = kStandRingRadiusWorld + 8.f;
	const float kFootAimHorizOuter = kStandRingRadiusWorld + 36.f;
	constexpr float kAimRevealFootZMax = 56.f;
	/** Yazi: uzak = kucuk, yakin = buyuk. */
	constexpr float kLabelScaleRefDist = 880.f;
	constexpr float kLabelScaleMin = 0.34f;
	constexpr float kLabelScaleMax = 1.12f;

	const Vector3 feetWorld = ReadLocalPawnFeetWorld(localPawn);
	const float maxDraw = Settings::Visuals::grenadeLineupMaxDrawDistance;

	int drawn = 0;
	constexpr int kMaxLineupsPerFrame = 28;
	for (const size_t idx : map_indices) {
		if (drawn >= kMaxLineupsPerFrame)
			break;
		const LineupEntry& lu = g_lineups[idx];
		if (lu.nade_kind != heldKind)
			continue;

		const float distStand = Dist3(localEyeWorld, lu.stand);
		if (maxDraw > 0.5f && distStand > maxDraw)
			continue;

		const float labelScale = std::clamp(
			kLabelScaleRefDist / (std::max)(distStand, 65.f), kLabelScaleMin, kLabelScaleMax);

		std::string nameDesc;
		nameDesc.reserve(lu.name.size() + lu.desc.size() + 4);
		const auto appendFlat = [&nameDesc](const std::string& x) {
			for (unsigned char uc : x) {
				const char c = static_cast<char>(uc);
				if (c == '\r' || c == '\n' || c == '\t')
					nameDesc.push_back(' ');
				else
					nameDesc.push_back(c);
			}
		};
		appendFlat(lu.name);
		if (!lu.desc.empty()) {
			nameDesc.push_back(' ');
			appendFlat(lu.desc);
		}
		const ExpectionalGrenadeThrowType howType = ThrowTypeFromLineupName(nameDesc, lu.throw_type);
		const char* how = ExpectionalGrenadeThrowTypeLabel(howType);
		char subline[160]{};
		std::snprintf(subline, sizeof subline, "%s | %s", NadeKindHudLabel(lu.nade_kind), how);

		Vector3 spStand{};
		if (W2sOut(lu.stand, spStand, vm)) {
			const ImU32 ringCol = NadeKindImU32(lu.nade_kind, kStandAlpha);
			DrawFlatCircle(
				draw_list, vm, lu.stand, kStandRingRadiusWorld, kStandRingSegments, ringCol, kStandRingLineThick);
			const ImU32 txt = NadeKindImU32(lu.nade_kind, kStandAlpha);
			const std::string titleDraw = LineupNameFirstLine(lu.name);
			/** anchorTop: halkanin ekran merkezinin hemen alti; kutu asagi dogru uzanir. */
			DrawLineupHud(
				draw_list,
				ImVec2(spStand.x, spStand.y + 13.f * labelScale),
				titleDraw,
				std::string(),
				subline,
				txt,
				kStandAlpha,
				labelScale,
				0);
		}

		Vector3 screenAim{};
		if (W2sOut(lu.target, screenAim, vm)) {
			const float horizFeet = std::hypot(feetWorld.x - lu.stand.x, feetWorld.y - lu.stand.y);
			const float dzFeet = std::fabs(feetWorld.z - lu.stand.z);
			float standBlend = 0.f;
			if (dzFeet <= kAimRevealFootZMax)
				standBlend = 1.f - Smoothstep01(kFootAimHorizInner, kFootAimHorizOuter, horizFeet);
			standBlend = std::clamp(standBlend, 0.f, 1.f);
			standBlend = SmoothStandBlend(LineupBlendKey(lu.stand), standBlend);
			if (standBlend > 0.02f) {
				const std::string skyTitle = LineupNameFirstLine(lu.name);
				DrawCs2StyleAimMarker(
					draw_list,
					ImVec2(screenAim.x, screenAim.y),
					standBlend,
					labelScale,
					skyTitle,
					how);
			}
		}
		++drawn;
	}
}

/* ============================================================================
 * Lineup Browser API: PAKET (her .txt dosyasi = 1 paket) bazinda calisir.
 * Sol: map listesi. Sag: o map'e ait paketler. Aktif: max 2 paket.
 * ============================================================================ */

std::vector<std::string> ExpectionalLineupBrowserMapList()
{
	EnsureLineupsLoadAsync();
	std::vector<std::string> maps;
	{
		std::lock_guard<std::mutex> lk(g_lineup_mtx);
		maps.reserve(g_packs_by_map.size());
		for (const auto& kv : g_packs_by_map) {
			if (kv.second.empty()) continue;
			maps.push_back(kv.first.empty() ? std::string("*") : kv.first);
		}
	}
	std::sort(maps.begin(), maps.end(), [](const std::string& a, const std::string& b) {
		const bool ca = (a.rfind("de_", 0) == 0 || a.rfind("cs_", 0) == 0 || a.rfind("ar_", 0) == 0);
		const bool cb = (b.rfind("de_", 0) == 0 || b.rfind("cs_", 0) == 0 || b.rfind("ar_", 0) == 0);
		if (ca != cb) return ca && !cb;
		return a < b;
	});
	return maps;
}

std::vector<ExpectionalLineupBrowserPack> ExpectionalLineupBrowserPacksForMap(const std::string& map)
{
	std::vector<ExpectionalLineupBrowserPack> out;
	std::lock_guard<std::mutex> lk(g_lineup_mtx);
	auto it = g_packs_by_map.find(map);
	if (it == g_packs_by_map.end()) {
		if (map == "*") {
			it = g_packs_by_map.find(std::string{});
			if (it == g_packs_by_map.end()) return out;
		} else {
			return out;
		}
	}
	out.reserve(it->second.size());
	for (size_t pi : it->second) {
		if (pi >= g_packs.size()) continue;
		const LineupPack& p = g_packs[pi];
		ExpectionalLineupBrowserPack r;
		r.id = p.id;
		r.title = p.title;
		r.map = p.map;
		r.lineup_count = p.lineup_idx.size();
		out.push_back(std::move(r));
	}
	std::sort(out.begin(), out.end(),
	    [](const ExpectionalLineupBrowserPack& a, const ExpectionalLineupBrowserPack& b) {
		    return a.title < b.title;
	    });
	out.erase(std::unique(out.begin(), out.end(),
	    [](const ExpectionalLineupBrowserPack& a, const ExpectionalLineupBrowserPack& b) {
		    return a.title == b.title;
	    }), out.end());
	return out;
}

void ExpectionalLineupBrowserStageToggle(const std::string& pack_id)
{
	if (pack_id.empty()) return;
	std::lock_guard<std::mutex> lk(g_browser_mtx);
	auto it = std::find(g_browser_staged.begin(), g_browser_staged.end(), pack_id);
	if (it != g_browser_staged.end())
		g_browser_staged.erase(it);
	else
		g_browser_staged.push_back(pack_id);
}

bool ExpectionalLineupBrowserStageContains(const std::string& pack_id)
{
	std::lock_guard<std::mutex> lk(g_browser_mtx);
	return std::find(g_browser_staged.begin(), g_browser_staged.end(), pack_id) != g_browser_staged.end();
}

std::size_t ExpectionalLineupBrowserStageCount()
{
	std::lock_guard<std::mutex> lk(g_browser_mtx);
	return g_browser_staged.size();
}

void ExpectionalLineupBrowserStageClear()
{
	std::lock_guard<std::mutex> lk(g_browser_mtx);
	g_browser_staged.clear();
}

void ExpectionalLineupBrowserApply()
{
	std::lock_guard<std::mutex> lk(g_browser_mtx);
	g_browser_active = g_browser_staged;
}

std::size_t ExpectionalLineupBrowserActiveCount()
{
	std::lock_guard<std::mutex> lk(g_browser_mtx);
	return g_browser_active.size();
}

bool ExpectionalLineupBrowserActiveContains(const std::string& pack_id)
{
	std::lock_guard<std::mutex> lk(g_browser_mtx);
	return std::find(g_browser_active.begin(), g_browser_active.end(), pack_id) != g_browser_active.end();
}

bool ExpectionalLineupBrowserActiveAdd(const std::string& pack_id)
{
	if (pack_id.empty()) return false;
	std::lock_guard<std::mutex> lk(g_browser_mtx);
	if (std::find(g_browser_active.begin(), g_browser_active.end(), pack_id) != g_browser_active.end())
		return true;
	g_browser_active.push_back(pack_id);
	return true;
}

void ExpectionalLineupBrowserActiveRemove(const std::string& pack_id)
{
	std::lock_guard<std::mutex> lk(g_browser_mtx);
	auto it = std::find(g_browser_active.begin(), g_browser_active.end(), pack_id);
	if (it != g_browser_active.end())
		g_browser_active.erase(it);
}

std::size_t ExpectionalLineupBrowserActiveCountForMap(const std::string& map)
{
	std::lock_guard<std::mutex> bk(g_browser_mtx);
	std::lock_guard<std::mutex> lk(g_lineup_mtx);
	std::size_t n = 0;
	for (const std::string& pid : g_browser_active) {
		auto it = g_pack_by_id.find(pid);
		if (it == g_pack_by_id.end()) continue;
		const std::string& pm = g_packs[it->second].map;
		if (pm == map) ++n;
	}
	return n;
}

std::vector<std::string> ExpectionalLineupBrowserActiveIdsSnapshot()
{
	std::lock_guard<std::mutex> lk(g_browser_mtx);
	return g_browser_active;
}

void ExpectionalLineupBrowserActiveSetFromConfig(const std::vector<std::string>& ids)
{
	std::lock_guard<std::mutex> lk(g_browser_mtx);
	g_browser_active = ids;
	/** Mukerrer kayitlari at. */
	std::vector<std::string> uniq;
	uniq.reserve(g_browser_active.size());
	for (const std::string& s : g_browser_active) {
		if (s.empty()) continue;
		if (std::find(uniq.begin(), uniq.end(), s) == uniq.end())
			uniq.push_back(s);
	}
	g_browser_active = std::move(uniq);
}

void ExpectionalLineupBrowserClearActive()
{
	std::lock_guard<std::mutex> lk(g_browser_mtx);
	g_browser_active.clear();
	g_browser_staged.clear();
}

void ExpectionalLineupBrowserRefreshWorkshop()
{
	/**
	 * Workshop'u yeniden tara — kullanicinin aktif paketlerini KAYBETMEDEN.
	 * pack_id'ler tam dosya yolu olduklari icin, ayni dosya hala diskte varsa
	 * yeniden parse sonrasinda ayni id ile g_pack_by_id'de bulunur ve render
	 * normal akisinda devam eder. Dosya silindiyse ID dormant kalir (zarari yok).
	 */
	g_loaded = false;
	g_load_started = false;
	EnsureLineupsLoadAsync();
}

bool ExpectionalLineupsLoaded() { return g_loaded.load(std::memory_order_acquire); }

std::size_t ExpectionalLineupsTotalCount()
{
	std::lock_guard<std::mutex> lk(g_lineup_mtx);
	return g_lineups.size();
}

std::size_t ExpectionalLineupPacksTotalCount()
{
	std::lock_guard<std::mutex> lk(g_lineup_mtx);
	return g_packs.size();
}
