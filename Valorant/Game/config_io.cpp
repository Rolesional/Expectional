#include "config_io.hpp"
#include "expectional_paths.hpp"
#include "expectional_winio.hpp"
#include "globals.hpp"
#include "grenade_lineup.hpp"
#include "weapon_runtime.hpp"
#include "../Overlay/menu.hpp"

#include <Windows.h>
#include <ShlObj.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <Shellapi.h>

#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "Shlwapi.lib")
#include "Protection/vxlang_per_tu.hpp"

namespace fs = std::filesystem;

static fs::path ExpectionalRootDirPath() {
	const std::wstring root = ExpectionalPaths::GlobalExpectionalRootWide();
	if (root.empty())
		return fs::path(L".") / L"Expectional";
	(void)ExpectionalWinIO::EnsureDirectoryWide(root);
	return fs::path(root);
}

static fs::path ConfigDirPath() {
	const std::wstring dirW = ExpectionalPaths::GlobalConfigDirWide();
	if (dirW.empty())
		return fs::path(L".") / L"Expectional" / L"configs";
	(void)ExpectionalWinIO::EnsureDirectoryWide(dirW);
	return fs::path(dirW);
}

static bool Win32PathExists(const std::wstring& path)
{
	return ExpectionalWinIO::FileExistsWide(path);
}

static bool Win32EnsureParentDir(const std::wstring& filePath)
{
	const size_t slash = filePath.find_last_of(L"\\/");
	if (slash == std::wstring::npos)
		return true;
	return ExpectionalWinIO::EnsureDirectoryWide(filePath.substr(0, slash));
}

static bool Win32WriteAllBytes(const std::wstring& path, const void* data, DWORD size)
{
	return ExpectionalWinIO::WriteAllBytesWide(path, data, size);
}

static bool Win32ReadAllBytes(const std::wstring& path, std::string& out)
{
	std::vector<uint8_t> bytes;
	if (!ExpectionalWinIO::ReadAllBytesWide(path, bytes))
		return false;
	out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	return !out.empty();
}

static fs::path LineupsDirPath() {
	const std::wstring dirW = ExpectionalPaths::GlobalLineupsDirWide();
	if (dirW.empty())
		return ExpectionalRootDirPath() / L"lineups";
	(void)ExpectionalWinIO::EnsureDirectoryWide(dirW);
	return fs::path(dirW);
}

std::string ExpectionalConfigDirUtf8() {
	try {
		return ConfigDirPath().string();
	} catch (...) {
		return {};
	}
}

static std::string WideToUtf8(const std::wstring& w) {
	if (w.empty())
		return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1)
		return {};
	std::string u8(static_cast<size_t>(n - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, u8.data(), n, nullptr, nullptr);
	return u8;
}

std::string ExpectionalLineupsDirUtf8() {
	try {
		return WideToUtf8(LineupsDirPath().wstring());
	} catch (...) {
		return {};
	}
}

std::wstring ExpectionalLineupsDirWide() {
	try {
		return LineupsDirPath().wstring();
	} catch (...) {
		return L"";
	}
}

static std::wstring Utf8PathToWide(const std::string& u8) {
	if (u8.empty())
		return L"";
	const int n = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, nullptr, 0);
	if (n <= 1)
		return L"";
	std::wstring w(static_cast<size_t>(n - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, w.data(), n);
	return w;
}

