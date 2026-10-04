#pragma once
#include <Windows.h>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "../../Includes/Imgui/imgui.h"
#include "../../Includes/Imgui/imgui_internal.h"
#include "../Game/globals.hpp"
#include "../Game/expectional_misc_runtime.hpp"
#include "../Game/esp_extras.hpp"
#include "../Game/cloud_radar.hpp"
#include "../Game/window_radar.hpp"
#include "../Game/config_io.hpp"
#include "../Game/grenade_lineup.hpp"
#include "../Game/esp_preview.hpp"
#include "../Game/weapon_runtime.hpp"
#include "../OSImGui/os_imgui_menu.hpp"
#include "../OSImGui/imgui_edited.hpp"
#include "../Game/Features/TriggerBot.h"

inline constexpr float kExpectionalMenuContentX = (1000.f - (470.f * 2.f + 20.f)) * 0.5f;

inline void ExpectionalAlignMenuContentX() {
	ImGui::SetCursorPosX(kExpectionalMenuContentX);
}

inline constexpr const char kExpectionalBhopConsoleSetup[] = "unbind space; unbind f7; bind f24 \"+jump\"";

inline constexpr const char kExpectionalBhopConsoleRestore[] = "bind space \"+jump\"; unbind f24; unbind f7";

inline bool ExpectionalCopyAsciiToClipboard(const char* text)
{
	if (!text || !text[0])
		return false;
	if (!OpenClipboard(nullptr))
		return false;
	EmptyClipboard();
	const size_t n = std::strlen(text) + 1;
	HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, n);
	if (!mem) {
		CloseClipboard();
		return false;
	}
	void* lock = GlobalLock(mem);
	if (lock)
		std::memcpy(lock, text, n);
	GlobalUnlock(mem);
	if (!SetClipboardData(CF_TEXT, mem)) {
		GlobalFree(mem);
		CloseClipboard();
		return false;
	}
	CloseClipboard();
	return true;
}

namespace hotkeys
{
	
	inline std::atomic<int> aimkey{2};
	
	inline std::atomic<int> triggerkey{6};
	
	inline std::atomic<int> menukey{VK_HOME};
}

static std::atomic<int> keystatus{0};
static std::atomic<int> trigger_keystatus{0};
static std::atomic<int> menu_keystatus{0};
static int realkey = 0;

inline bool GetKey(int key)
{
	realkey = key;
	return true;
}

static DWORD WINAPI ChangeKeyThread(LPVOID)
{
	keystatus.store(1);
	Sleep(200);
	for (;;) {
		bool any_down = false;
		for (int vk = 1; vk < 256; ++vk) {
			if (GetAsyncKeyState(vk) & 0x8000) {
				any_down = true;
				break;
			}
		}
		if (!any_down)
			break;
		Sleep(10);
	}
	for (;;) {
		for (int vk = 1; vk < 256; ++vk) {
			if (GetAsyncKeyState(vk) & 0x8000) {
				hotkeys::aimkey.store(vk);
				keystatus.store(0);
				return 0;
			}
		}
		Sleep(10);
	}
}

static DWORD WINAPI ChangeTriggerKeyThread(LPVOID)
{
	trigger_keystatus.store(1);
	Sleep(200);
	for (;;) {
		bool any_down = false;
		for (int vk = 1; vk < 256; ++vk) {
			if (GetAsyncKeyState(vk) & 0x8000) {
				any_down = true;
				break;
			}
		}
		if (!any_down)
			break;
		Sleep(10);
	}
	for (;;) {
		for (int vk = 1; vk < 256; ++vk) {
			if (GetAsyncKeyState(vk) & 0x8000) {
				hotkeys::triggerkey.store(vk);
				trigger_keystatus.store(0);
				return 0;
			}
		}
		Sleep(10);
	}
}

static DWORD WINAPI ChangeMenuKeyThread(LPVOID)
{
	menu_keystatus.store(1);
	Sleep(200);
	for (;;) {
		bool any_down = false;
		for (int vk = 1; vk < 256; ++vk) {
			if (GetAsyncKeyState(vk) & 0x8000) {
				any_down = true;
				break;
			}
		}
		if (!any_down)
			break;
		Sleep(10);
	}
	for (;;) {
		for (int vk = 1; vk < 256; ++vk) {
			if (GetAsyncKeyState(vk) & 0x8000) {
				hotkeys::menukey.store(vk);
				menu_keystatus.store(0);
				return 0;
			}
		}
		Sleep(10);
	}
}

static const char* keyNames[] =
{
	"Press a key",
	"Left Mouse",
	"Right Mouse",
	"Cancel",
	"Middle Mouse",
	"Mouse 5",
	"Mouse 4",
	"",
	"Backspace",
	"Tab",
	"",
	"",
	"Clear",
	"Enter",
	"",
	"",
	"Shift",
	"Control",
	"Alt",
	"Pause",
	"Caps",
	"",
	"",
	"",
	"",
	"",
	"",
	"Escape",
	"",
	"",
	"",
	"",
	"Space",
	"Page Up",
	"Page Down",
	"End",
	"Home",
	"Left",
	"Up",
	"Right",
	"Down",
	"",
	"",
	"",
	"Print",
	"Insert",
	"Delete",
	"",
	"0",
	"1",
	"2",
	"3",
	"4",
	"5",
	"6",
	"7",
	"8",
	"9",
	"",
	"",
	"",
	"",
	"",
	"",
	"",
	"A",
	"B",
	"C",
	"D",
	"E",
	"F",
	"G",
	"H",
	"I",
	"J",
	"K",
	"L",
	"M",
	"N",
	"O",
	"P",
	"Q",
	"R",
	"S",
	"T",
	"U",
	"V",
	"W",
	"X",
	"Y",
	"Z",
	"",
	"",
	"",
	"",
	"",
	"Numpad 0",
	"Numpad 1",
	"Numpad 2",
	"Numpad 3",
	"Numpad 4",
	"Numpad 5",
	"Numpad 6",
	"Numpad 7",
	"Numpad 8",
	"Numpad 9",
	"Multiply",
	"Add",
	"",
	"Subtract",
	"Decimal",
	"Divide",
	"F1",
	"F2",
	"F3",
	"F4",
	"F5",
	"F6",
	"F7",
	"F8",
	"F9",
	"F10",
	"F11",
	"F12",
};

static bool Items_ArrayGetter(void* data, int idx, const char** out_text)
{
	const char* const* items = (const char* const*)data;
	if (out_text)
		*out_text = items[idx];
	return true;
}

inline const char* ExpectionalVkPreview(int vk)
{
	const char* preview = nullptr;
	if (vk >= 0 && vk < IM_ARRAYSIZE(keyNames))
		Items_ArrayGetter(keyNames, vk, &preview);
	return preview ? preview : "?";
}

inline void ExpectionalFmtKeyBracketShort(int vk, char out[20])
{
	if (!out)
		return;
	out[0] = '\0';
	if (vk >= '0' && vk <= '9') {
		snprintf(out, 20, "[%c]", static_cast<char>(vk));
		return;
	}
	if (vk >= 'A' && vk <= 'Z') {
		snprintf(out, 20, "[%c]", static_cast<char>(vk));
		return;
	}
	if (vk >= 'a' && vk <= 'z') {
		snprintf(out, 20, "[%c]", static_cast<char>(vk - 'a' + 'A'));
		return;
	}
	switch (vk) {
	case VK_SPACE: snprintf(out, 20, "[spc]"); return;
	case VK_HOME: snprintf(out, 20, "[Hm]"); return;
	case VK_XBUTTON1: snprintf(out, 20, "[M4]"); return;
	case VK_XBUTTON2: snprintf(out, 20, "[M5]"); return;
	case VK_RBUTTON: snprintf(out, 20, "[M2]"); return;
	case VK_LBUTTON: snprintf(out, 20, "[M1]"); return;
	case VK_MBUTTON: snprintf(out, 20, "[M3]"); return;
	default: {
		const char* p = ExpectionalVkPreview(vk);
		if (p && p[0])
			snprintf(out, 20, "[%s]", p);
		else
			snprintf(out, 20, "[?]");
		return;
	}
	}
}

