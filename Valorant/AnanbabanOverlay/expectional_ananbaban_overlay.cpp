
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "expectional_ananbaban_overlay.hpp"
#include "expectional_overlay_window.hpp"
#include "../game/globals.hpp"
#include "../Game/expectional_paths.hpp"
#include "../Game/expectional_winio.hpp"
#include "../Game/fonts/weapons_font_data.hpp"
#include "../OSImGui/os_imgui_menu.hpp"

#include <cstdio>
#include <cstring>
#include <vector>
#include <avrt.h>
#include <timeapi.h>
#pragma comment(lib, "avrt.lib")
#pragma comment(lib, "winmm.lib")

extern HWND MyWnd;
extern HWND GameWnd;

ID3D11Device* g_ExpectionalMainDX11Device = nullptr;

namespace {

std::wstring g_ex_imgui_ini_wide;

static std::wstring ResolveImGuiIniPathWide()
{
	const std::wstring primary = ExpectionalPaths::GlobalImGuiIniPathWide();
	const std::wstring bridge = ExpectionalPaths::UwpNotepadImGuiIniPathWide();
	if (!primary.empty() && ExpectionalWinIO::FileExistsWide(primary))
		return primary;
	if (!bridge.empty() && ExpectionalWinIO::FileExistsWide(bridge))
		return bridge;
	if (!primary.empty())
		return primary;
	return bridge;
}

static void EnsureImGuiIniParentDir(const std::wstring& ini_path)
{
	const size_t slash = ini_path.find_last_of(L"\\/");
	if (slash == std::wstring::npos)
		return;
	(void)ExpectionalWinIO::EnsureDirectoryWide(ini_path.substr(0, slash));
}

static void ExpectionalImGuiIniLoad()
{
	g_ex_imgui_ini_wide = ResolveImGuiIniPathWide();
	if (g_ex_imgui_ini_wide.empty())
		return;

	EnsureImGuiIniParentDir(g_ex_imgui_ini_wide);

	std::vector<uint8_t> bytes;
	if (ExpectionalWinIO::ReadAllBytesWide(g_ex_imgui_ini_wide, bytes) && !bytes.empty()) {
		ImGui::LoadIniSettingsFromMemory(reinterpret_cast<const char*>(bytes.data()), bytes.size());
	}

	ImGui::GetIO().IniFilename = nullptr;
}

static void ExpectionalImGuiIniWriteMemory()
{
	if (g_ex_imgui_ini_wide.empty())
		return;

	size_t ini_size = 0;
	const char* ini_data = ImGui::SaveIniSettingsToMemory(&ini_size);
	if (!ini_data || ini_size == 0)
		return;

	EnsureImGuiIniParentDir(g_ex_imgui_ini_wide);
	(void)ExpectionalWinIO::WriteAllBytesWide(g_ex_imgui_ini_wide, ini_data, static_cast<DWORD>(ini_size));
	ImGui::GetIO().WantSaveIniSettings = false;
}

} 

void ExpectionalOverlayImGuiIniPollSave()
{
	if (ImGui::GetIO().WantSaveIniSettings)
		ExpectionalImGuiIniWriteMemory();
}

void ExpectionalOverlayImGuiIniSaveOnShutdown()
{
	ExpectionalImGuiIniWriteMemory();
}

static bool ExpectionalOverlayInitImGuiExtras()
{
	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontDefault();
	{
		ImFontConfig fc{};
		fc.FontDataOwnedByAtlas = false;
		g_WeaponsIconFont = io.Fonts->AddFontFromMemoryTTF(expectional_weapons_font::weapons,
		    sizeof(expectional_weapons_font::weapons), 16.f, &fc, io.Fonts->GetGlyphRangesDefault());
	}
	ExpectionalOsMenu_InitFonts();
	ExpectionalImGuiIniLoad();
	return true;
}

bool ExpectionalAnanbabanOverlayRun(HWND gameHwnd, const std::function<void()>& frameCallback)
{
	if (!gameHwnd || !IsWindow(gameHwnd))
		return false;

	HWND root = GetAncestor(gameHwnd, GA_ROOT);
	(void)root;
	GameWnd = gameHwnd;

	if (!ExpectionalOverlayWindow::Create(gameHwnd)) {
		fprintf(stderr, "> CS2-External-Base overlay: Create failed\n");
		return false;
	}

	g_ExpectionalMainDX11Device = ExpectionalOverlayWindow::m_pDevice;
	MyWnd = ExpectionalOverlayWindow::m_hWnd;

	ExpectionalOverlayInitImGuiExtras();
	SetForegroundWindow(GameWnd);

	timeBeginPeriod(1);
	const HANDLE hRenderThread = GetCurrentThread();
	const int prevPrio = GetThreadPriority(hRenderThread);
	SetThreadPriority(hRenderThread, THREAD_PRIORITY_ABOVE_NORMAL);

	DWORD mmcssTaskIdx = 0;
	HANDLE mmcssHandle = AvSetMmThreadCharacteristicsW(L"Games", &mmcssTaskIdx);
	if (mmcssHandle)
		AvSetMmThreadPriority(mmcssHandle, AVRT_PRIORITY_HIGH);

	while (ExpectionalOverlayWindow::RenderFrame(frameCallback)) {
	}

	if (mmcssHandle)
		AvRevertMmThreadCharacteristics(mmcssHandle);
	SetThreadPriority(hRenderThread, prevPrio);
	timeEndPeriod(1);

	ExpectionalOverlayWindow::Destroy();

	g_ExpectionalMainDX11Device = nullptr;
	MyWnd = nullptr;
	return true;
}
