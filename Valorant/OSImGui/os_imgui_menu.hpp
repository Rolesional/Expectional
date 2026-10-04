#pragma once
#include "../../Includes/Imgui/imgui.h"
#include "../../Includes/Imgui/imgui_internal.h"
#include <algorithm>
#include "shade_imgui_settings.h"

void ExpectionalOsMenu_InitFonts();
bool ExpectionalOsMenu_FontsReady();

void ExpectionalOsMenu_ApplyStyle(ImGuiStyle* style);
void ExpectionalOsMenu_DrawDecoration(const char* titleWhite, const char* titleAccentSuffix);
bool ExpectionalOsMenu_Tab(bool selected, const char* icon, const char* label, const ImVec2& size_arg);

struct ExpectionalOsMenu_UiFontScope {
	int pushed = 0;
	ExpectionalOsMenu_UiFontScope()
	{
		if (ExpectionalOsMenu_FontsReady() && font::lexend_regular) {
			ImGui::PushFont(font::lexend_regular);
			pushed = 1;
		}
	}
	~ExpectionalOsMenu_UiFontScope()
	{
		if (pushed)
			ImGui::PopFont();
	}
	ExpectionalOsMenu_UiFontScope(const ExpectionalOsMenu_UiFontScope&) = delete;
	ExpectionalOsMenu_UiFontScope& operator=(const ExpectionalOsMenu_UiFontScope&) = delete;
};

struct ExpectionalOsMenu_HudStyleScope {
	ExpectionalOsMenu_UiFontScope font_;
	ExpectionalOsMenu_HudStyleScope()
	{
		const ImVec4 fill = c::background::filling;
		const ImVec4 accent = c::accent;
		ImGui::PushStyleColor(ImGuiCol_WindowBg, fill);
		ImGui::PushStyleColor(ImGuiCol_Border, c::background::stroke);
		ImGui::PushStyleColor(ImGuiCol_Text, c::elements::text_active);
		ImGui::PushStyleColor(ImGuiCol_TitleBg, fill);
		ImGui::PushStyleColor(ImGuiCol_TitleBgActive, fill);
		ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, fill);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogram, accent);
		ImGui::PushStyleColor(ImGuiCol_PlotHistogramHovered,
		    ImVec4(std::clamp(accent.x * 1.12f, 0.f, 1.f), std::clamp(accent.y * 1.12f, 0.f, 1.f), std::clamp(accent.z * 1.12f, 0.f, 1.f), 1.f));
		ImGui::PushStyleColor(ImGuiCol_FrameBg, c::elements::background_widget);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, c::background::rounding);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12.f, 10.f));
	}
	~ExpectionalOsMenu_HudStyleScope()
	{
		ImGui::PopStyleVar(3);
		ImGui::PopStyleColor(9);
	}
	ExpectionalOsMenu_HudStyleScope(const ExpectionalOsMenu_HudStyleScope&) = delete;
	ExpectionalOsMenu_HudStyleScope& operator=(const ExpectionalOsMenu_HudStyleScope&) = delete;
};

inline void ExpectionalHudDragFromLastItem(const char* drag_id) noexcept
{
	ImGuiWindow* win = ImGui::GetCurrentWindow();
	if (!win)
		return;
	const ImGuiID key_ox = win->GetID(drag_id);
	const ImGuiID key_oy = win->GetID("##hud_drag_oy");
	const ImGuiID key_on = win->GetID("##hud_drag_on");
	ImGuiStorage& st = win->StateStorage;
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		if (!st.GetBool(key_on, false)) {
			st.SetFloat(key_ox, win->Pos.x);
			st.SetFloat(key_oy, win->Pos.y);
			st.SetBool(key_on, true);
		}
		const ImVec2 origin(st.GetFloat(key_ox, win->Pos.x), st.GetFloat(key_oy, win->Pos.y));
		ImGui::SetWindowPos(origin + ImGui::GetMouseDragDelta(ImGuiMouseButton_Left));
	} else {
		st.SetBool(key_on, false);
	}
}

inline void ExpectionalHudDragBar(const char* id, float bar_h = 22.f) noexcept
{
	ImGuiWindow* win = ImGui::GetCurrentWindow();
	if (!win)
		return;
	const ImVec2 ws = ImGui::GetWindowSize();
	const ImGuiID key_ox = win->GetID(id);
	const ImGuiID key_oy = win->GetID("##hud_drag_oy");
	const ImGuiID key_on = win->GetID("##hud_drag_on");

	ImGui::SetCursorPos(ImVec2(0.f, 0.f));
	ImGui::InvisibleButton(id, ImVec2(ws.x, bar_h));
	ImGuiStorage& st = win->StateStorage;
	if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
		if (!st.GetBool(key_on, false)) {
			st.SetFloat(key_ox, win->Pos.x);
			st.SetFloat(key_oy, win->Pos.y);
			st.SetBool(key_on, true);
		}
		const ImVec2 origin(st.GetFloat(key_ox, win->Pos.x), st.GetFloat(key_oy, win->Pos.y));
		ImGui::SetWindowPos(origin + ImGui::GetMouseDragDelta(ImGuiMouseButton_Left));
	} else {
		st.SetBool(key_on, false);
	}
}