inline const char* ExpectionalModeShort(int m)
{
	switch (m) {
	case 0: return "hold";
	case 1: return "toggle";
	case 2: return "always";
	default: return "?";
	}
}

inline void ExpectionalKeybindListWindow()
{
	if (!Settings::misc::keybind_list_window)
		return;

	const ImVec4 titleFill = c::elements::background_widget;
	ImGui::PushStyleColor(ImGuiCol_WindowBg, c::background::filling);
	ImGui::PushStyleColor(ImGuiCol_Border, c::background::stroke);
	ImGui::PushStyleColor(ImGuiCol_Text, c::elements::text_active);
	ImGui::PushStyleColor(ImGuiCol_TitleBg, titleFill);
	ImGui::PushStyleColor(ImGuiCol_TitleBgActive, titleFill);
	ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, titleFill);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.f, 10.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);

	ExpectionalOsMenu_UiFontScope uiFont;

	ImGuiIO& io = ImGui::GetIO();
	ImGui::SetNextWindowBgAlpha(0.97f);
	ImGui::SetNextWindowPos(ImVec2(36.f, io.DisplaySize.y - 48.f), ImGuiCond_FirstUseEver, ImVec2(0.f, 1.f));
	ImGui::SetNextWindowSizeConstraints(ImVec2(220.f, 48.f), ImVec2(4096.f, 4096.f));

	const ImGuiWindowFlags wf = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoScrollbar;

	if (!ImGui::Begin("Keybinds##Expectional", nullptr, wf)) {
		ImGui::End();
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(6);
		return;
	}

	auto rowKeybind = [](const char* name, const char* mode, const char* keyCol, bool active) {
		const ImVec4 col = active ? c::accent : c::elements::text_active;
		ImGui::PushStyleColor(ImGuiCol_Text, col);
		ImGui::TextUnformatted(name);
		ImGui::SameLine(82.f);
		ImGui::TextUnformatted(mode);
		ImGui::SameLine(156.f);
		ImGui::TextUnformatted(keyCol);
		ImGui::PopStyleColor();
	};

	char kb[20]{};
	const LegitCombatSettings rt = WeaponRuntime::Active();

	const int ak = hotkeys::aimkey.load();
	const bool akDown = ak > 0 && ((GetAsyncKeyState(ak) & 0x8000) != 0);
	bool aimActive = false;
	if (rt.aimbot) {
		const int mode = rt.aim_key_mode;
		if (mode == 0)
			aimActive = akDown;
		else if (mode == 1)
			aimActive = ExpectionalCombatUiState::aim_toggle_arm;
		else if (mode == 2)
			aimActive = true;
		else
			aimActive = akDown;
		ExpectionalFmtKeyBracketShort(ak, kb);
		rowKeybind("Aimbot", ExpectionalModeShort(rt.aim_key_mode), kb, aimActive);
	}

	const int tk = hotkeys::triggerkey.load();
	const bool tkDown = tk > 0 && ((GetAsyncKeyState(tk) & 0x8000) != 0);
	bool trigActive = false;
	if (rt.triggerbot) {
		const int tmode = rt.trigger_key_mode;
		if (tmode == 0)
			trigActive = tkDown;
		else if (tmode == 1)
			trigActive = TriggerBot::ToggleArmForUi();
		else if (tmode == 2)
			trigActive = true;
		else
			trigActive = tkDown;
		ExpectionalFmtKeyBracketShort(tk, kb);
		rowKeybind("Trigger", ExpectionalModeShort(rt.trigger_key_mode), kb, trigActive);
	}

	if (Settings::misc::bhop) {
		const bool spaceDown = (GetAsyncKeyState(VK_SPACE) & 0x8000) != 0;
		rowKeybind("Bhop", "space", "[F24]", spaceDown);
	}

	ImGui::End();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(6);
}

inline void HotkeyButton(int aimkey, int status)
{
	const char* preview_value = NULL;
	if (aimkey >= 0 && aimkey < IM_ARRAYSIZE(keyNames))
		Items_ArrayGetter(keyNames, aimkey, &preview_value);

	std::string aimkeys;
	if (preview_value == NULL)
		aimkeys = "Select Key";
	else
		aimkeys = preview_value;

	if (status == 1)
	{

		aimkeys = "Press Key";
	}
	if (ImGui::Button(aimkeys.c_str(), ImVec2(125, 20)))
	{
		if (status == 0 && keystatus.load() == 0)
			CreateThread(nullptr, 0, ChangeKeyThread, nullptr, 0, nullptr);
	}
}

static void HotkeyButtonTrigger(int trigKey, int status)
{
	const char* preview_value = NULL;
	if (trigKey >= 0 && trigKey < IM_ARRAYSIZE(keyNames))
		Items_ArrayGetter(keyNames, trigKey, &preview_value);

	std::string label = preview_value ? preview_value : std::string("Select Key");
	if (status == 1)
		label = "Press Key";

	if (ImGui::Button(label.c_str(), ImVec2(125, 20))) {
		if (status == 0 && trigger_keystatus.load() == 0)
			CreateThread(nullptr, 0, ChangeTriggerKeyThread, nullptr, 0, nullptr);
	}
}

static void HotkeyButtonMenu(int menuKey, int status)
{
	const char* preview_value = nullptr;
	if (menuKey >= 0 && menuKey < IM_ARRAYSIZE(keyNames))
		Items_ArrayGetter(keyNames, menuKey, &preview_value);

	std::string label = preview_value ? preview_value : std::string("Select Key");
	if (status == 1)
		label = "Press Key";

	if (ImGui::Button(label.c_str(), ImVec2(125, 20))) {
		if (status == 0 && menu_keystatus.load() == 0)
			CreateThread(nullptr, 0, ChangeMenuKeyThread, nullptr, 0, nullptr);
	}
}

inline bool ExpectionalCategoryTabButton(const char* label, bool selected, const ImVec2& size) {
	ImGui::PushID(label);
	const ImU32 borderCol = ImGui::GetColorU32(selected ? c::accent : ImVec4(0.22f, 0.22f, 0.22f, 1.f));
	const ImVec4 fill = selected
	    ? ImVec4(c::accent.x, c::accent.y, c::accent.z, 0.14f)
	    : ImVec4(0.10f, 0.10f, 0.10f, 1.f);
	const ImU32 bgCol = ImGui::GetColorU32(fill);
	const ImU32 textCol = ImGui::GetColorU32(selected ? c::accent : c::elements::text);

	ImGui::InvisibleButton("##catbtn", size);
	const bool pressed = ImGui::IsItemClicked();
	const ImRect bb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
	ImDrawList* dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(bb.Min, bb.Max, bgCol, 4.f);
	dl->AddRect(bb.Min, bb.Max, borderCol, 4.f, 0, selected ? 1.6f : 1.2f);

	const ImVec2 ts = ImGui::CalcTextSize(label);
	const float tx = bb.Min.x + (size.x - ts.x) * 0.5f;
	const float ty = bb.Min.y + (size.y - ts.y) * 0.5f;
	dl->AddText(ImVec2(tx, ty), textCol, label);
	ImGui::PopID();
	return pressed;
}

