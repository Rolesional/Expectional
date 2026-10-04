#define IMGUI_DEFINE_MATH_OPERATORS
#include "os_imgui_menu.hpp"
#include "../../Includes/Imgui/imgui_internal.h"

#include <map>

void ExpectionalOsMenu_ApplyStyle(ImGuiStyle* style)
{
	ImVec4* Colors = style->Colors;
	const ImVec4 stroke = c::background::stroke;
	const ImVec4 accent = c::accent;
	Colors[ImGuiCol_Text] = c::elements::text_active;
	Colors[ImGuiCol_TextDisabled] = c::elements::text;
	Colors[ImGuiCol_WindowBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
	Colors[ImGuiCol_ChildBg] = ImVec4(0.f, 0.f, 0.f, 0.f);
	Colors[ImGuiCol_PopupBg] = ImVec4(19.f / 255.f, 19.f / 255.f, 19.f / 255.f, 0.94f);
	Colors[ImGuiCol_Border] = stroke;
	Colors[ImGuiCol_BorderShadow] = ImVec4(0.f, 0.f, 0.f, 0.24f);
	Colors[ImGuiCol_FrameBg] = c::elements::background_widget;
	Colors[ImGuiCol_FrameBgHovered] = ImVec4(35.f / 255.f, 35.f / 255.f, 35.f / 255.f, 1.f);
	Colors[ImGuiCol_FrameBgActive] = ImVec4(40.f / 255.f, 40.f / 255.f, 40.f / 255.f, 1.f);
	Colors[ImGuiCol_CheckMark] = accent;
	Colors[ImGuiCol_SliderGrab] = accent;
	Colors[ImGuiCol_SliderGrabActive] =
	    ImVec4(ImMin(accent.x * 1.08f, 1.f), ImMin(accent.y * 1.08f, 1.f), ImMin(accent.z * 1.08f, 1.f), 1.f);
	Colors[ImGuiCol_Button] = c::elements::background_widget;
	Colors[ImGuiCol_ButtonHovered] = ImVec4(35.f / 255.f, 35.f / 255.f, 35.f / 255.f, 1.f);
	Colors[ImGuiCol_ButtonActive] = ImVec4(45.f / 255.f, 45.f / 255.f, 45.f / 255.f, 1.f);
	Colors[ImGuiCol_Header] = ImVec4(25.f / 255.f, 25.f / 255.f, 25.f / 255.f, 0.55f);
	Colors[ImGuiCol_HeaderHovered] = ImVec4(35.f / 255.f, 35.f / 255.f, 35.f / 255.f, 1.f);
	Colors[ImGuiCol_HeaderActive] = ImVec4(40.f / 255.f, 40.f / 255.f, 40.f / 255.f, 1.f);
	Colors[ImGuiCol_Tab] = ImVec4(17.f / 255.f, 17.f / 255.f, 17.f / 255.f, 1.f);
	Colors[ImGuiCol_TabHovered] = ImVec4(35.f / 255.f, 35.f / 255.f, 35.f / 255.f, 1.f);
	Colors[ImGuiCol_TabActive] = ImVec4(28.f / 255.f, 28.f / 255.f, 28.f / 255.f, 1.f);
	Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.f);
	Colors[ImGuiCol_TitleBgActive] = Colors[ImGuiCol_TitleBg];
	Colors[ImGuiCol_ScrollbarBg] = ImVec4(17.f / 255.f, 17.f / 255.f, 17.f / 255.f, 1.f);
	Colors[ImGuiCol_ScrollbarGrab] = ImVec4(50.f / 255.f, 50.f / 255.f, 50.f / 255.f, 1.f);
	Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(62.f / 255.f, 62.f / 255.f, 62.f / 255.f, 1.f);
	Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(72.f / 255.f, 72.f / 255.f, 72.f / 255.f, 1.f);
	Colors[ImGuiCol_Separator] = stroke;
	Colors[ImGuiCol_SeparatorHovered] = ImVec4(44.f / 255.f, 44.f / 255.f, 44.f / 255.f, 1.f);
	Colors[ImGuiCol_SeparatorActive] = accent;
	Colors[ImGuiCol_ResizeGrip] = ImVec4(0.f, 0.f, 0.f, 0.f);
	Colors[ImGuiCol_TextSelectedBg] = ImVec4(accent.x, accent.y, accent.z, 0.35f);

	style->WindowPadding = ImVec2(0.f, 0.f);
	style->WindowBorderSize = 0.f;
	style->ChildRounding = c::elements::rounding;
	style->FrameRounding = c::elements::rounding;
	style->GrabRounding = c::elements::rounding;
	style->FrameBorderSize = 1.f;
	style->TabRounding = c::elements::rounding;
	style->WindowRounding = c::background::rounding;
	style->ScrollbarSize = 14.f;
	style->ScrollbarRounding = c::elements::rounding;
	style->ItemSpacing = ImVec2(8.f, 6.f);
	style->ItemInnerSpacing = ImVec2(8.f, 4.f);
}