void ExpectionalShellOpenUtf8Path(const char* utf8_path) {
	if (!utf8_path || !utf8_path[0])
		return;
	const std::wstring w = Utf8PathToWide(std::string(utf8_path));
	if (w.empty())
		return;
	/** "open" ile Windows varsayilan iliskilendirmesini kullanir (klasor = Explorer, .txt = editor). */
	ShellExecuteW(nullptr, L"open", w.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static constexpr char kLineupsReadmeEn[] =
	"Grenade lineups (Expectional)\r\n"
	"\r\n"
	"This folder is scanned on startup for Counter-Strike 2 workshop grenade guide files in KV3 text form "
	"(files containing MapAnnotationNode blocks). Supported extension: .txt\r\n\r\n"
	"How to obtain lineup files\r\n"
	"----------------------------\r\n"
	"1) Open Steam and go to the Counter-Strike 2 Workshop.\r\n"
	"2) Filter guide and cs2 then search for lineup guides for the maps you want.\r\n"
	"3) Subscribe to the workshop items you like. Steam will download them to your machine.\r\n\r\n"
	"Where Steam stores subscribed workshop content (Windows)\r\n"
	"--------------------------------------------------------\r\n"
	"By default, files are under your Steam library folder, for example:\r\n\r\n"
	"    <SteamLibrary>\\steamapps\\workshop\\content\\730\\<WorkshopItemID>\\\r\n\r\n"
	"730 is the Counter-Strike 2 app context used for many workshop items. Inside the numbered folder(s).\r\n\r\n"
	"What to do in Expectional\r\n"
	"-------------------------\r\n"
	"Copy the relevant .txt file(s) into this folder:\r\n\r\n"
	"    Documents\\Expectional\\lineups\\\r\n\r\n"
	"Then fully restart the cheat client so lineups are read again from disk. Embedded defaults (if any) are "
	"still loaded first; files in this folder are appended.\r\n\r\n"
	"In-game, enable \"Grenade lineups\" under Visuals. Hold the matching grenade to see lineup markers for "
	"the active map.\r\n";

void ExpectionalEnsureLineupsDirAndReadme() {
	const std::wstring dirW = ExpectionalPaths::GlobalLineupsDirWide();
	(void)ExpectionalWinIO::EnsureDirectoryWide(dirW);
	const std::wstring readme = dirW + L"\\readme.txt";
	(void)ExpectionalWinIO::WriteAllBytesWide(readme, kLineupsReadmeEn, sizeof(kLineupsReadmeEn) - 1);
}

void ExpectionalShellOpenLineupsFolder() {
	ExpectionalEnsureLineupsDirAndReadme();
	const std::wstring p = ExpectionalLineupsDirWide();
	if (p.empty())
		return;
	ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void ExpectionalShellOpenLineupsReadme() {
	ExpectionalEnsureLineupsDirAndReadme();
	const fs::path readme = LineupsDirPath() / L"readme.txt";
	ShellExecuteW(nullptr, L"open", readme.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::vector<std::string> ExpectionalConfigList() {
	std::vector<std::string> out;
	auto appendFromDir = [&](const std::wstring& dirW) {
		if (dirW.empty())
			return;
		const std::wstring pattern = dirW + L"\\*.cfg";
		WIN32_FIND_DATAW fd{};
		HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
		if (hFind == INVALID_HANDLE_VALUE)
			return;
		do {
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			const std::string stem = WideToUtf8(fd.cFileName);
			const size_t dot = stem.rfind('.');
			const std::string name = (dot == std::string::npos) ? stem : stem.substr(0, dot);
			if (!name.empty() &&
			    std::find(out.begin(), out.end(), name) == out.end())
				out.push_back(name);
		} while (FindNextFileW(hFind, &fd));
		FindClose(hFind);
	};
	appendFromDir(ConfigDirPath().wstring());
	appendFromDir(ExpectionalPaths::GlobalConfigDirWide());
	appendFromDir(ExpectionalPaths::UwpNotepadConfigDirWide());
	std::sort(out.begin(), out.end());
	return out;
}

static fs::path PathForName(const char* name_no_ext) {
	std::string n = name_no_ext ? name_no_ext : "";
	while (!n.empty() && (n.back() == ' ' || n.back() == '.')) n.pop_back();
	for (char& c : n) {
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
			c = '_';
	}
	if (n.empty())
		n = "default";
	return ConfigDirPath() / (Utf8PathToWide(n) + L".cfg");
}

/** Load: in-process config dir, sonra global + UWP bridge fallback. */
static fs::path ResolveConfigFilePath(const char* name_no_ext)
{
	const fs::path primary = PathForName(name_no_ext);
	const std::wstring primaryW = primary.wstring();
	if (Win32PathExists(primaryW))
		return primary;

	const std::wstring file = primary.filename().wstring();
	for (const std::wstring& dir : {
	         ExpectionalPaths::GlobalConfigDirWide(),
	         ExpectionalPaths::UwpNotepadConfigDirWide() }) {
		if (dir.empty())
			continue;
		const std::wstring candidate = dir + L"\\" + file;
		if (Win32PathExists(candidate))
			return fs::path(candidate);
	}
	return primary;
}

static void Trim(std::string& s) {
	while (!s.empty() && (unsigned char)s.front() <= ' ') s.erase(0, 1);
	while (!s.empty() && (unsigned char)s.back() <= ' ') s.pop_back();
}

static bool ParseBool(std::string v) {
	Trim(v);
	return v == "1" || v == "true" || v == "True" || v == "TRUE";
}

static void WriteBool(std::ostream& o, const char* k, bool v) { o << k << '=' << (v ? 1 : 0) << '\n'; }
static void WriteInt(std::ostream& o, const char* k, int v) { o << k << '=' << v << '\n'; }
static void WriteFloat(std::ostream& o, const char* k, float v) { o << k << '=' << v << '\n'; }

static void WriteRgb3(std::ostream& o, const char* key, const float* v) {
	o << "EspUi." << key << '=' << v[0] << ',' << v[1] << ',' << v[2] << '\n';
}
static void WriteRgb4(std::ostream& o, const char* key, const float* v) {
	o << "EspUi." << key << '=' << v[0] << ',' << v[1] << ',' << v[2] << ',' << v[3] << '\n';
}

static void WriteEspUiColors(std::ostream& f) {
	WriteRgb3(f, "skel_col", EspUiColors::skel_col);
	WriteRgb3(f, "skel_vis_col", EspUiColors::skel_vis_col);
	WriteRgb3(f, "espcol", EspUiColors::espcol);
	WriteRgb3(f, "vis_espcol", EspUiColors::vis_espcol);
	WriteRgb3(f, "esp_fill_col", EspUiColors::esp_fill_col);
	WriteRgb3(f, "esp_fill2_col", EspUiColors::esp_fill2_col);
	WriteRgb3(f, "bomb_esp_col", EspUiColors::bomb_esp_col);
	WriteRgb3(f, "grenade_traj_line", EspUiColors::grenade_traj_line);
	WriteRgb3(f, "grenade_traj_marker", EspUiColors::grenade_traj_marker);
	WriteRgb3(f, "name_esp", EspUiColors::name_esp);
	WriteRgb3(f, "name_vis_esp", EspUiColors::name_vis_esp);
	WriteRgb3(f, "weapon_esp", EspUiColors::weapon_esp);
	WriteRgb3(f, "weapon_vis_esp", EspUiColors::weapon_vis_esp);
	WriteRgb3(f, "distance_esp", EspUiColors::distance_esp);
	WriteRgb3(f, "distance_vis_esp", EspUiColors::distance_vis_esp);
	WriteRgb3(f, "bomb_carrier_tag", EspUiColors::bomb_carrier_tag);
	WriteRgb3(f, "armor_bar", EspUiColors::armor_bar);
	WriteRgb3(f, "scoped_label", EspUiColors::scoped_label);
	WriteRgb3(f, "blind_label", EspUiColors::blind_label);
	WriteRgb3(f, "eye_ray", EspUiColors::eye_ray);
	WriteRgb3(f, "sniper_crosshair", EspUiColors::sniper_crosshair);
	WriteRgb3(f, "snapline_esp", EspUiColors::snapline_esp);
	WriteRgb3(f, "snapline_vis_los", EspUiColors::snapline_vis_los);
	WriteRgb3(f, "ammo_text_esp", EspUiColors::ammo_text_esp);
	WriteRgb3(f, "dropped_weapon_esp", EspUiColors::dropped_weapon_esp);
	WriteRgb3(f, "health_bar_high", EspUiColors::health_bar_high);
	WriteRgb3(f, "health_bar_low", EspUiColors::health_bar_low);
	WriteRgb3(f, "health_bar_value_text", EspUiColors::health_bar_value_text);
	WriteRgb3(f, "health_text_damaged", EspUiColors::health_text_damaged);
	WriteRgb3(f, "health_text_full", EspUiColors::health_text_full);
	WriteRgb3(f, "ammo_bar_hi", EspUiColors::ammo_bar_hi);
	WriteRgb3(f, "ammo_bar_lo", EspUiColors::ammo_bar_lo);
	WriteRgb4(f, "head_circle_fill", EspUiColors::head_circle_fill);
	WriteRgb4(f, "head_circle_vis_fill", EspUiColors::head_circle_vis_fill);
	WriteRgb3(f, "aim_fov_circle", EspUiColors::aim_fov_circle);
	WriteRgb3(f, "pen_crosshair_yes", EspUiColors::pen_crosshair_yes);
	WriteRgb3(f, "pen_crosshair_no", EspUiColors::pen_crosshair_no);
	WriteRgb3(f, "radar_team", EspUiColors::radar_team);
	WriteRgb3(f, "radar_enemy", EspUiColors::radar_enemy);
	WriteRgb3(f, "radar_local", EspUiColors::radar_local);
}

static bool ParseCsvFloats(const std::string& val, float* out, int n) {
	std::istringstream ss(val);
	std::string part;
	for (int i = 0; i < n; ++i) {
		if (!std::getline(ss, part, ','))
			return false;
		Trim(part);
		if (part.empty())
			return false;
		out[i] = std::stof(part);
	}
	return true;
}

bool ExpectionalConfigSave(const char* name_no_ext) {
	WeaponRuntime::EditorPrepareGlobalsForConfigSave();
	std::ostringstream f;
	f << "expectional_cfg 4\n";
	WriteBool(f, "aimbot.aimbot", Settings::aimbot::aimbot);
	WriteFloat(f, "aimbot.aim_fov", Settings::aimbot::aim_fov);
	WriteFloat(f, "aimbot.aim_fov_min", Settings::aimbot::aim_fov_min);
	WriteFloat(f, "aimbot.smooth", Settings::aimbot::smooth);
	WriteInt(f, "aimbot.aim_delay_ms", Settings::aimbot::aim_delay_ms);
	WriteBool(f, "aimbot.aim_humanize", Settings::aimbot::aim_humanize);
	WriteInt(f, "aimbot.aim_humanize_strength", Settings::aimbot::aim_humanize_strength);
	WriteBool(f, "aimbot.aim_visible_only", Settings::aimbot::aim_visible_only);
	WriteBool(f, "aimbot.aim_autowall", Settings::aimbot::aim_autowall);
	WriteFloat(f, "aimbot.aim_min_damage", Settings::aimbot::aim_min_damage);
	WriteInt(f, "aimbot.aim_key_mode", Settings::aimbot::aim_key_mode);
	WriteBool(f, "aimbot.trigger_reaction_enabled", Settings::aimbot::trigger_reaction_enabled);
	WriteFloat(f, "aimbot.trigger_delay_min", Settings::aimbot::trigger_delay_min);
	WriteFloat(f, "aimbot.trigger_delay_max", Settings::aimbot::trigger_delay_max);
	WriteFloat(f, "aimbot.trigger_shot_cooldown", Settings::aimbot::trigger_shot_cooldown);
	WriteFloat(f, "aimbot.trigger_ttd_delay_ms", Settings::aimbot::trigger_ttd_delay_ms);
	WriteBool(f, "aimbot.trigger_visible_only", Settings::aimbot::trigger_visible_only);
	WriteBool(f, "aimbot.trigger_autowall", Settings::aimbot::trigger_autowall);
	WriteFloat(f, "aimbot.trigger_min_damage", Settings::aimbot::trigger_min_damage);
	WriteBool(f, "aimbot.trigger_head_only", Settings::aimbot::trigger_head_only);
	WriteInt(f, "aimbot.trigger_key_mode", Settings::aimbot::trigger_key_mode);
	WriteBool(f, "aimbot.trigger_always", Settings::aimbot::trigger_key_mode == 2);
	WriteBool(f, "aimbot.trigger_stopped_only", Settings::aimbot::trigger_stopped_only);
	WriteBool(f, "aimbot.trigger_ignore_flash", Settings::aimbot::trigger_ignore_flash);
	WriteBool(f, "aimbot.rcs_enabled", Settings::aimbot::rcs_enabled);
	WriteBool(f, "aimbot.rcs_standalone", Settings::aimbot::rcs_standalone);
	WriteInt(f, "aimbot.rcs_after_bullet", Settings::aimbot::rcs_after_bullet);
	WriteFloat(f, "aimbot.rcs_scale_pct", Settings::aimbot::rcs_scale_pct);
	WriteFloat(f, "aimbot.rcs_sens_mult", Settings::aimbot::rcs_sens_mult);
	WriteFloat(f, "aimbot.rcs_smooth", Settings::aimbot::rcs_smooth);
	WriteFloat(f, "aimbot.rcs_aim_blend_pct", Settings::aimbot::rcs_aim_blend_pct);
	WriteBool(f, "aimbot.fov_circle", Settings::aimbot::fov_circle);
	WriteBool(f, "aimbot.crosshair", Settings::aimbot::crosshair);
	WriteBool(f, "aimbot.penetration_crosshair", Settings::aimbot::penetration_crosshair);
	WriteBool(f, "aimbot.triggerbot", Settings::aimbot::triggerbot);
	WriteInt(f, "aimbot.hitbox_mask", static_cast<int>(Settings::aimbot::hitbox_mask));

	WriteBool(f, "Visuals.enablePlayerEsp", Settings::Visuals::enablePlayerEsp);
	WriteInt(f, "Visuals.boxMode", Settings::Visuals::boxMode);
	WriteBool(f, "Visuals.bSnaplines", Settings::Visuals::bSnaplines);
	WriteBool(f, "Visuals.bDistance", Settings::Visuals::bDistance);
	WriteBool(f, "Visuals.bBox", Settings::Visuals::bBox);
	WriteBool(f, "Visuals.healthBar", Settings::Visuals::healthBar);
	WriteBool(f, "Visuals.healthText", Settings::Visuals::healthText);
	WriteBool(f, "Visuals.headcircle", Settings::Visuals::headcircle);
	WriteBool(f, "Visuals.bones", Settings::Visuals::bones);
	WriteBool(f, "Visuals.glow", Settings::Visuals::glow);
	WriteBool(f, "Visuals.distance", Settings::Visuals::distance);
	WriteBool(f, "Visuals.armor", Settings::Visuals::armor);
	WriteBool(f, "Visuals.names", Settings::Visuals::names);
	WriteBool(f, "Visuals.weaponEsp", Settings::Visuals::weaponEsp);
	WriteBool(f, "Visuals.weaponEspIcon", Settings::Visuals::weaponEspIcon);
	WriteBool(f, "Visuals.enemiesOnly", Settings::Visuals::enemiesOnly);
	WriteBool(f, "Visuals.esp_visible_only", Settings::Visuals::esp_visible_only);
	WriteBool(f, "Visuals.noflash", Settings::Visuals::noflash);
	WriteBool(f, "Visuals.nohands", Settings::Visuals::nohands);
	WriteBool(f, "Visuals.chams", Settings::Visuals::chams);
	WriteBool(f, "Visuals.ragdoll", Settings::Visuals::ragdoll);
	WriteBool(f, "Visuals.nightmode", Settings::Visuals::nightmode);
	WriteBool(f, "Visuals.box", Settings::Visuals::box);
	WriteFloat(f, "Visuals.BoxWidth", Settings::Visuals::BoxWidth);
	WriteFloat(f, "Visuals.boxRounding", Settings::Visuals::boxRounding);
	WriteBool(f, "Visuals.filledBox", Settings::Visuals::filledBox);
	WriteBool(f, "Visuals.filledGradient", Settings::Visuals::filledGradient);
	WriteBool(f, "Visuals.filledVisBox", Settings::Visuals::filledVisBox);
	WriteInt(f, "Visuals.snaplineMode", Settings::Visuals::snaplineMode);
	WriteBool(f, "Visuals.eyeRay", Settings::Visuals::eyeRay);
	WriteBool(f, "Visuals.ammoBar", Settings::Visuals::ammoBar);
	WriteBool(f, "Visuals.ammoText", Settings::Visuals::ammoText);
	WriteBool(f, "Visuals.bombWorldEsp", Settings::Visuals::bombWorldEsp);
	WriteBool(f, "Visuals.worldGrenades", Settings::Visuals::worldGrenades);
	WriteBool(f, "Visuals.worldGrenadeIcons", Settings::Visuals::worldGrenadeIcons);
	WriteBool(f, "Visuals.worldInfernoHull", Settings::Visuals::worldInfernoHull);
	WriteBool(f, "Visuals.grenadeEspLocalOnly", Settings::Visuals::grenadeEspLocalOnly);
	WriteBool(f, "Visuals.grenadeLineups", Settings::Visuals::grenadeLineups);
	WriteFloat(f, "Visuals.grenadeLineupMaxDrawDistance", Settings::Visuals::grenadeLineupMaxDrawDistance);
	WriteBool(f, "Visuals.bombCarrierEsp", Settings::Visuals::bombCarrierEsp);
	WriteBool(f, "Visuals.showScoped", Settings::Visuals::showScoped);
	WriteBool(f, "Visuals.showBlind", Settings::Visuals::showBlind);
	WriteBool(f, "Visuals.blindHideEsp", Settings::Visuals::blindHideEsp);
	WriteBool(f, "Visuals.awpCrosshair", Settings::Visuals::awpCrosshair);
	WriteBool(f, "Visuals.droppedWeaponEsp", Settings::Visuals::droppedWeaponEsp);
	WriteBool(f, "Visuals.droppedWeaponText", Settings::Visuals::droppedWeaponText);
	WriteBool(f, "Visuals.droppedWeaponIcons", Settings::Visuals::droppedWeaponIcons);

	WriteBool(f, "misc.bhop", Settings::misc::bhop);
	WriteInt(f, "misc.bhop_delay_ms", Settings::misc::bhop_delay_ms);
	WriteBool(f, "misc.keybind_list_window", Settings::misc::keybind_list_window);
	WriteBool(f, "misc.radar", Settings::misc::radar);
	WriteBool(f, "misc.water", Settings::misc::water);
	WriteBool(f, "misc.waterShowOverlayFps", Settings::misc::waterShowOverlayFps);
	WriteBool(f, "misc.waterShowGameFps", Settings::misc::waterShowGameFps);
	WriteBool(f, "misc.waterShowPing", Settings::misc::waterShowPing);
	WriteInt(f, "misc.hit_sound", Settings::misc::hit_sound);
	WriteBool(f, "misc.hit_marker", Settings::misc::hit_marker);
	WriteBool(f, "misc.spectatorList", Settings::misc::spectatorList);
	WriteBool(f, "misc.bombTimer", Settings::misc::bombTimer);
	WriteBool(f, "misc.fovChanger", Settings::misc::fovChanger);
	WriteFloat(f, "misc.fov", Settings::misc::fov);
	WriteBool(f, "misc.cloudRadar", Settings::misc::cloudRadar);
	WriteBool(f, "misc.radarWindow", Settings::misc::radarWindow);
	WriteBool(f, "misc.radarWindowDebugLog", Settings::misc::radarWindowDebugLog);
	WriteBool(f, "misc.radarWindow43", Settings::misc::radarWindow43);
	WriteFloat(f, "misc.radarWindowMapPx", Settings::misc::radarWindowMapPx);
	WriteBool(f, "misc.radarWindowFollowLocal", Settings::misc::radarWindowFollowLocal);
	WriteFloat(f, "misc.radarWindowFollowZoom", Settings::misc::radarWindowFollowZoom);
	WriteFloat(f, "misc.radarWindowBlipScale", Settings::misc::radarWindowBlipScale);
	WriteBool(f, "misc.radarWindowRotateWithView", Settings::misc::radarWindowRotateWithView);
	WriteBool(f, "misc.radarWindowHudMatch", Settings::misc::radarWindowHudMatch);
	WriteBool(f, "misc.radarWindowGameMapTex", Settings::misc::radarWindowGameMapTex);
	WriteBool(f, "misc.radarWindowHideMapImage", Settings::misc::radarWindowHideMapImage);
	WriteInt(f, "misc.radarWindowTransparency", Settings::misc::radarWindowTransparency);
	WriteBool(f, "misc.rank_reveal_window", Settings::misc::rank_reveal_window);
	WriteBool(f, "misc.rank_reveal_inventory_enabled", Settings::misc::rank_reveal_inventory_enabled);
	WriteBool(f, "misc.debug_visible_check", Settings::misc::debug_visible_check);
	WriteBool(f, "misc.autosave_config", Settings::misc::autosave_config);
	WriteBool(f, "misc.obsBypass", Settings::misc::obsBypass);
	WriteBool(f, "misc.overlayCustomFps", Settings::misc::overlayCustomFps);
	WriteInt(f, "misc.overlayCustomFpsValue", Settings::misc::overlayCustomFpsValue);
	f << "misc.cloudRadarPublishUrl=" << Settings::misc::cloudRadarPublishUrl << '\n';
	f << "misc.cloudRadarViewerBase=" << Settings::misc::cloudRadarViewerBase << '\n';

	/** Aktif grenade lineup paketleri — birden cok satir yazilir, her satir tek pack_id (tam yol). */
	{
		const std::vector<std::string> active_ids = ExpectionalLineupBrowserActiveIdsSnapshot();
		for (const std::string& id : active_ids) {
			if (id.empty()) continue;
			f << "lineups.active_pack=" << id << '\n';
		}
	}

	WriteEspUiColors(f);

	WriteInt(f, "hotkeys.aimkey", hotkeys::aimkey.load());
	WriteInt(f, "hotkeys.triggerkey", hotkeys::triggerkey.load());
	WriteInt(f, "hotkeys.menukey", hotkeys::menukey.load());
	WeaponRuntime::SerializeWeaponProfiles(f);
	const std::string body = f.str();
	const std::wstring path = PathForName(name_no_ext).wstring();
	return Win32WriteAllBytes(path, body.data(), static_cast<DWORD>(body.size()));
}

bool ExpectionalSaveActiveConfig() {
	return ExpectionalConfigSave(ExpectionalActiveCfgName);
}

static void SetCStr(char* dest, size_t cap, const std::string& v) {
	if (!dest || cap == 0) return;
	std::memset(dest, 0, cap);
	for (size_t i = 0; i + 1 < cap && i < v.size(); ++i)
		dest[i] = v[i];
}

static bool ApplyMiscWatermarkKey(const std::string& key, const std::string& val) {
	if (key == "misc.water") {
		Settings::misc::water = ParseBool(val);
		return true;
	}
	if (key == "misc.waterShowOverlayFps") {
		Settings::misc::waterShowOverlayFps = ParseBool(val);
		return true;
	}
	if (key == "misc.waterShowGameFps") {
		Settings::misc::waterShowGameFps = ParseBool(val);
		return true;
	}
	if (key == "misc.waterShowPing") {
		Settings::misc::waterShowPing = ParseBool(val);
		return true;
	}
	return false;
}

static void ApplyEspUiColor(const std::string& sub, const std::string& val) {
	float t[4]{};
	auto try3 = [&](const char* name, float* d) -> bool {
		if (sub != name) return false;
		if (ParseCsvFloats(val, t, 3)) std::memcpy(d, t, 3 * sizeof(float));
		return true;
	};
	auto try4 = [&](const char* name, float* d) -> bool {
		if (sub != name) return false;
		if (ParseCsvFloats(val, t, 4)) std::memcpy(d, t, 4 * sizeof(float));
		return true;
	};
	if (try3("skel_col", EspUiColors::skel_col)) return;
	if (try3("skel_vis_col", EspUiColors::skel_vis_col)) return;
	if (try3("espcol", EspUiColors::espcol)) return;
	if (try3("vis_espcol", EspUiColors::vis_espcol)) return;
	if (try3("esp_fill_col", EspUiColors::esp_fill_col)) return;
	if (try3("esp_fill2_col", EspUiColors::esp_fill2_col)) return;
	if (try3("bomb_esp_col", EspUiColors::bomb_esp_col)) return;
	if (try3("grenade_traj_line", EspUiColors::grenade_traj_line)) return;
	if (try3("grenade_traj_marker", EspUiColors::grenade_traj_marker)) return;
	if (try3("name_esp", EspUiColors::name_esp)) return;
	if (try3("name_vis_esp", EspUiColors::name_vis_esp)) return;
	if (try3("weapon_esp", EspUiColors::weapon_esp)) return;
	if (try3("weapon_vis_esp", EspUiColors::weapon_vis_esp)) return;
	if (try3("distance_esp", EspUiColors::distance_esp)) return;
	if (try3("distance_vis_esp", EspUiColors::distance_vis_esp)) return;
	if (try3("bomb_carrier_tag", EspUiColors::bomb_carrier_tag)) return;
	if (try3("armor_bar", EspUiColors::armor_bar)) return;
	if (try3("scoped_label", EspUiColors::scoped_label)) return;
	if (try3("blind_label", EspUiColors::blind_label)) return;
	if (try3("eye_ray", EspUiColors::eye_ray)) return;
	if (try3("sniper_crosshair", EspUiColors::sniper_crosshair)) return;
	if (try3("snapline_esp", EspUiColors::snapline_esp)) return;
	if (try3("snapline_vis_los", EspUiColors::snapline_vis_los)) return;
	if (try3("ammo_text_esp", EspUiColors::ammo_text_esp)) return;
	if (try3("dropped_weapon_esp", EspUiColors::dropped_weapon_esp)) return;
	if (try3("health_bar_high", EspUiColors::health_bar_high)) return;
	if (try3("health_bar_low", EspUiColors::health_bar_low)) return;
	if (try3("health_bar_value_text", EspUiColors::health_bar_value_text)) return;
	if (try3("health_text_damaged", EspUiColors::health_text_damaged)) return;
	if (try3("health_text_full", EspUiColors::health_text_full)) return;
	if (try3("ammo_bar_hi", EspUiColors::ammo_bar_hi)) return;
	if (try3("ammo_bar_lo", EspUiColors::ammo_bar_lo)) return;
	if (try4("head_circle_fill", EspUiColors::head_circle_fill)) return;
	if (try4("head_circle_vis_fill", EspUiColors::head_circle_vis_fill)) return;
	if (try3("aim_fov_circle", EspUiColors::aim_fov_circle)) return;
	if (try3("pen_crosshair_yes", EspUiColors::pen_crosshair_yes)) return;
	if (try3("pen_crosshair_no", EspUiColors::pen_crosshair_no)) return;
	if (try3("radar_team", EspUiColors::radar_team)) return;
	if (try3("radar_enemy", EspUiColors::radar_enemy)) return;
	if (try3("radar_local", EspUiColors::radar_local)) return;
}

static void ApplyKeyVal(const std::string& key, const std::string& val) {
	if (ApplyMiscWatermarkKey(key, val))
		return;
	if (key == "aimbot.aimbot") Settings::aimbot::aimbot = ParseBool(val);
	else if (key == "aimbot.aim_fov") Settings::aimbot::aim_fov = std::stof(val);
	else if (key == "aimbot.aim_fov_min") Settings::aimbot::aim_fov_min = std::stof(val);
	else if (key == "aimbot.smooth") Settings::aimbot::smooth = std::clamp(std::stof(val), 1.f, 120.f);
	else if (key == "aimbot.aim_delay_ms") Settings::aimbot::aim_delay_ms = std::stoi(val);
	else if (key == "aimbot.aim_humanize") Settings::aimbot::aim_humanize = ParseBool(val);
	else if (key == "aimbot.aim_humanize_strength")
		Settings::aimbot::aim_humanize_strength = std::clamp(std::stoi(val), 0, 20);
	else if (key == "aimbot.aim_visible_only") Settings::aimbot::aim_visible_only = ParseBool(val);
	else if (key == "aimbot.aim_autowall") Settings::aimbot::aim_autowall = ParseBool(val);
	else if (key == "aimbot.aim_min_damage") Settings::aimbot::aim_min_damage = std::clamp(std::stof(val), 1.f, 100.f);
	else if (key == "aimbot.aim_key_mode")
		Settings::aimbot::aim_key_mode = (std::clamp)(std::stoi(val), 0, 2);
	else if (key == "aimbot.aim_vis_mode") {
		if (std::stoi(val) != 0)
			Settings::aimbot::aim_visible_only = true;
	}
	else if (key == "aimbot.trigger_reaction_enabled") Settings::aimbot::trigger_reaction_enabled = ParseBool(val);
	else if (key == "aimbot.trigger_delay") {
		const float x = std::clamp(std::stof(val), 0.f, 300.f);
		Settings::aimbot::trigger_delay_min = x;
		Settings::aimbot::trigger_delay_max = x;
	}
	else if (key == "aimbot.trigger_delay_min")
		Settings::aimbot::trigger_delay_min = std::clamp(std::stof(val), 0.f, 300.f);
	else if (key == "aimbot.trigger_delay_max")
		Settings::aimbot::trigger_delay_max = std::clamp(std::stof(val), 0.f, 300.f);
	else if (key == "aimbot.trigger_shot_cooldown") Settings::aimbot::trigger_shot_cooldown = std::clamp(std::stof(val), 1.f, 1000.f);
	else if (key == "aimbot.trigger_ttd_delay_ms") Settings::aimbot::trigger_ttd_delay_ms = std::clamp(std::stof(val), 0.f, 400.f);
	else if (key == "aimbot.trigger_visible_only") Settings::aimbot::trigger_visible_only = ParseBool(val);
	else if (key == "aimbot.trigger_autowall") Settings::aimbot::trigger_autowall = ParseBool(val);
	else if (key == "aimbot.trigger_min_damage") Settings::aimbot::trigger_min_damage = std::clamp(std::stof(val), 1.f, 100.f);
	else if (key == "aimbot.trigger_head_only") Settings::aimbot::trigger_head_only = ParseBool(val);
	else if (key == "aimbot.trigger_key_mode")
		Settings::aimbot::trigger_key_mode = (std::clamp)(std::stoi(val), 0, 2);
	else if (key == "aimbot.trigger_vis_mode") {
		if (std::stoi(val) != 0)
			Settings::aimbot::trigger_visible_only = true;
	}
	else if (key == "aimbot.trigger_ttd_spotted") {
		if (ParseBool(val))
			Settings::aimbot::trigger_visible_only = true;
	}
	else if (key == "aimbot.trigger_always") {
		if (ParseBool(val))
			Settings::aimbot::trigger_key_mode = 2;
	}
	else if (key == "aimbot.trigger_stopped_only") Settings::aimbot::trigger_stopped_only = ParseBool(val);
	else if (key == "aimbot.trigger_ignore_flash") Settings::aimbot::trigger_ignore_flash = ParseBool(val);
	else if (key == "aimbot.rcs_enabled") Settings::aimbot::rcs_enabled = ParseBool(val);
	else if (key == "aimbot.rcs_standalone") Settings::aimbot::rcs_standalone = ParseBool(val);
	else if (key == "aimbot.rcs_after_bullet") Settings::aimbot::rcs_after_bullet = std::stoi(val);
	else if (key == "aimbot.rcs_scale_pct") Settings::aimbot::rcs_scale_pct = std::stof(val);
	else if (key == "aimbot.rcs_sens_mult") Settings::aimbot::rcs_sens_mult = std::stof(val);
	else if (key == "aimbot.rcs_smooth") Settings::aimbot::rcs_smooth = std::stof(val);
	else if (key == "aimbot.rcs_aim_blend_pct") Settings::aimbot::rcs_aim_blend_pct = std::stof(val);
	else if (key == "aimbot.fov_circle") Settings::aimbot::fov_circle = ParseBool(val);
	else if (key == "aimbot.crosshair") Settings::aimbot::crosshair = ParseBool(val);
	else if (key == "aimbot.penetration_crosshair") Settings::aimbot::penetration_crosshair = ParseBool(val);
	else if (key == "aimbot.triggerbot") Settings::aimbot::triggerbot = ParseBool(val);
	else if (key == "aimbot.hitbox_mask")
		Settings::aimbot::hitbox_mask = static_cast<uint32_t>(std::stoul(val));
	else if (key == "aimbot.selectedhitbox") {
		const int v = (std::clamp)(std::stoi(val), 0, 2);
		Settings::aimbot::hitbox_mask = (1u << v);
	}

	else if (key == "Visuals.boxMode") Settings::Visuals::boxMode = std::stoi(val);
	else if (key == "Visuals.bSnaplines") Settings::Visuals::bSnaplines = ParseBool(val);
	else if (key == "Visuals.bDistance") Settings::Visuals::bDistance = ParseBool(val);
	else if (key == "Visuals.enablePlayerEsp") Settings::Visuals::enablePlayerEsp = ParseBool(val);
	else if (key == "Visuals.bBox") Settings::Visuals::bBox = ParseBool(val);
	else if (key == "Visuals.healthBar") Settings::Visuals::healthBar = ParseBool(val);
	else if (key == "Visuals.healthText") Settings::Visuals::healthText = ParseBool(val);
	else if (key == "Visuals.headcircle") Settings::Visuals::headcircle = ParseBool(val);
	else if (key == "Visuals.bones") Settings::Visuals::bones = ParseBool(val);
	else if (key == "Visuals.glow") Settings::Visuals::glow = ParseBool(val);
	else if (key == "Visuals.distance") Settings::Visuals::distance = ParseBool(val);
	else if (key == "Visuals.armor") Settings::Visuals::armor = ParseBool(val);
	else if (key == "Visuals.names") Settings::Visuals::names = ParseBool(val);
	else if (key == "Visuals.weaponEsp") Settings::Visuals::weaponEsp = ParseBool(val);
	else if (key == "Visuals.weaponEspIcon") Settings::Visuals::weaponEspIcon = ParseBool(val);
	else if (key == "Visuals.enemiesOnly") Settings::Visuals::enemiesOnly = ParseBool(val);
	else if (key == "Visuals.esp_visible_only") Settings::Visuals::esp_visible_only = ParseBool(val);
	else if (key == "Visuals.noflash") Settings::Visuals::noflash = ParseBool(val);
	else if (key == "Visuals.nohands") Settings::Visuals::nohands = ParseBool(val);
	else if (key == "Visuals.chams") Settings::Visuals::chams = ParseBool(val);
	else if (key == "Visuals.ragdoll") Settings::Visuals::ragdoll = ParseBool(val);
	else if (key == "Visuals.nightmode") Settings::Visuals::nightmode = ParseBool(val);
	else if (key == "Visuals.box") Settings::Visuals::box = ParseBool(val);
	else if (key == "Visuals.BoxWidth") Settings::Visuals::BoxWidth = std::stof(val);
	else if (key == "Visuals.boxRounding") Settings::Visuals::boxRounding = std::clamp(std::stof(val), 0.f, 16.f);
	else if (key == "Visuals.filledBox") Settings::Visuals::filledBox = ParseBool(val);
	else if (key == "Visuals.filledGradient") Settings::Visuals::filledGradient = ParseBool(val);
	else if (key == "Visuals.filledVisBox") Settings::Visuals::filledVisBox = ParseBool(val);
	else if (key == "Visuals.snaplineMode") Settings::Visuals::snaplineMode = std::clamp(std::stoi(val), 0, 2);
	else if (key == "Visuals.eyeRay") Settings::Visuals::eyeRay = ParseBool(val);
	else if (key == "Visuals.ammoBar") Settings::Visuals::ammoBar = ParseBool(val);
	else if (key == "Visuals.ammoText") Settings::Visuals::ammoText = ParseBool(val);
	else if (key == "Visuals.bombWorldEsp") Settings::Visuals::bombWorldEsp = ParseBool(val);
	else if (key == "Visuals.worldGrenades") Settings::Visuals::worldGrenades = ParseBool(val);
	else if (key == "Visuals.worldGrenadeIcons") Settings::Visuals::worldGrenadeIcons = ParseBool(val);
	else if (key == "Visuals.worldInfernoHull") Settings::Visuals::worldInfernoHull = ParseBool(val);
	else if (key == "Visuals.grenadeEspLocalOnly") Settings::Visuals::grenadeEspLocalOnly = ParseBool(val);
	else if (key == "Visuals.grenadeLineups") Settings::Visuals::grenadeLineups = ParseBool(val);
	else if (key == "Visuals.grenadeLineupMaxDrawDistance")
		Settings::Visuals::grenadeLineupMaxDrawDistance = std::clamp(std::stof(val), 0.f, 2500.f);
	else if (key == "Visuals.bombCarrierEsp") Settings::Visuals::bombCarrierEsp = ParseBool(val);
	else if (key == "Visuals.showScoped") Settings::Visuals::showScoped = ParseBool(val);
	else if (key == "Visuals.showBlind") Settings::Visuals::showBlind = ParseBool(val);
	else if (key == "Visuals.blindHideEsp") Settings::Visuals::blindHideEsp = ParseBool(val);
	else if (key == "Visuals.awpCrosshair") Settings::Visuals::awpCrosshair = ParseBool(val);
	else if (key == "Visuals.droppedWeaponEsp") Settings::Visuals::droppedWeaponEsp = ParseBool(val);
	else if (key == "Visuals.droppedWeaponText") Settings::Visuals::droppedWeaponText = ParseBool(val);
	else if (key == "Visuals.droppedWeaponIcons") Settings::Visuals::droppedWeaponIcons = ParseBool(val);

	else if (key.rfind("EspUi.", 0) == 0) {
		ApplyEspUiColor(key.substr(6), val);
		return;
	}

	if (key == "misc.bhop") Settings::misc::bhop = ParseBool(val);
	else if (key == "misc.bhop_delay_ms")
		Settings::misc::bhop_delay_ms = std::clamp(std::stoi(val), 0, 500);
	else if (key == "misc.keybind_list_window") Settings::misc::keybind_list_window = ParseBool(val);
	else if (key == "misc.radar") Settings::misc::radar = ParseBool(val);
	else if (key == "misc.hit_sound")
		Settings::misc::hit_sound = std::clamp(std::stoi(val), 0, 2);
	else if (key == "misc.hit_marker") Settings::misc::hit_marker = ParseBool(val);
	else if (key == "misc.spectatorList") Settings::misc::spectatorList = ParseBool(val);
	else if (key == "misc.bombTimer") Settings::misc::bombTimer = ParseBool(val);
	else if (key == "misc.fovChanger") Settings::misc::fovChanger = ParseBool(val);
	else if (key == "misc.fov") Settings::misc::fov = std::stof(val);
	else if (key == "misc.cloudRadar") Settings::misc::cloudRadar = ParseBool(val);
	else if (key == "misc.radarWindow") Settings::misc::radarWindow = ParseBool(val);
	else if (key == "misc.radarWindowDebugLog") Settings::misc::radarWindowDebugLog = ParseBool(val);
	else if (key == "misc.radarWindow43") { /* menu kapali: config'ten okunmaz, her zaman kare */ }
	else if (key == "misc.radarWindowMapPx") Settings::misc::radarWindowMapPx = std::stof(val);
	else if (key == "misc.radarWindowFollowLocal") Settings::misc::radarWindowFollowLocal = ParseBool(val);
	else if (key == "misc.radarWindowFollowZoom") Settings::misc::radarWindowFollowZoom = std::stof(val);
	else if (key == "misc.radarWindowBlipScale") Settings::misc::radarWindowBlipScale = std::stof(val);
	else if (key == "misc.radarWindowRotateWithView") Settings::misc::radarWindowRotateWithView = ParseBool(val);
	else if (key == "misc.radarWindowHudMatch") Settings::misc::radarWindowHudMatch = ParseBool(val);
	else if (key == "misc.radarWindowGameMapTex") Settings::misc::radarWindowGameMapTex = ParseBool(val);
	else if (key == "misc.radarWindowHideMapImage") Settings::misc::radarWindowHideMapImage = ParseBool(val);
	else if (key == "misc.radarWindowTransparency") Settings::misc::radarWindowTransparency = std::clamp(std::stoi(val), 0, 100);
	else if (key == "misc.rank_reveal_window") Settings::misc::rank_reveal_window = ParseBool(val);
	else if (key == "misc.rank_reveal_inventory_enabled") Settings::misc::rank_reveal_inventory_enabled = ParseBool(val);
	else if (key == "misc.votekick_reveal_window") { /* disabled — menu removed */ }
	else if (key == "misc.debug_visible_check") Settings::misc::debug_visible_check = ParseBool(val);
	else if (key == "misc.autosave_config") Settings::misc::autosave_config = ParseBool(val);
	else if (key == "misc.obsBypass") Settings::misc::obsBypass = ParseBool(val);
	else if (key == "misc.overlayCustomFps") Settings::misc::overlayCustomFps = ParseBool(val);
	else if (key == "misc.overlayCustomFpsValue") {
		const int parsed = std::stoi(val);
		Settings::misc::overlayCustomFpsValue = parsed < 60 ? 60 : parsed;
	}
	else if (key == "misc.workerQuality") { /* sabit hale getirildi — config'ten yuklenmiyor */ }
	else if (key == "lineups.active_pack") ExpectionalLineupBrowserActiveAdd(val);
	else if (key == "misc.cloudRadarPublishUrl") SetCStr(Settings::misc::cloudRadarPublishUrl, sizeof Settings::misc::cloudRadarPublishUrl, val);
	else if (key == "misc.cloudRadarViewerBase") SetCStr(Settings::misc::cloudRadarViewerBase, sizeof Settings::misc::cloudRadarViewerBase, val);

	else if (key == "hotkeys.aimkey") hotkeys::aimkey.store(std::stoi(val));
	else if (key == "hotkeys.triggerkey") hotkeys::triggerkey.store(std::stoi(val));
	else if (key == "hotkeys.menukey") {
		const int v = std::stoi(val);
		if (v > 0 && v < 256)
			hotkeys::menukey.store(v);
	}
}

bool ExpectionalConfigLoad(const char* name_no_ext) {
	const fs::path cfgPath = ResolveConfigFilePath(name_no_ext);
	std::string body;
	if (!Win32ReadAllBytes(cfgPath.wstring(), body))
		return false;
	std::istringstream f(body);
	WeaponRuntime::ClearWeaponProfiles();
	/** Eski aktif lineup paketlerini temizle — config'teki satirlar yeniden doldurur. */
	ExpectionalLineupBrowserClearActive();
	std::string line;
	while (std::getline(f, line)) {
		Trim(line);
		if (line.empty() || line[0] == '#')
			continue;
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		std::string k = line.substr(0, eq);
		std::string v = line.substr(eq + 1);
		Trim(k);
		Trim(v);
		try {
			if (k.rfind("weapon_cfg.", 0) == 0 || k.rfind("weapon.", 0) == 0)
				WeaponRuntime::LoadWeaponProfileLine(k, v);
			else
				ApplyKeyVal(k, v);
		} catch (...) {}
	}
	Settings::misc::save_fps = true;
	Settings::misc::radarWindow43 = false;
	WeaponRuntime::EditorSyncGlobalSnapshotFromSettings();
	return true;
}

bool ExpectionalConfigDelete(const char* name_no_ext) {
	std::error_code ec;
	return fs::remove(PathForName(name_no_ext), ec);
}