inline void ExpectionalWeaponCategoryInnerTabs(const char* scope_id) {
	static int s_prevCategory = -1;
	int& sel = Settings::weapon_cfg::editor_category_idx;
	const int maxIdx = WeaponRuntime::CategoryCount() - 1;
	if (sel < 0) sel = 0;
	if (sel > maxIdx) sel = maxIdx;

	if (s_prevCategory < 0) {
		WeaponRuntime::EditorSyncGlobalSnapshotFromSettings();
		if (sel > 0)
			WeaponRuntime::EnsureEditorCategoryProfile(static_cast<WeaponRuntime::WeaponCategory>(sel));
		s_prevCategory = sel;
	}

	static const char* kTabLabels[] = { "General", "Pistols", "H.Pistols", "SMG", "Heavy", "Rifles", "Sniper" };
	constexpr float kTabH = 30.f;
	constexpr float kTabGap = 8.f;
	constexpr float kFullContentW = 470.f * 2.f + 20.f; 
	const int nTabs = maxIdx + 1;
	const float kTabW = (kFullContentW - kTabGap * static_cast<float>(nTabs - 1)) / static_cast<float>(nTabs);

	ImGui::PushID(scope_id);
	ExpectionalAlignMenuContentX();
	ImGui::BeginGroup();
	for (int i = 0; i <= maxIdx; ++i) {
		if (i > 0)
			ImGui::SameLine(0, kTabGap);
		ImGui::PushID(1000 + i);
		const char* tabLabel = (i < IM_ARRAYSIZE(kTabLabels)) ? kTabLabels[i] : WeaponRuntime::CategoryLabel(i);
		if (ExpectionalCategoryTabButton(tabLabel, sel == i, ImVec2(kTabW, kTabH))) {
			if (sel != i) {
				const int prev = sel;
				sel = i;
				WeaponRuntime::OnMenuCategoryChanged(prev, i);
				if (i > 0) {
					const auto cat = static_cast<WeaponRuntime::WeaponCategory>(i);
					WeaponRuntime::EnsureEditorCategoryProfile(cat);
				}
				ImGui::ClearActiveID();
				s_prevCategory = i;
			}
		}
		ImGui::PopID();
	}
	ImGui::EndGroup();
	ExpectionalAlignMenuContentX();
	ImGui::Spacing();
	ImGui::PopID();
}

inline bool ExpectionalIsCategoryEditorLocked() {
	const int sel = Settings::weapon_cfg::editor_category_idx;
	if (sel <= 0)
		return false;
	return !Settings::weapon_cfg::cat_custom[static_cast<size_t>(sel - 1)];
}

struct ExpectionalCategoryEditorLockScope {
	bool locked_ = false;
	ExpectionalCategoryEditorLockScope() {
		locked_ = ExpectionalIsCategoryEditorLocked();
		if (locked_)
			ImGui::BeginDisabled();
	}
	~ExpectionalCategoryEditorLockScope() {
		if (locked_)
			ImGui::EndDisabled();
	}
};

inline void ExpectionalDrawCategoryMasterSwitch() {
	const int sel = Settings::weapon_cfg::editor_category_idx;
	if (sel <= 0)
		return;
	ExpectionalAlignMenuContentX();
	bool& master = Settings::weapon_cfg::cat_custom[static_cast<size_t>(sel - 1)];
	edited::Checkbox("Master Switch", "", &master);
	if (ImGui::IsItemEdited() && master) {
		const auto cat = static_cast<WeaponRuntime::WeaponCategory>(sel);
		WeaponRuntime::EnsureEditorCategoryProfile(cat);
	}
	ImGui::Spacing();
}

inline void ExpectionalDrawRcsPanel() {
	LegitCombatSettings* row = WeaponRuntime::MutEditorProfileRow();
	bool& rcs_enabled = row ? row->rcs_enabled : Settings::aimbot::rcs_enabled;
	bool& rcs_standalone = row ? row->rcs_standalone : Settings::aimbot::rcs_standalone;
	int& rcs_after_bullet = row ? row->rcs_after_bullet : Settings::aimbot::rcs_after_bullet;
	float& rcs_scale_pct = row ? row->rcs_scale_pct : Settings::aimbot::rcs_scale_pct;
	float& rcs_smooth = row ? row->rcs_smooth : Settings::aimbot::rcs_smooth;

	ImGui::PushID("tabrcs");
	ImGui::PushID(Settings::weapon_cfg::editor_category_idx);
	{
		ExpectionalCategoryEditorLockScope lock;
		edited::Checkbox("RCS", "", &rcs_enabled);
		if (rcs_enabled) {
			edited::Checkbox("Standalone", "", &rcs_standalone);
			edited::SliderInt("Start after bullet", "", &rcs_after_bullet, 0, 10);
			edited::SliderFloat("Strength %", "", &rcs_scale_pct, 10.f, 100.f);
			edited::SliderFloat("RCS smooth", "", &rcs_smooth, 4.f, 160.f);
		}
	}
	ImGui::PopID();
	ImGui::PopID();
}

class GradientLine {
public:

	static bool Render(ImVec2 size)
	{
		static ImColor gradient_colors[] =
		{
			
			ImColor(0, 0, 0),
			
			ImColor(0, 0, 0),
			
			ImColor(0, 0, 0),
			
			ImColor(0, 0, 0),
			
			ImColor(0, 0, 0),
			
			ImColor(0, 0, 0),
			
			ImColor(0, 0, 0)
		};

		ImDrawList* draw_list = ImGui::GetWindowDrawList();
		ImVec2      screen_pos = ImGui::GetCursorScreenPos();

		static int pos = 0;

		if (size.x - pos == 0)
			pos = 0;
		else
			pos++;

		for (int i = 0; i < 6; ++i)
		{
			ImVec2 item_spacing = ImGui::GetStyle().ItemSpacing;

			auto render = [&](int displacement)
				{
					draw_list->AddRectFilledMultiColor
					(
						ImVec2((screen_pos.x - item_spacing.x - displacement) + (i) * (size.x / 6), (screen_pos.y - item_spacing.y)),
						ImVec2((screen_pos.x - item_spacing.x + (item_spacing.x * 2) - displacement) + (i + 1) * (size.x / 6), (screen_pos.y - item_spacing.y) + (size.y)),

						gradient_colors[i], gradient_colors[i + 1], gradient_colors[i + 1], gradient_colors[i]
					);
				};

			render((pos)); render((pos - size.x));
		}
		return true;
	}
};

inline void ExpectionalTryAutosaveAfterMenuInteraction() {
	if (!Settings::misc::autosave_config || !Settings::bMenu)
		return;
	static bool s_anyItemWasActive = false;
	const bool anyActive = ImGui::IsAnyItemActive();
	if (s_anyItemWasActive && !anyActive)
		WeaponRuntime::NotifyMenuCombatSettingEdited();
	s_anyItemWasActive = anyActive;
}

