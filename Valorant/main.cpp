#include <Windows.h>
#include <iostream>
#include <thread>
#include <chrono>
#include <string>
#include <filesystem>
#include <immintrin.h>
#include <timeapi.h>

#pragma comment(lib, "winmm.lib")
#include "driver/driver.hpp"
#if defined(EXPECTIONAL_USE_R69)
#include "Driver/r69_controller.hpp"
#include "Driver/expectional_r69_bridge.hpp"
#endif
#include "overlay/render.hpp"
#include "overlay/menu.hpp"
#include "game/cheat.hpp"
#include "Game/spectator_list.hpp"
#include "Game/bomb_timer.hpp"
#include "Game/cloud_radar.hpp"
#include "Game/window_radar.hpp"
#include "Game/window_radar_map_tex.hpp"
#include "Game/offsets_runtime.hpp"
#include "game/globals.hpp"
#include "Game/expectional_misc_runtime.hpp"
#include "Game/expectional_reveal_workers.hpp"
#include "Game/catalyst_world_bvh.hpp"
#include "Game/votekick_reveal.hpp"
#include "AnanbabanOverlay/expectional_ananbaban_overlay.hpp"

#include <TlHelp32.h>
#include <cstdio>
#include <ctime>

static void FatalErrorExit(const char* message)
{
	fflush(stdout);
	printf("\n%s\n", message);
	fflush(stdout);
	MessageBoxA(nullptr, message, "Expectional", MB_OK | MB_ICONERROR);
}

static void ExpectionalPauseConsoleBeforeExit()
{
	printf("\n> Press Enter to close...\n");
	fflush(stdout);
	(void)getchar();
}

DWORD GetProcessID(const std::wstring processName);

inline bool ExpectionalIsCs2ProcessRunning()
{
	return GetProcessID(L"cs2.exe") != 0;
}

inline bool ExpectionalIsProcessAlive(DWORD pid)
{
	if (!pid)
		return false;
#if defined(EXPECTIONAL_USE_R69)
	return expectional_r69::is_process_alive(static_cast<std::uint32_t>(pid));
#else
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return true;
	PROCESSENTRY32W pe{};
	pe.dwSize = sizeof(pe);
	bool found = false;
	if (Process32FirstW(snap, &pe)) {
		do {
			if (pe.th32ProcessID == pid) {
				found = true;
				break;
			}
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap);
	return found;
#endif
}

inline void ExpectionalExitIfCs2Closed()
{
	static DWORD s_last_check = 0;
	const DWORD now = GetTickCount();
	if (s_last_check && (now - s_last_check) < 750u)
		return;
	s_last_check = now;
	if (processid && !ExpectionalIsProcessAlive(processid)) {
		ExitProcess(0);
		return;
	}
#if !defined(EXPECTIONAL_USE_R69)
	if (!ExpectionalIsCs2ProcessRunning()) {
		ExitProcess(0);
		return;
	}
#endif
	if (GameWnd && !IsWindow(GameWnd))
		ExitProcess(0);
}

DWORD GetProcessID(const std::wstring processName)
{
#if defined(EXPECTIONAL_USE_R69)
	if (_wcsicmp(processName.c_str(), L"cs2.exe") == 0)
		return expectional_r69_bridge::find_cs2_process_id();
	return static_cast<DWORD>(expectional_r69::find_process_id_by_image_name(processName.c_str(), nullptr));
#else
	DWORD result = 0;

	PROCESSENTRY32 processInfo;
	processInfo.dwSize = sizeof(processInfo);

	HANDLE processesSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);
	if (processesSnapshot == INVALID_HANDLE_VALUE)
		goto getpid_leave;

	Process32First(processesSnapshot, &processInfo);
	if (!processName.compare(processInfo.szExeFile)) {
		result = processInfo.th32ProcessID;
		CloseHandle(processesSnapshot);
		goto getpid_leave;
	}

	while (Process32Next(processesSnapshot, &processInfo)) {
		if (!processName.compare(processInfo.szExeFile)) {
			result = processInfo.th32ProcessID;
			CloseHandle(processesSnapshot);
			goto getpid_leave;
		}
	}

	CloseHandle(processesSnapshot);
getpid_leave:
	return result;
#endif
}

