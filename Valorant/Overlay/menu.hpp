#pragma once
namespace hotkeys
{
	int aimkey;
}
static int keystatus = 0;
static int realkey = 0;

bool GetKey(int key)
{
	realkey = key;
	return true;
}
void ChangeKey(void* blank)
{
	keystatus = 1;
	while (true)
	{
		for (int i = 0; i < 0x87; i++)
		{
			if (GetKeyState(i) & 0x8000)
			{
				hotkeys::aimkey = i;
				keystatus = 0;
				return;
			}
		}
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
void HotkeyButton(int aimkey, void* changekey, int status)
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
		if (status == 0)
		{
			CreateThread(0, 0, (LPTHREAD_START_ROUTINE)changekey, nullptr, 0, nullptr);
		}
	}
}

int tab = 0;
class GradientLine {
public:

	static bool Render(ImVec2 size)
	{
		static ImColor gradient_colors[] =
		{
			//https://txwes.libguides.com/c.php?g=978475&p=7075536

			//Red
			ImColor(0, 0, 0),
			//Yellow
			ImColor(0, 0, 0),
			//Lime
			ImColor(0, 0, 0),
			//Cyan / Aqua
			ImColor(0, 0, 0),
			//Blue
			ImColor(0, 0, 0),
			//Magenta / Fuchsia
			ImColor(0, 0, 0),
			//Red
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

						//add one to color to create a skuffed blend
						gradient_colors[i], gradient_colors[i + 1], gradient_colors[i + 1], gradient_colors[i]
					);
				};

			render((pos)); render((pos - size.x));
		}
		return true;
	}
};