void ExpectionalOsMenu_DrawDecoration(const char* titleWhite, const char* titleAccentSuffix)
{
	const ImVec2 pos = ImGui::GetWindowPos();
	ImDrawList* dl = ImGui::GetWindowDrawList();
	const float fw = c::background::size.x;
	const float fh = c::background::size.y;
	const ImVec2 br(pos.x + fw, pos.y + fh);
	const ImU32 fill = ImGui::GetColorU32(c::background::filling);
	const ImU32 strk = ImGui::GetColorU32(c::background::stroke);
	const float r = c::background::rounding;
	dl->AddRectFilled(pos, br, fill, r);
	dl->AddRect(pos, br, strk, r, 0, 1.5f);
	dl->AddLine(ImVec2(pos.x, pos.y + 40.f), ImVec2(pos.x + fw, pos.y + 40.f), strk, 1.5f);

	ImFont* f = font::lexend_bold ? font::lexend_bold : ImGui::GetFont();
	const float fs = 22.f;
	const ImVec2 tp(pos.x + 20.f, pos.y + 10.f);
	dl->AddText(f, fs, tp, IM_COL32(255, 255, 255, 255), titleWhite);
	const ImVec2 tw = f->CalcTextSizeA(fs, FLT_MAX, 0.f, titleWhite);
	dl->AddText(f, fs, ImVec2(tp.x + tw.x, tp.y), ImGui::GetColorU32(c::accent), titleAccentSuffix);
}

bool ExpectionalOsMenu_Tab(bool selected, const char* icon, const char* label, const ImVec2& size_arg)
{
	ImGuiWindow* window = ImGui::GetCurrentWindow();
	if (window->SkipItems)
		return false;

	ImGuiContext& g = *GImGui;
	const ImGuiID id = window->GetID(label);
	ImFont* icon_font = font::icomoon;
	ImFont* text_font = font::lexend_bold ? font::lexend_bold : g.Font;
	const float fs = g.FontSize;
	const ImVec2 label_size = text_font->CalcTextSizeA(fs, FLT_MAX, 0.f, label);
	const ImVec2 icon_size =
	    (icon_font && icon && icon[0]) ? icon_font->CalcTextSizeA(fs, FLT_MAX, 0.f, icon) : ImVec2(0.f, 0.f);
	const float spacing = (icon_size.x > 0.f && label_size.x > 0.f) ? 5.f : 0.f;
	const float total_width = icon_size.x + spacing + label_size.x;

	const ImVec2 pos = window->DC.CursorPos;
	const ImVec2 size = ImGui::CalcItemSize(size_arg, total_width, ImMax(label_size.y, icon_size.y));
	const ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));
	ImGui::ItemSize(size, 0.f);
	if (!ImGui::ItemAdd(bb, id))
		return false;

	bool hovered = false, held = false;
	const bool pressed = ImGui::ButtonBehavior(bb, id, &hovered, &held);

	const ImVec4 target =
	    selected ? c::elements::text_active : (hovered ? c::elements::text_active : c::elements::text);
	const float t = ImClamp(g.IO.DeltaTime * 6.f, 0.f, 1.f);

	static std::map<ImGuiID, ImVec4> s_textCol;
	auto it = s_textCol.find(id);
	if (it == s_textCol.end()) {
		s_textCol[id] = target;
		it = s_textCol.find(id);
	} else {
		it->second = ImLerp(it->second, target, t);
	}

	ImVec2 text_pos(bb.Min.x + (size.x - total_width) * 0.5f,
	    bb.Min.y + (size.y - ImMax(label_size.y, icon_size.y)) * 0.5f);
	if (icon_font && icon && icon[0]) {
		window->DrawList->AddText(icon_font, fs, text_pos, ImGui::GetColorU32(it->second), icon);
		text_pos.x += icon_size.x + spacing;
	}
	window->DrawList->AddText(text_font, fs, text_pos, ImGui::GetColorU32(it->second), label);
	return pressed;
}