inline void ExpectionalDrawMenuTabBody(int s_mainTabSel, int esp_part = -1)
{
	switch (s_mainTabSel) {
			case 0: {
				LegitCombatSettings* wAim = WeaponRuntime::MutEditorProfileRow();
				bool& aimbot = wAim ? wAim->aimbot : Settings::aimbot::aimbot;
				float& aim_fov = wAim ? wAim->aim_fov : Settings::aimbot::aim_fov;
				float& aim_fov_min = wAim ? wAim->aim_fov_min : Settings::aimbot::aim_fov_min;
				float& smooth = wAim ? wAim->smooth : Settings::aimbot::smooth;
				int& aim_delay_ms = wAim ? wAim->aim_delay_ms : Settings::aimbot::aim_delay_ms;
				bool& aim_humanize = wAim ? wAim->aim_humanize : Settings::aimbot::aim_humanize;
				int& aim_humanize_strength = wAim ? wAim->aim_humanize_strength : Settings::aimbot::aim_humanize_strength;
				bool& aim_visible_only = wAim ? wAim->aim_visible_only : Settings::aimbot::aim_visible_only;
				bool& aim_autowall = wAim ? wAim->aim_autowall : Settings::aimbot::aim_autowall;
				float& aim_min_damage = wAim ? wAim->aim_min_damage : Settings::aimbot::aim_min_damage;
				int& aim_key_mode = wAim ? wAim->aim_key_mode : Settings::aimbot::aim_key_mode;
				uint32_t& hitbox_mask = wAim ? wAim->hitbox_mask : Settings::aimbot::hitbox_mask;
				bool& fov_circle = wAim ? wAim->fov_circle : Settings::aimbot::fov_circle;
				bool& crosshair = wAim ? wAim->crosshair : Settings::aimbot::crosshair;
				bool& penetration_crosshair = wAim ? wAim->penetration_crosshair : Settings::aimbot::penetration_crosshair;
				ImGui::PushID("tabaim");
				ImGui::PushID(Settings::weapon_cfg::editor_category_idx);
				{
					ExpectionalCategoryEditorLockScope lock;
					edited::Checkbox("Aimbot", "", &aimbot);
					if (aimbot) {
					edited::SliderFloat("Field Of View", "", &aim_fov, 1.f, 120.f);
					edited::SliderFloat("FOV deadzone (px)", "", &aim_fov_min, 0.f, 8.f);
					ImGui::Spacing();
					edited::SliderFloat("Smooth", "", &smooth, 1.f, 120.f);
					edited::SliderInt("Aim delay (ms)", "", &aim_delay_ms, 0, 40);
					ImGui::Spacing();
					edited::Checkbox("Humanize", "", &aim_humanize);
					if (aim_humanize)
						edited::SliderInt("Humanize strength", "", &aim_humanize_strength, 0, 20);
					ImGui::Spacing();
					edited::Checkbox("Visible check", "", &aim_visible_only);
					if (aim_visible_only) {
						edited::Checkbox("Autowall", "", &aim_autowall);
						if (aim_autowall)
							edited::SliderFloat("Min damage", "", &aim_min_damage, 1.f, 100.f);
					}
					ImGui::Spacing();
					edited::Checkbox("Penetration crosshair", "", &penetration_crosshair);
					if (penetration_crosshair) {
						ImGui::PushItemWidth(28.f);
						ImGui::ColorEdit3("##pen_yes", EspUiColors::pen_crosshair_yes,
							ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
						ImGui::SameLine(0.f, 6.f);
						ImGui::ColorEdit3("##pen_no", EspUiColors::pen_crosshair_no,
							ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
						ImGui::PopItemWidth();
					}
					ImGui::Spacing();
					edited::Checkbox("Fov Circle", "", &fov_circle);
					ImGui::SameLine();
					ImGui::PushItemWidth(28.f);
					ImGui::ColorEdit3("##aim_fov_circ_col", EspUiColors::aim_fov_circle,
						ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel);
					ImGui::PopItemWidth();
					ImGui::Spacing();
					const char* aimKeyLab[] = { "Mode: Hold key", "Mode: Toggle key", "Mode: Always on" };
					ImGui::SetNextItemWidth(200.f);
					edited::Combo("Aim key mode", "", &aim_key_mode, aimKeyLab, IM_ARRAYSIZE(aimKeyLab));
					ImGui::Spacing();
					ImGui::Text("Aim key ");
					ImGui::SameLine();
					HotkeyButton(hotkeys::aimkey.load(), keystatus.load());
					ImGui::Spacing();
					ImGui::TextUnformatted("Hitboxes");
					bool hb_head = (hitbox_mask & 1u) != 0;
					bool hb_neck = (hitbox_mask & 2u) != 0;
					bool hb_pelvis = (hitbox_mask & 4u) != 0;
					
					edited::Checkbox("Head##hbh", "", &hb_head);
					edited::Checkbox("Neck##hbn", "", &hb_neck);
					edited::Checkbox("Pelvis##hbp", "", &hb_pelvis);
					hitbox_mask = (hb_head ? 1u : 0u) | (hb_neck ? 2u : 0u) | (hb_pelvis ? 4u : 0u);
					if (!hitbox_mask)
						hitbox_mask = 1u;
					ImGui::Spacing();
				}
				}
				ImGui::Spacing();
				ImGui::PopID();
				ImGui::PopID();
				break;
			}
			case 1: {
				LegitCombatSettings* wTrg = WeaponRuntime::MutEditorProfileRow();
				bool& triggerbot = wTrg ? wTrg->triggerbot : Settings::aimbot::triggerbot;
				bool& trigger_reaction_enabled = wTrg ? wTrg->trigger_reaction_enabled : Settings::aimbot::trigger_reaction_enabled;
				float& trigger_delay_min = wTrg ? wTrg->trigger_delay_min : Settings::aimbot::trigger_delay_min;
				float& trigger_delay_max = wTrg ? wTrg->trigger_delay_max : Settings::aimbot::trigger_delay_max;
				float& trigger_shot_cooldown = wTrg ? wTrg->trigger_shot_cooldown : Settings::aimbot::trigger_shot_cooldown;
				bool& trigger_stopped_only = wTrg ? wTrg->trigger_stopped_only : Settings::aimbot::trigger_stopped_only;
				bool& trigger_ignore_flash = wTrg ? wTrg->trigger_ignore_flash : Settings::aimbot::trigger_ignore_flash;
				bool& trigger_visible_only = wTrg ? wTrg->trigger_visible_only : Settings::aimbot::trigger_visible_only;
				bool& trigger_autowall = wTrg ? wTrg->trigger_autowall : Settings::aimbot::trigger_autowall;
				float& trigger_min_damage = wTrg ? wTrg->trigger_min_damage : Settings::aimbot::trigger_min_damage;
				bool& trigger_head_only = wTrg ? wTrg->trigger_head_only : Settings::aimbot::trigger_head_only;
				int& trigger_key_mode = wTrg ? wTrg->trigger_key_mode : Settings::aimbot::trigger_key_mode;
				ImGui::PushID("tabtrg");
				ImGui::PushID(Settings::weapon_cfg::editor_category_idx);
				{
					ExpectionalCategoryEditorLockScope lock;
					edited::Checkbox("Triggerbot", "", &triggerbot);
					if (triggerbot) {
					const char* trigKeyLab[] = { "Mode: Hold key", "Mode: Toggle key", "Mode: Always on" };
					ImGui::SetNextItemWidth(220.f);
					edited::Combo("Trigger key mode", "", &trigger_key_mode, trigKeyLab, IM_ARRAYSIZE(trigKeyLab));
					ImGui::Spacing();
					edited::Checkbox("Reaction time", "", &trigger_reaction_enabled);
					if (trigger_reaction_enabled) {
						edited::SliderFloat("Reaction min (ms)", "", &trigger_delay_min, 0.f, 300.f);
						edited::SliderFloat("Reaction max (ms)", "", &trigger_delay_max, 0.f, 300.f);
					}
					edited::SliderFloat("Delay Between Shots (ms)", "", &trigger_shot_cooldown, 1.f, 1000.f);
					ImGui::Spacing();
					edited::Checkbox("Visible check", "", &trigger_visible_only);
					if (trigger_visible_only) {
						edited::Checkbox("Autowall", "", &trigger_autowall);
						if (trigger_autowall)
							edited::SliderFloat("Min damage", "", &trigger_min_damage, 1.f, 100.f);
					}
					ImGui::Spacing();
					edited::Checkbox("Head only", "", &trigger_head_only);
					ImGui::Spacing();
					edited::Checkbox("Stopped only", "", &trigger_stopped_only);
					edited::Checkbox("Ignore flash", "", &trigger_ignore_flash);
					ImGui::Spacing();
					ImGui::Text("Trigger key ");
					ImGui::SameLine();
					HotkeyButtonTrigger(hotkeys::triggerkey.load(), trigger_keystatus.load());
				}
				}
				ImGui::PopID();
				ImGui::PopID();
				break;
			}
			case 2: {

				const ImGuiColorEditFlags kEspColRgb = ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoLabel;
				const ImGuiColorEditFlags kEspColRgba = kEspColRgb | ImGuiColorEditFlags_AlphaPreviewHalf;

				auto espRgb = [&](const char* id, float* rgb, const char* tip) {
					ImGui::PushItemWidth(28.f);
					ImGui::ColorEdit3(id, rgb, kEspColRgb);
					ImGui::PopItemWidth();
					if (tip && ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", tip);
				};
				auto espRgba = [&](const char* id, float* rgba, const char* tip) {
					ImGui::PushItemWidth(36.f);
					ImGui::ColorEdit4(id, rgba, kEspColRgba);
					ImGui::PopItemWidth();
					if (tip && ImGui::IsItemHovered())
						ImGui::SetTooltip("%s", tip);
				};

				auto draw_esp_players = [&]() {
					edited::Checkbox("Enable ESP", "Player ESP master toggle", &Settings::Visuals::enablePlayerEsp);
					ImGui::Separator();
					edited::Checkbox("Box ESP", "", &Settings::Visuals::bBox);
					ImGui::SameLine();
					espRgb("##esp_outline_h", EspUiColors::espcol, "Hidden ESP Color");
					ImGui::SameLine(0.f, 6.f);
					espRgb("##esp_outline_v", EspUiColors::vis_espcol, "Visible ESP Color");

					if (Settings::Visuals::bBox) {
						ImGui::SetNextItemWidth(-FLT_MIN);
						edited::Combo("Box type", "", &Settings::Visuals::boxMode, Settings::Visuals::boxStyle,
							IM_ARRAYSIZE(Settings::Visuals::boxStyle));
					}

					edited::Checkbox("Filled Box", "", &Settings::Visuals::filledBox);
					ImGui::SameLine();
					espRgb("##esp_fill_a", EspUiColors::esp_fill_col, "Fill Color");

					edited::Checkbox("Gradient Filled", "", &Settings::Visuals::filledGradient);
					ImGui::SameLine();
					espRgb("##esp_fill_b", EspUiColors::esp_fill2_col, "Fill Gradient 2nd Color");

					edited::Checkbox("Visible Only ESP", "", &Settings::Visuals::esp_visible_only);

					edited::Checkbox("Head ESP", "", &Settings::Visuals::headcircle);
					ImGui::SameLine();
					espRgba("##esp_head_fill_h", EspUiColors::head_circle_fill, "Head Hidden Color");
					ImGui::SameLine(0.f, 6.f);
					espRgba("##esp_head_fill_v", EspUiColors::head_circle_vis_fill, "Head Visible Color");

					edited::Checkbox("Skeleton ESP", "", &Settings::Visuals::bones);
					ImGui::SameLine();
					espRgb("##esp_skel_h", EspUiColors::skel_col, "Skeleton Hidden Color");
					ImGui::SameLine(0.f, 6.f);
					espRgb("##esp_skel_v", EspUiColors::skel_vis_col, "Skeleton Visible Color");

					edited::Checkbox("Name ESP", "", &Settings::Visuals::names);
					ImGui::SameLine();
					espRgb("##esp_name_h", EspUiColors::name_esp, "Name Hidden Color");
					ImGui::SameLine(0.f, 6.f);
					espRgb("##esp_name_v", EspUiColors::name_vis_esp, "Name Visible Color");

					edited::Checkbox("Weapon Name", "", &Settings::Visuals::weaponEsp);
					ImGui::SameLine();
					espRgb("##esp_wpn_h", EspUiColors::weapon_esp, "Weapon Hidden Color");
					ImGui::SameLine(0.f, 6.f);
					espRgb("##esp_wpn_v", EspUiColors::weapon_vis_esp, "Weapon Visible Color");

					edited::Checkbox("Weapon Icon", "", &Settings::Visuals::weaponEspIcon);

					edited::Checkbox("Armor ESP", "", &Settings::Visuals::armor);
					ImGui::SameLine();
					espRgb("##esp_armor", EspUiColors::armor_bar, "Armor Bar Color");

					edited::Checkbox("Health Bar", "", &Settings::Visuals::healthBar);
					ImGui::SameLine();
					espRgb("##esp_hp_hi", EspUiColors::health_bar_high, "High HP Color");
					ImGui::SameLine(0.f, 6.f);
					espRgb("##esp_hp_lo", EspUiColors::health_bar_low, "Low HP Color");
					ImGui::SameLine(0.f, 6.f);
					espRgb("##esp_hp_tx", EspUiColors::health_bar_value_text, "Number Color");

					edited::Checkbox("Health Text", "", &Settings::Visuals::healthText);

					edited::Checkbox("Ammo Bar", "", &Settings::Visuals::ammoBar);
					ImGui::SameLine();
					espRgb("##esp_am_hi", EspUiColors::ammo_bar_hi, "High Ammo Color");
					ImGui::SameLine(0.f, 6.f);
					espRgb("##esp_am_lo", EspUiColors::ammo_bar_lo, "Low Ammo Color");

					edited::Checkbox("Ammo Text", "", &Settings::Visuals::ammoText);
					ImGui::SameLine();
					espRgb("##esp_am_txt", EspUiColors::ammo_text_esp, "Ammo Text Color");

					edited::Checkbox("Distance", "", &Settings::Visuals::distance);
					ImGui::SameLine();
					espRgb("##esp_dist_h", EspUiColors::distance_esp, "Distance Hidden Color");
					ImGui::SameLine(0.f, 6.f);
					espRgb("##esp_dist_v", EspUiColors::distance_vis_esp, "Distance Visible Color");

					edited::Checkbox("Snapline ESP", "", &Settings::Visuals::bSnaplines);
					ImGui::SameLine();
					espRgb("##esp_snap_h", EspUiColors::snapline_esp, "Snapline Hidden Color");
					ImGui::SameLine(0.f, 6.f);
					espRgb("##esp_snap_v", EspUiColors::snapline_vis_los, "Snapline Visible Color");

					if (Settings::Visuals::bSnaplines) {
						ImGui::SetNextItemWidth(-FLT_MIN);
						const char* snapLab[] = { "Bottom to foot", "Bottom to crosshair", "Top to sky" };
						edited::Combo("Snap line style", "", &Settings::Visuals::snaplineMode, snapLab, 3);
					}

					edited::Checkbox("Eye Ray", "", &Settings::Visuals::eyeRay);
					ImGui::SameLine();
					espRgb("##esp_eye", EspUiColors::eye_ray, "Eye Ray Color");

					edited::Checkbox("Bomb Carrier Flag", "", &Settings::Visuals::bombCarrierEsp);
					ImGui::SameLine();
					espRgb("##esp_c4tag", EspUiColors::bomb_carrier_tag, "Text Color");

					edited::Checkbox("Scoped Flag", "", &Settings::Visuals::showScoped);
					ImGui::SameLine();
					espRgb("##esp_zoom", EspUiColors::scoped_label, "Scoped label");

					edited::Checkbox("Flash Flag", "", &Settings::Visuals::showBlind);
					ImGui::SameLine();
					espRgb("##esp_flash", EspUiColors::blind_label, "Flashed label");

					edited::Checkbox("Sniper Crosshair", "", &Settings::Visuals::awpCrosshair);
					ImGui::SameLine();
					espRgb("##esp_awpx", EspUiColors::sniper_crosshair, "Sniper crosshair");
				};

				auto draw_esp_other = [&]() {
					edited::Checkbox("Dropped Weapon ESP", "", &Settings::Visuals::droppedWeaponEsp);
					ImGui::SameLine();
					espRgb("##dw_dropcol", EspUiColors::dropped_weapon_esp, "Dropped Weapon Text/Icon Color");

					edited::Checkbox("Weapon Name", "", &Settings::Visuals::droppedWeaponText);

					edited::Checkbox("Weapon Icon", "", &Settings::Visuals::droppedWeaponIcons);

					edited::Checkbox("Bomb World Color", "", &Settings::Visuals::bombWorldEsp);
					ImGui::SameLine();
					espRgb("##esp_bombw", EspUiColors::bomb_esp_col, "Bomb Marker Color");

					edited::Checkbox("World Grenades", "", &Settings::Visuals::worldGrenades);

					if (Settings::Visuals::worldGrenades)
						edited::Checkbox("Grenade Icons", "", &Settings::Visuals::worldGrenadeIcons);

					edited::Checkbox("Molotov Hull", "", &Settings::Visuals::worldInfernoHull);

					edited::Checkbox("Grenade lineups", "", &Settings::Visuals::grenadeLineups);
					if (Settings::Visuals::grenadeLineups) {
						ImGui::PushID("grenade_lineups_browser");
						ImGui::Indent(8.f);

						const bool loaded = ExpectionalLineupsLoaded();
						const std::size_t total_packs = ExpectionalLineupPacksTotalCount();
						const std::size_t total_lineups = ExpectionalLineupsTotalCount();
						if (!loaded) {
							ImGui::TextDisabled("Scanning Steam workshop... (packs: %zu)", total_packs);
						} else {
							ImGui::Text("Packs: %zu  |  Lineups: %zu", total_packs, total_lineups);
						}
						ImGui::SameLine();
						if (ImGui::SmallButton("Rescan workshop"))
							ExpectionalLineupBrowserRefreshWorkshop();

						edited::SliderFloat(
							"Max draw distance",
							"0 = unlimited",
							&Settings::Visuals::grenadeLineupMaxDrawDistance,
							0.f,
							2500.f,
							"%.0f");

						static std::vector<std::string> s_maps;
						static std::vector<ExpectionalLineupBrowserPack> s_packs;
						static std::string s_sel_map;
						static int s_last_count = -1;
						static bool s_warning_acked = false;
						static std::string s_pending_pack_id;  
						static bool s_open_warning_popup = false;

						const int cur_count = static_cast<int>(total_packs);
						if (cur_count != s_last_count) {
							s_last_count = cur_count;
							s_maps = ExpectionalLineupBrowserMapList();
							if (!s_maps.empty() && std::find(s_maps.begin(), s_maps.end(), s_sel_map) == s_maps.end())
								s_sel_map.clear();
							s_packs = s_sel_map.empty() ? std::vector<ExpectionalLineupBrowserPack>{}
							                            : ExpectionalLineupBrowserPacksForMap(s_sel_map);
						}

						const float row_h = ImGui::GetFrameHeightWithSpacing() * 11.f;
						const float full_w = ImGui::GetContentRegionAvail().x;
						const float left_w = (std::max)(140.f, full_w * 0.32f);

						ImGui::BeginChild("##lu_maps", ImVec2(left_w, row_h), true);
						ImGui::TextDisabled("Maps");
						ImGui::Separator();
						if (s_maps.empty()) {
							ImGui::TextDisabled("No maps found.\nSubscribe to a\nCS2 workshop\nlineup addon\non Steam, then\nclick 'Rescan'.");
						} else {
							for (const std::string& m : s_maps) {
								const bool sel = (m == s_sel_map);
								if (ImGui::Selectable(m.c_str(), sel)) {
									s_sel_map = m;
									s_packs = ExpectionalLineupBrowserPacksForMap(m);
								}
							}
						}
						ImGui::EndChild();

						ImGui::SameLine();
						ImGui::BeginChild("##lu_packs", ImVec2(0.f, row_h), true);
						if (s_sel_map.empty()) {
							ImGui::TextDisabled("Select a map on the left to see its lineup packs.");
						} else {
							const std::size_t active_total = ExpectionalLineupBrowserActiveCount();
							const std::size_t active_on_map = ExpectionalLineupBrowserActiveCountForMap(s_sel_map);
							ImGui::Text("%s  (%zu packs)  |  Active here: %zu  |  Total: %zu",
							    s_sel_map.c_str(), s_packs.size(), active_on_map, active_total);
							ImGui::Separator();
							if (s_packs.empty()) {
								ImGui::TextDisabled("No packs on this map.");
							}
							for (const ExpectionalLineupBrowserPack& p : s_packs) {
								const bool was_active = ExpectionalLineupBrowserActiveContains(p.id);
								bool want_active = was_active;
								ImGui::PushID(p.id.c_str());
								if (ImGui::Checkbox("##act", &want_active)) {
									if (want_active && !was_active) {
										
										const std::size_t same_map = ExpectionalLineupBrowserActiveCountForMap(p.map);
										if (same_map >= 1 && !s_warning_acked) {
											s_pending_pack_id = p.id;
											s_open_warning_popup = true;
										} else {
											ExpectionalLineupBrowserActiveAdd(p.id);
										}
									} else if (!want_active && was_active) {
										ExpectionalLineupBrowserActiveRemove(p.id);
									}
								}
								ImGui::SameLine();
								if (was_active)
									ImGui::TextColored(ImVec4(0.55f, 1.f, 0.55f, 1.f),
									    "[on] %s  (%zu lineups)", p.title.c_str(), p.lineup_count);
								else
									ImGui::Text("%s  (%zu lineups)", p.title.c_str(), p.lineup_count);
								if (ImGui::IsItemHovered())
									ImGui::SetTooltip("%s", p.id.c_str());
								ImGui::PopID();
							}
						}
						ImGui::EndChild();

						if (s_open_warning_popup) {
							s_open_warning_popup = false;
							ImGui::OpenPopup("Multiple packs warning");
						}
						const ImGuiViewport* vp = ImGui::GetMainViewport();
						ImGui::SetNextWindowPos(
						    ImVec2(vp->GetCenter().x, vp->GetCenter().y),
						    ImGuiCond_Always, ImVec2(0.5f, 0.5f));
						if (ImGui::BeginPopupModal("Multiple packs warning", nullptr,
						        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
							ImGui::TextWrapped(
							    "Warning: enabling more than one lineup pack on the same map is NOT recommended.");
							ImGui::Spacing();
							ImGui::TextWrapped(
							    "Overlapping guide sets on a single map can clutter the HUD, reduce readability, "
							    "and may confuse aim references. Use this only if you really need a backup pack.");
							ImGui::Spacing();
							ImGui::Checkbox("Don't show this again", &s_warning_acked);
							ImGui::Spacing();
							if (ImGui::Button("Enable anyway", ImVec2(150, 0))) {
								if (!s_pending_pack_id.empty())
									ExpectionalLineupBrowserActiveAdd(s_pending_pack_id);
								s_pending_pack_id.clear();
								ImGui::CloseCurrentPopup();
							}
							ImGui::SameLine();
							if (ImGui::Button("Cancel", ImVec2(120, 0))) {
								s_pending_pack_id.clear();
								ImGui::CloseCurrentPopup();
							}
							ImGui::EndPopup();
						}

						ImGui::Unindent(8.f);
						ImGui::PopID();
					}
				};

				if (esp_part < 0) {
					if (ImGui::BeginTabBar("##espSubTabs")) {
						if (ImGui::BeginTabItem("Players")) {
							draw_esp_players();
							ImGui::EndTabItem();
						}
						if (ImGui::BeginTabItem("Other")) {
							draw_esp_other();
							ImGui::EndTabItem();
						}
						ImGui::EndTabBar();
					}
				} else if (esp_part == 0) {
					draw_esp_players();
				} else if (esp_part == 1) {
					draw_esp_other();
				}
				break;
			}
			case 3: {
				auto draw_misc_left = [&]() {
					edited::Checkbox("Spectator list", "", &Settings::misc::spectatorList);
					edited::Checkbox("Bomb timer", "", &Settings::misc::bombTimer);
					edited::Checkbox("Rank Revealer", "", &Settings::misc::rank_reveal_window);
					edited::Checkbox("Watermark", "", &Settings::misc::water);
					if (Settings::misc::water) {
						edited::Checkbox("Overlay FPS##water", "", &Settings::misc::waterShowOverlayFps);
						edited::Checkbox("Game FPS##water", "", &Settings::misc::waterShowGameFps);
						edited::Checkbox("Ping##water", "", &Settings::misc::waterShowPing);
					}
					{
						const char* hsItems[] = { "Off", "Neverlose", "Skeet" };
						edited::Combo("Hit sound", "", &Settings::misc::hit_sound, hsItems, IM_ARRAYSIZE(hsItems));
						if (Settings::misc::hit_sound < 0)
							Settings::misc::hit_sound = 0;
						else if (Settings::misc::hit_sound > 2)
							Settings::misc::hit_sound = 2;
					}
					edited::Checkbox("Hit marker", "", &Settings::misc::hit_marker);
					edited::Checkbox("Bunny hop", "", &Settings::misc::bhop);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip(
							"Bunnyhop needs console bind, please copy and paste enable bind to console.\n"
							"If u want to disable Bunnyhop please paste disable bind to console.");
					ImGui::SameLine();
					if (ImGui::SmallButton("Enable Bind##bhop_console_on")) {
						ExpectionalCopyAsciiToClipboard(kExpectionalBhopConsoleSetup);
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip(
							"Paste:\n%s\n\nThis Command Into CS2 Console.",
							kExpectionalBhopConsoleSetup);
					ImGui::SameLine();
					if (ImGui::SmallButton("Disable Bind##bhop_console_off")) {
						ExpectionalCopyAsciiToClipboard(kExpectionalBhopConsoleRestore);
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip(
							"Paste:\n%s\n\nThis Command Into CS2 Console.",
							kExpectionalBhopConsoleRestore);
					edited::Checkbox("Keybind list", "", &Settings::misc::keybind_list_window);
					edited::Checkbox("Stream proof", "OBS/Discord etc. bypass", &Settings::misc::obsBypass);
				};
				auto draw_misc_right = [&]() { cloud_radar_render_menu_misc(); };

				if (esp_part < 0) {
					draw_misc_left();
					ImGui::Spacing();
					draw_misc_right();
				} else if (esp_part == 0) {
					draw_misc_left();
				} else {
					window_radar_render_menu_misc();
					ImGui::Spacing();
					draw_misc_right();
				}
				ImGui::Spacing();
				break;
			}
			case 4: {
				static char cfgNameBuf[96] = "default";
				static int cfgSel = 0;
				static std::vector<std::string> cfgList;
				static char statusMsg[160] = "";
				static int s_cfgTabPrevSel = -1;

				if (s_cfgTabPrevSel != s_mainTabSel) {
					if (s_mainTabSel == 4)
						cfgList = ExpectionalConfigList();
					s_cfgTabPrevSel = s_mainTabSel;
				}

				ImGui::TextUnformatted("Menu toggle key");
				ImGui::SameLine();
				HotkeyButtonMenu(hotkeys::menukey.load(), menu_keystatus.load());
				ImGui::Spacing();
				edited::Checkbox("Team Check	", "", &Settings::Visuals::enemiesOnly);
				ImGui::Spacing();
				edited::Checkbox("Overlay Custom FPS", "", &Settings::misc::overlayCustomFps);
				if (Settings::misc::overlayCustomFps) {
					if (Settings::misc::overlayCustomFpsValue < 60)
						Settings::misc::overlayCustomFpsValue = 60;
					edited::SliderInt("Overlay FPS##ocfps", "", &Settings::misc::overlayCustomFpsValue, 60, 1000);
					if (Settings::misc::overlayCustomFpsValue < 60)
						Settings::misc::overlayCustomFpsValue = 60;
				}
				ImGui::Spacing();
				ImGui::TextWrapped("Folder: %%localappdata%%\\Expectional\\configs\\");
				if (ImGui::Button("Refresh")) {
					cfgList = ExpectionalConfigList();
					if (cfgSel >= (int)cfgList.size())
						cfgSel = 0;
					statusMsg[0] = '\0';
				}
				ImGui::BeginChild("##cfglistbox", ImVec2(0, 140), true);
				for (int i = 0; i < (int)cfgList.size(); ++i) {
					if (ImGui::Selectable(cfgList[i].c_str(), cfgSel == i)) {
						cfgSel = i;
						snprintf(cfgNameBuf, sizeof cfgNameBuf, "%s", cfgList[(size_t)i].c_str());
					}
				}
				ImGui::EndChild();
				
				ImGui::InputText("##cfgname", cfgNameBuf, sizeof cfgNameBuf);
				if (ImGui::Button("Save")) {
					if (ExpectionalConfigSave(cfgNameBuf)) {
						snprintf(ExpectionalActiveCfgName, sizeof ExpectionalActiveCfgName, "%s", cfgNameBuf);
						snprintf(statusMsg, sizeof statusMsg, "Saved: %s", cfgNameBuf);
					} else
						snprintf(statusMsg, sizeof statusMsg, "Save Failed");
					cfgList = ExpectionalConfigList();
				}
				ImGui::SameLine();
				if (ImGui::Button("Load")) {
					if (ExpectionalConfigLoad(cfgNameBuf)) {
						ImGui::ClearActiveID();
						snprintf(ExpectionalActiveCfgName, sizeof ExpectionalActiveCfgName, "%s", cfgNameBuf);
						snprintf(statusMsg, sizeof statusMsg, "Loaded: %s", cfgNameBuf);
					} else
						snprintf(statusMsg, sizeof statusMsg, "Load Failed: %s", cfgNameBuf);
				}
				ImGui::SameLine();
				if (ImGui::Button("Delete")) {
					if (ExpectionalConfigDelete(cfgNameBuf)) {
						cfgList = ExpectionalConfigList();
						cfgSel = 0;
						snprintf(statusMsg, sizeof statusMsg, "Deleted: %s", cfgNameBuf);
					} else
						snprintf(statusMsg, sizeof statusMsg, "Delete Failed");
				}
				if (statusMsg[0])
					ImGui::TextWrapped("%s", statusMsg);
				break;
			}
			default:
				break;
			}
}

#include "shade_gui.hpp"

inline float ExpectionalCombatPanelHeight() {
	const float base = 442.f;
	if (Settings::weapon_cfg::editor_category_idx > 0)
		return base - 34.f;
	return base;
}

inline void Expectional_Menu_OnShadeGuiDraw()
{
	using namespace GUI;
	if (page == 0) {
		ExpectionalWeaponCategoryInnerTabs("aimcat");
		ExpectionalDrawCategoryMasterSwitch();
		ExpectionalAlignMenuContentX();
		const float combatPanelH = ExpectionalCombatPanelHeight();
		BeginPanelH("##aim_l", combatPanelH);
		ImGui::BeginChild("##aim_sc", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		ImGui::TextColored(c::accent, "Aimbot");
		ImGui::Separator();
		ExpectionalDrawMenuTabBody(0);
		ImGui::EndChild();
		EndPanel("", cPos, cSize);
		ImGui::SameLine(0, kPanelGap);
		BeginPanelH("##aim_r", combatPanelH);
		ImGui::BeginChild("##aim_rsc", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		ImGui::TextColored(c::accent, "RCS");
		ImGui::Separator();
		ExpectionalDrawRcsPanel();
		ImGui::EndChild();
		EndPanel("", cPos, cSize);
	} else if (page == 1) {
		ExpectionalWeaponCategoryInnerTabs("trgcat");
		ExpectionalDrawCategoryMasterSwitch();
		ExpectionalAlignMenuContentX();
		const float combatPanelH = ExpectionalCombatPanelHeight();
		BeginPanelH("##trg_l", combatPanelH);
		ImGui::BeginChild("##trg_sc", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		ImGui::TextColored(c::accent, "Triggerbot");
		ImGui::Separator();
		ExpectionalDrawMenuTabBody(1);
		ImGui::EndChild();
		EndPanel("", cPos, cSize);
		ImGui::SameLine(0, kPanelGap);
		BeginPanelH("##trg_r", combatPanelH);
		ImGui::BeginChild("##trg_rsc", ImVec2(0, 0), false, 0);
		ImGui::EndChild();
		EndPanel("", cPos, cSize);
	} else if (page == 2) {
		ExpectionalAlignMenuContentX();
		BeginPanel("##esp_l");
		ImGui::BeginChild("##esp_psc", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		ImGui::TextColored(c::accent, "Players");
		ImGui::Separator();
		ExpectionalDrawMenuTabBody(2, 0);
		ImGui::EndChild();
		EndPanel("", cPos, cSize);
		ImGui::SameLine(0, kPanelGap);
		BeginPanel("##esp_r");
		ImGui::BeginChild("##esp_osc", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		ImGui::TextColored(c::accent, "Other");
		ImGui::Separator();
		ExpectionalDrawMenuTabBody(2, 1);
		ImGui::EndChild();
		EndPanel("", cPos, cSize);
	} else if (page == 3) {
		ExpectionalAlignMenuContentX();
		BeginPanel("##misc_l");
		ImGui::BeginChild("##misc_psc", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		ImGui::TextColored(c::accent, "General");
		ImGui::Separator();
		ExpectionalDrawMenuTabBody(3, 0);
		ImGui::EndChild();
		EndPanel("", cPos, cSize);
		ImGui::SameLine(0, kPanelGap);
		BeginPanel("##misc_r");
		ImGui::BeginChild("##misc_osc", ImVec2(0, 0), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
		ImGui::TextColored(c::accent, "Radar");
		ImGui::Separator();
		ExpectionalDrawMenuTabBody(3, 1);
		ImGui::EndChild();
		EndPanel("", cPos, cSize);
	} else if (page == 4) {
		ImGui::BeginChild("##shade_cfg_w", ImVec2(960, 480), ImGuiChildFlags_None,
		    ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_AlwaysVerticalScrollbar);
		ExpectionalDrawMenuTabBody(4);
		ImGui::EndChild();
	}
}

inline void drawmenu()
{
	{
		int mk = hotkeys::menukey.load();
		if (mk <= 0 || mk > 255)
			mk = VK_HOME;
		
		static bool s_menuKeyDownPrev = false;
		const bool down = (GetAsyncKeyState(mk) & 0x8000) != 0;
		if (down && !s_menuKeyDownPrev)
			Settings::bMenu = !Settings::bMenu;
		s_menuKeyDownPrev = down;
	}

	if (Settings::bMenu) {
		MenuConfig::ShowMenu = true;

		const ImGuiStyle style_backup = ImGui::GetStyle();
		ExpectionalOsMenu_ApplyStyle(&ImGui::GetStyle());

		const bool osMenuFonts = ExpectionalOsMenu_FontsReady();
		if (osMenuFonts && font::lexend_regular)
			ImGui::PushFont(font::lexend_regular);

		GUI::DrawGui();

		if (!MenuConfig::ShowMenu)
			Settings::bMenu = false;

		ExpectionalTryAutosaveAfterMenuInteraction();

		if (osMenuFonts && font::lexend_regular)
			ImGui::PopFont();

		const int page = GUI::page;
		ImGuiWindow* shadeWin = ImGui::FindWindowByName("Expectional");
		if (shadeWin && (page == 0 || page == 1)) {
			const ImVec2 vac_menu_pos = shadeWin->Pos;
			const ImVec2 vac_menu_sz = shadeWin->Size;
			ImGui::SetNextWindowPos(ImVec2(vac_menu_pos.x, vac_menu_pos.y), ImGuiCond_Always, ImVec2(0.f, 1.f));
			ImGui::SetNextWindowSize(ImVec2(vac_menu_sz.x, 0.f), ImGuiCond_Always);
			ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.09f, 0.97f));
			ImGui::PushStyleColor(ImGuiCol_Border, c::accent);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 4.f);
			ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 10.f));
			ExpectionalOsMenu_UiFontScope vacUiFont;
			ImGui::Begin("##VacLiveWarnBanner", nullptr,
			    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
			    | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoNavFocus);
			const float wrapW = (vac_menu_sz.x > 48.f) ? (vac_menu_sz.x - 24.f) : 24.f;
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + wrapW);
			ImGui::TextUnformatted("Due to recent ");
			ImGui::SameLine(0.f, 0.f);
			ImGui::TextColored(c::accent, "VAC Live");
			ImGui::SameLine(0.f, 0.f);
			ImGui::TextUnformatted(" algorithm updates, using aim features may result in temporary cooldowns.");
			ImGui::PopTextWrapPos();
			ImGui::End();
			ImGui::PopStyleVar(2);
			ImGui::PopStyleColor(2);
		}

		if (page == 2)
			ExpectionalDrawEspPreview();

		ImGui::GetStyle() = style_backup;
	} else
		MenuConfig::ShowMenu = false;
}
