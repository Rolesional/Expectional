#pragma once

#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include "../../Includes/Imgui/imgui.h"
#include "shade_menu_config_stub.hpp"

#include <d3d11.h>

inline ID3D11ShaderResourceView* Logo = NULL;
inline ID3D11ShaderResourceView* BombLogo = NULL;
inline ID3D11ShaderResourceView* EnemyIcon = NULL;
inline ID3D11ShaderResourceView* TeamIcon = NULL;
inline ID3D11ShaderResourceView* CloudIcon = NULL;
inline ID3D11ShaderResourceView* SpotifyLogo = NULL;
inline ID3D11ShaderResourceView* SpotifyPlay = NULL;
inline ID3D11ShaderResourceView* SpotifyPre = NULL;
inline ID3D11ShaderResourceView* SpotifyNext = NULL;
inline ID3D11ShaderResourceView* GlobalLogo = NULL;
inline ID3D11ShaderResourceView* MsIcon = NULL;
inline ID3D11ShaderResourceView* SettingsIcon = NULL;
inline ID3D11ShaderResourceView* KeybindIcon = NULL;
inline ID3D11ShaderResourceView* FpsIcon = NULL;
inline ID3D11ShaderResourceView* NotificationIcon = NULL;

inline int LogoW = 0, LogoH = 0;
inline int BombW = 0, BombH = 0;
inline int SpotifyLogoW = 0, SpotifyLogoH = 0;
inline int SpotifyPlayW = 0, SpotifyPlayH = 0;
inline int SpotifyPreW = 0, SpotifyPreH = 0;
inline int SpotifyNextW = 0, SpotifyNextH = 0;
inline int GlobalW = 0, GlobalH = 0;
inline int MsW = 0, MsH = 0;
inline int SettingsW = 0, SettingsH = 0;
inline int KeybindW = 0, KeybindH = 0;
inline int FpsW = 0, FpsH = 0;
inline int NotificationW = 0, NotificationH = 0;
inline int EnemyW = 0, EnemyH = 0;
inline int TeamW = 0, TeamH = 0;
inline int CloudW = 0, CloudH = 0;

void Expectional_Menu_OnShadeGuiDraw();

namespace GUI
{
	inline ImGuiColorEditFlags picker_flags =
	    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreview;
	inline int page = 0;
	inline int visual_sub_tab = 0;
	inline ImVec2 cPos, cSize;
	inline constexpr float kMenuWinW = 1000.f;
	inline constexpr float kMenuWinH = 600.f;
	inline constexpr float kPanelColW = 470.f;
	inline constexpr float kPanelGap = 20.f;
	inline constexpr float kContentStartX = (kMenuWinW - (kPanelColW * 2.f + kPanelGap)) * 0.5f;

#define BeginPanel(name) \
	ImGui::BeginChild(name, ImVec2(GUI::kPanelColW, 480), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground); \
	cPos = ImGui::GetCursorPos(); \
	cSize = ImVec2(GUI::kPanelColW, 480);

#define BeginPanelH(name, height) \
	ImGui::BeginChild(name, ImVec2(GUI::kPanelColW, height), ImGuiChildFlags_None, ImGuiWindowFlags_NoBackground); \
	cPos = ImGui::GetCursorPos(); \
	cSize = ImVec2(GUI::kPanelColW, height);

#define EndPanel(title, pos, size) \
	ImGui::EndChild();

	template<typename T>
	inline T ImLerp(T a, T b, float t)
	{
		return (T)(a + (b - a) * t);
	}

	inline void LoadDefaultConfig() {}

	inline void LoadImages()
	{
		static bool s_done = false;
		if (s_done)
			return;
		s_done = true;
		MenuConfig::MarkWinPos = ImVec2(ImGui::GetIO().DisplaySize.x - 300.0f, 100.f);
		MenuConfig::RadarWinPos = ImVec2(25.f, 25.f);
		MenuConfig::SpecWinPos = ImVec2(10.0f, ImGui::GetIO().DisplaySize.y / 2 - 200);
		MenuConfig::BombWinPos = ImVec2((ImGui::GetIO().DisplaySize.x - 200.0f) / 2.0f, 80.0f);
	}

	inline void Decoration()
	{
		ImVec2 pos = ImGui::GetWindowPos();
		ImVec2 size = ImVec2(kMenuWinW, kMenuWinH);
		ImGui::GetWindowDrawList()->AddRectFilled(pos, pos + size, ImGui::GetColorU32(c::background::filling), 6);
		ImGui::GetWindowDrawList()->AddRect(pos, pos + size, ImGui::GetColorU32(c::background::stroke), 6, 0, 1.5f);
		ImGui::GetWindowDrawList()->AddLine(ImVec2(pos.x, pos.y + 40), ImVec2(pos.x + size.x, pos.y + 40),
		    ImGui::GetColorU32(c::background::stroke), 1.5f);
		if (font::lexend_bold) {
			const ImVec2 title0 = ImVec2(pos.x + 20, pos.y + 10);
			const ImU32 colAccent = ImGui::ColorConvertFloat4ToU32(c::accent);
			const ImU32 colWhite = IM_COL32(255, 255, 255, 255);
			ImGui::GetWindowDrawList()->AddText(font::lexend_bold, 22, title0, colAccent, "Expect");
			ImVec2 expectSz = font::lexend_bold->CalcTextSizeA(22, FLT_MAX, 0.0f, "Expect");
			ImGui::GetWindowDrawList()->AddText(font::lexend_bold, 22,
			    ImVec2(title0.x + expectSz.x, title0.y), colWhite, "ional");
		}
	}

	inline void DrawGui()
	{
		LoadImages();
		if (!MenuConfig::ShowMenu)
			return;

		ImGui::SetNextWindowSize(ImVec2(kMenuWinW, kMenuWinH));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.07f, 0.07f, 0.09f, 0.97f));
		ImGui::Begin("Expectional", &MenuConfig::ShowMenu, ImGuiWindowFlags_NoDecoration);
		{
			Decoration();

			{
				const char* tab_labels[] = { "Aimbot", "Triggerbot", "ESP", "Misc", "Config" };
				const char* tab_icons[] = { "a", "e", "b", "c", "d" };
				constexpr float kNavTabW = 80.f;
				constexpr float kNavTabSpacing = 8.f;
				constexpr int kNavTabCount = 5;
				ImGui::SetCursorPos(ImVec2(kContentStartX, 560));
				ImGui::BeginGroup();
				for (int i = 0; i < kNavTabCount; ++i) {
					if (edited::Tab(page == i, tab_icons[i], tab_labels[i], ImVec2(kNavTabW, 30)))
						page = i;
					if (i < kNavTabCount - 1)
						ImGui::SameLine(0, kNavTabSpacing);
				}
				ImGui::EndGroup();
			}

			ImGui::SetCursorPos(ImVec2(kContentStartX, 60));
			Expectional_Menu_OnShadeGuiDraw();
		}
		ImGui::End();
		ImGui::PopStyleColor();
	}
}
