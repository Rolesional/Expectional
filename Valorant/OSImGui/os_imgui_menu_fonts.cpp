#include "os_imgui_menu.hpp"
#include "Font.h"
#include "../../Includes/Imgui/imgui.h"

static bool s_fontsInit = false;

void ExpectionalOsMenu_InitFonts()
{
	if (s_fontsInit)
		return;
	ImGuiIO& io = ImGui::GetIO();
	ImFontConfig cfg{};
	cfg.FontDataOwnedByAtlas = false;

	font::lexend_general_bold =
	    io.Fonts->AddFontFromMemoryTTF(lexend_bold, sizeof(lexend_bold), 18.f, &cfg, io.Fonts->GetGlyphRangesDefault());
	font::lexend_bold =
	    io.Fonts->AddFontFromMemoryTTF(lexend_regular, sizeof(lexend_regular), 17.f, &cfg, io.Fonts->GetGlyphRangesDefault());
	font::lexend_regular =
	    io.Fonts->AddFontFromMemoryTTF(lexend_regular, sizeof(lexend_regular), 14.f, &cfg, io.Fonts->GetGlyphRangesDefault());
	font::icomoon =
	    io.Fonts->AddFontFromMemoryTTF(icomoon, sizeof(icomoon), 20.f, &cfg, io.Fonts->GetGlyphRangesDefault());
	font::icomoon_widget = io.Fonts->AddFontFromMemoryTTF(
	    icomoon_widget, sizeof(icomoon_widget), 15.f, &cfg, io.Fonts->GetGlyphRangesDefault());
	font::icomoon_widget2 =
	    io.Fonts->AddFontFromMemoryTTF(icomoon, sizeof(icomoon), 16.f, &cfg, io.Fonts->GetGlyphRangesDefault());

	s_fontsInit = true;
}

bool ExpectionalOsMenu_FontsReady()
{
	return s_fontsInit && font::lexend_regular != nullptr;
}