void drawmenu() {
	// menu made by aware :)
	if (GetAsyncKeyState(VK_HOME) & 1) { Settings::bMenu = !Settings::bMenu; }

	if (Settings::bMenu) {

		ImGuiStyle* style = &ImGui::GetStyle();

		ImVec4* Colors = style->Colors;
		Colors[ImGuiCol_Text] = ImVec4(1.00f, 1.00f, 1.00f, 1.00f);
		Colors[ImGuiCol_TextDisabled] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
		Colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
		Colors[ImGuiCol_ChildBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		Colors[ImGuiCol_PopupBg] = ImVec4(0.19f, 0.19f, 0.19f, 0.92f);
		Colors[ImGuiCol_Border] = ImVec4(0.19f, 0.19f, 0.19f, 0.59f);
		Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.24f);
		Colors[ImGuiCol_FrameBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
		Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.19f, 0.19f, 0.19f, 0.54f);
		Colors[ImGuiCol_FrameBgActive] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
		Colors[ImGuiCol_TitleBg] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
		Colors[ImGuiCol_TitleBgActive] = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);
		Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
		Colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
		Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.05f, 0.05f, 0.05f, 0.54f);
		Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
		Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.54f);
		Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
		Colors[ImGuiCol_CheckMark] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
		Colors[ImGuiCol_SliderGrab] = ImVec4(0.34f, 0.34f, 0.34f, 0.54f);
		Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.56f, 0.56f, 0.56f, 0.54f);
		Colors[ImGuiCol_Button] = ImVec4(0.15f, 0.15f, 0.15f, 0.54f);
		Colors[ImGuiCol_ButtonHovered] = ImVec4(0.29f, 0.29f, 0.29f, 0.54f);
		Colors[ImGuiCol_ButtonActive] = ImVec4(0.30f, 0.32f, 0.33f, 1.00f);
		Colors[ImGuiCol_Header] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
		Colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 0.00f, 0.00f, 0.36f);
		Colors[ImGuiCol_HeaderActive] = ImVec4(0.20f, 0.22f, 0.23f, 0.33f);
		Colors[ImGuiCol_Separator] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
		Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
		Colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
		Colors[ImGuiCol_ResizeGrip] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
		Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.44f, 0.44f, 0.44f, 0.29f);
		Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.44f, 0.47f, 1.00f);
		Colors[ImGuiCol_Tab] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
		Colors[ImGuiCol_TabHovered] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
		Colors[ImGuiCol_TabActive] = ImVec4(0.20f, 0.20f, 0.20f, 0.36f);
		Colors[ImGuiCol_TabUnfocused] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
		Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.14f, 0.14f, 0.14f, 1.00f);
		Colors[ImGuiCol_PlotLines] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
		Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
		Colors[ImGuiCol_PlotHistogram] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
		Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
		Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
		Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.00f, 0.00f, 0.00f, 0.52f);
		Colors[ImGuiCol_TableBorderLight] = ImVec4(0.28f, 0.28f, 0.28f, 0.29f);
		Colors[ImGuiCol_TableRowBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
		Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.00f, 1.00f, 1.00f, 0.06f);
		Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.22f, 0.23f, 1.00f);
		Colors[ImGuiCol_DragDropTarget] = ImVec4(0.33f, 0.67f, 0.86f, 1.00f);
		Colors[ImGuiCol_NavHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 1.00f);
		Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 0.00f, 0.00f, 0.70f);
		Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.20f);
		Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(1.00f, 0.00f, 0.00f, 0.35f);

		style->WindowPadding = ImVec2(8.00f, 8.00f);
		style->FramePadding = ImVec2(5.00f, 2.00f);
		style->CellPadding = ImVec2(6.00f, 6.00f);
		style->ItemSpacing = ImVec2(2.00f, 3.00f);
		style->ItemInnerSpacing = ImVec2(6.00f, 6.00f);
		style->TouchExtraPadding = ImVec2(0.00f, 0.00f);
		style->IndentSpacing = 25;
		style->ScrollbarSize = 16;
		style->GrabMinSize = 10;
		style->WindowBorderSize = 1;
		style->ChildBorderSize = 1;
		style->PopupBorderSize = 1;
		style->FrameBorderSize = 1;
		style->TabBorderSize = 1;
		style->LogSliderDeadzone = 4;
		style->ScrollbarRounding = 0;
		style->TabRounding = 0;
		style->WindowTitleAlign = ImVec2(0.5f, 0.5f);

	


		//ImGui::Begin(("cheat"), 0, ImGuiWindowFlags_::ImGuiWindowFlags_NoResize | ImGuiWindowFlags_::ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_::ImGuiWindowFlags_NoTitleBar);

		
		//ImGui::BeginChild(("##backround"), ImVec2(0, 0), true, ImGuiWindowFlags_NoScrollbar);
		{
		ImGui::SetNextWindowSize(ImVec2(450.000f, 550.000f), ImGuiCond_Once);

		ImGui::Begin(("Expectional (Undetected)"), 0, ImGuiWindowFlags_::ImGuiWindowFlags_NoResize | ImGuiWindowFlags_::ImGuiWindowFlags_NoCollapse);
		(ImGui::Text("Welcome!", ImVec2()));
		(ImGui::Text("Version: 1.8"));
		ImGui::Checkbox("Aimbot", &Settings::aimbot::aimbot);
		if (Settings::aimbot::aimbot) {
			ImGui::SliderFloat("Field Of View", &Settings::aimbot::aim_fov, 1, 120);
			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::SliderFloat("Smooth", &Settings::aimbot::smooth, 1, 120);
			ImGui::Spacing();
			ImGui::Text(("Key "));
			ImGui::SameLine();
			HotkeyButton(hotkeys::aimkey, ChangeKey, keystatus);
			ImGui::Spacing();
			ImGui::Spacing();
			ImGui::Combo(("Hitbox"), &Settings::aimbot::selectedhitbox, Settings::aimbot::hitbox, IM_ARRAYSIZE(Settings::aimbot::hitbox));

		}
		ImGui::Spacing();
		ImGui::Checkbox("Fov Circle", &Settings::aimbot::fov_circle);
		ImGui::Checkbox("Box ESP", &Settings::Visuals::bBox);
		if (Settings::Visuals::bBox) {
			ImGui::Combo("Style", &Settings::Visuals::boxMode, Settings::Visuals::boxStyle, IM_ARRAYSIZE(Settings::Visuals::boxStyle));
		}
		ImGui::Checkbox("Head ESP", &Settings::Visuals::headcircle);
		ImGui::Checkbox("Skeleton ESP", &Settings::Visuals::bones);
		ImGui::Checkbox("Armor ESP", &Settings::Visuals::armor);
		ImGui::Checkbox(("Health ESP"), &Settings::Visuals::bHealth);
		ImGui::Checkbox("Snapline ESP", &Settings::Visuals::bSnaplines);
		if (ImGui::Button("Unload Cheat"))
		{
			exit(0);
		}
		(ImGui::Text(""));
		(ImGui::Text("delicious food By singhhdev and Rolesional <3"));


			if (tab == 3)			// deleted features (not working dont care)
			{
				ImGui::BeginChild(("##tab3left"), ImVec2(189, 455), true, ImGuiWindowFlags_NoScrollbar);
				{
					ImGui::Checkbox("water mark", &Settings::misc::water);
					if (ImGui::Checkbox("no flash", &Settings::Visuals::noflash)) {

						if (Settings::Visuals::noflash) {
							noFlashEnabled = true;

						}
						else {
							noFlashEnabled = false;

						}
					}
					if (ImGui::Checkbox("no hands", &Settings::Visuals::nohands)) {

						if (Settings::Visuals::nohands)
						{
							noHandsEnabled = true;
						}
						else {
							noHandsEnabled = false;
						}
					}

					ImGui::Checkbox("chams", &Settings::Visuals::chams);
					ImGui::Checkbox("no ragdolls", &Settings::Visuals::ragdoll);
					ImGui::Checkbox("night mode", &Settings::Visuals::nightmode);
					ImGui::Checkbox("water mark", &Settings::misc::water);
					ImGui::Checkbox(("radar"), &Settings::misc::radar);

					ImGui::Checkbox("bunny hop", &Settings::misc::bhop);

					ImGui::Checkbox("fov changer", &Settings::misc::fovChanger);

					if (Settings::misc::fovChanger) {
						ImGui::SliderFloat("fov", &Settings::misc::fov, 1, 360);
						isFOV = true;
					}
					else {
						Settings::misc::fov = originalFOV;
						isFOV = false;

					}
				}
		{
		exit(0);
		}
		}
		ImGui::End();
		}
		}
		}



