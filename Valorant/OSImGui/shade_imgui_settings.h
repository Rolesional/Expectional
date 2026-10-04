#pragma once

#include "../../Includes/Imgui/imgui.h"

namespace font
{
	inline ImFont* icomoon = nullptr;
	inline ImFont* lexend_bold = nullptr;
	inline ImFont* lexend_regular = nullptr;
	inline ImFont* lexend_general_bold = nullptr;

	inline ImFont* icomoon_widget = nullptr;
	inline ImFont* icomoon_widget2 = nullptr;
	inline ImFont* weapon_icon = nullptr;

}

namespace c
{
	namespace other
	{
		static int notify_select = 0;
	}

	namespace tab
	{
		static float tab_alpha = 0.f;
		static float tab_add = 0.f;
		static int active_tab = 0;

		inline ImVec4 tab_active = ImColor(25, 25, 25);
		inline ImVec4 tab_active_rect = ImColor(35, 35, 35);

		inline ImVec4 border = ImColor(25, 25, 25);
	}

	inline ImVec4 accent = ImColor(205, 148, 255, 255);

	namespace background
	{

		inline ImVec4 filling = ImColor(17, 17, 17);
		inline ImVec4 stroke = ImColor(30, 30, 30);
		inline ImVec2 size = ImVec2(850, 600);

		inline float rounding = 6;

	}

	namespace elements
	{
		inline ImVec4 mark = ImColor(255, 255, 255);

		inline ImVec4 stroke = ImColor(17, 17, 17);
		inline ImVec4 background = ImColor(17, 17, 17);
		inline ImVec4 background_widget = ImColor(25, 25, 25);
		inline ImVec4 background_rect = ImColor(35, 35, 35);

		inline ImVec4 text_active = ImColor(255, 255, 255);
		inline ImVec4 text_hov = ImColor(200, 255, 200, 255);
		inline ImVec4 text = ImColor(130, 130, 130);

		inline float rounding = 4;
	}

	namespace child
	{

	}

}