static void ExpectionalBhopThreadEntry()
{
	timeBeginPeriod(1);
	constexpr int kTargetHz = 512;
	const auto tickInterval = std::chrono::nanoseconds(1'000'000'000 / kTargetHz);
	auto nextTick = std::chrono::steady_clock::now();

	for (;;) {
		if (GameWnd && IsWindow(GameWnd))
			ExpectionalBhopTick();

		nextTick += tickInterval;
		const auto now = std::chrono::steady_clock::now();
		if (nextTick < now)
			nextTick = now;

		std::this_thread::sleep_until(nextTick - std::chrono::milliseconds(1));
		while (std::chrono::steady_clock::now() < nextTick)
			_mm_pause();
	}
}

static int ExpectionalMainEntry();

int main()
{
	return ExpectionalMainEntry();
}

static int ExpectionalMainEntry()
{
	int exit_code = 0;
	bool skip_exit_pause = false;
	do {
		ExpectionalRandomConsoleTitleW();
		printf(">  expectional starting... \n");

		printf("> waiting for counter strike 2...\n");

		while (Entryhwnd == NULL) {
#if defined(EXPECTIONAL_USE_R69)
			processid = expectional_r69_bridge::find_cs2_process_id_light();
#else
			processid = GetProcessID(L"cs2.exe");
#endif
			Entryhwnd = get_process_wnd(static_cast<uint32_t>(processid));
			Sleep(250);
		}

		if (!ExpectionalLoadOffsetsFromLocalFiles()) {
			printf("> offsets: exe yanindaki offsets klasorune offsets.hpp + client_dll.hpp koy.\n");
			ApplyFallbackOffsets();
		}

		if (!g_GameMem.initdriver(processid)) {
			FatalErrorExit(g_GameMem.last_init_error());
			g_GameMem.shutdown();
			exit_code = 1;
			break;
		}

		client = (uintptr_t)g_GameMem.client_address();
		if (!client) {
			FatalErrorExit(
			    "client.dll is missing after driver init.\n"
			    "Restart Expectional after CS2 reaches the main menu.");
			g_GameMem.shutdown();
			exit_code = 1;
			break;
		}

		ex_world_bvh::EnsureWorldBvhLoadThread();

		GameWnd = Entryhwnd;
		if (!GameWnd || !IsWindow(GameWnd)) {
			FatalErrorExit("Invalid CS2 window handle.");
			g_GameMem.shutdown();
			exit_code = 1;
			break;
		}

		{
			std::thread cg(cacheGame);
			SetThreadPriority(cg.native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
			cg.detach();
		}
		{
			std::thread sp(spectator_list::cache_loop);
			SetThreadPriority(sp.native_handle(), THREAD_PRIORITY_BELOW_NORMAL);
			sp.detach();
		}
		std::thread(ExpectionalBhopThreadEntry).detach();
		cloud_radar_start_thread();
#ifdef EXPECTIONAL_HIDE_CONSOLE
		ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif

		if (!ExpectionalAnanbabanOverlayRun(GameWnd, []() { render(); })) {
			FatalErrorExit(
			    "D3D11 overlay init failed (see console).\n"
			    "Try: update GPU drivers, close other overlays, run windowed/borderless.");
			g_GameMem.shutdown();
			exit_code = 1;
			break;
		}
		exit_code = 0;
	} while (0);

	if (exit_code != 0 && !skip_exit_pause)
		ExpectionalPauseConsoleBeforeExit();
	return exit_code;
}

auto render() -> void
{
	ExpectionalExitIfCs2Closed();
	ex_misc::BeginRenderFrame();
	ExpectionalRevealWorkersNotifyOverlayFrame();
	ExpectionalSyncScreenCenterFromImGui();

	const bool want_grenade_overlay = Settings::Visuals::grenadeLineups;
	const bool want_world_nades = ex_sched::RunWorldGrenadeOverlayThisFrame();
	const bool want_world_pickup = Settings::Visuals::bombWorldEsp || Settings::Visuals::droppedWeaponEsp;
	const bool want_esp = ExpectionalShouldRunEspLoop();
	UE4Structs::view_matrix_t frame_vm{};
	const UE4Structs::view_matrix_t* frame_vm_ptr = nullptr;
	if (client && offsets::dwViewMatrix && (want_grenade_overlay || want_world_nades || want_world_pickup || want_esp)) {
		frame_vm = g_GameMem.readv<UE4Structs::view_matrix_t>(client + offsets::dwViewMatrix);
		frame_vm_ptr = &frame_vm;
	}
	if (want_esp)
		espLoop(frame_vm_ptr);
	if (want_world_pickup)
		ExpectionalDrawWorldPickupOverlayFrame(frame_vm_ptr);
	if (want_world_nades)
		ExpectionalDrawWorldGrenadeOverlayFrame(frame_vm_ptr);
	if (want_grenade_overlay)
		ExpectionalGrenadeOverlayFrame(frame_vm_ptr);
	drawmenu();
	ExpectionalSyncOverlayForMenu(Settings::bMenu);
	ExpectionalClearImGuiInputForGameplay();

	if (Settings::misc::bombTimer)
		bomb_timer::draw_window();
	if (Settings::misc::spectatorList)
		spectator_list::draw_window();
	if (Settings::misc::water)
		ex_hit_feedback::DrawWatermarkWindow();
	if (Settings::misc::keybind_list_window)
		ExpectionalKeybindListWindow();
	if (Settings::misc::radarWindow)
		window_radar_render_frame();
	ExpectionalFaceitWorkerNotifyFrame();
	if (Settings::misc::rank_reveal_window)
		ExpectionalDrawRankRevealWindow();
	if (Settings::misc::votekick_reveal_window)
		ExpectionalDrawVoteKickRevealWindow();

	if (WindowRadarMapTex_IsParsingMap()) {
		ImGui::GetBackgroundDrawList()->AddText(ImGui::GetFont(), 24.f, ImVec2(10.f, 10.f), IM_COL32(255, 255, 0, 255), "Parsing the map...", nullptr, 0.f, nullptr);
	}
}
