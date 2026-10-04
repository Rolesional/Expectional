#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h>
#include <sddl.h>
#include <utility>

#if defined(EXPECTIONAL_USE_R69)
#include "../Driver/expectional_r69_bridge.hpp"
#else
#include <tlhelp32.h>
#endif

#pragma comment(lib, "Advapi32.lib")

namespace ExpectionalOverlayZOrder {

inline constexpr wchar_t kMappingName[] = L"Local\\ExpectionalOverlayZOrderV1";
inline constexpr wchar_t kOverlayClassName[] = L"ExpectionalExternalBase";
inline constexpr DWORD kMagic = 0x4558505Au;
inline constexpr DWORD kVersion = 4u;

struct Shared {
	DWORD magic = 0;
	DWORD version = 0;
	volatile LONG seq = 0;
	HWND overlay_hwnd = nullptr;
	HWND game_hwnd = nullptr;
	HWND game_root_hwnd = nullptr;
	LONG overlay_x = 0;
	LONG overlay_y = 0;
	LONG overlay_w = 0;
	LONG overlay_h = 0;
	BOOL active = FALSE;
	BOOL menu_open = FALSE;
};

inline DWORD FindCs2ProcessId() noexcept
{
#if defined(EXPECTIONAL_USE_R69)
	return expectional_r69_bridge::find_cs2_process_id_light();
#else
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
	if (snap == INVALID_HANDLE_VALUE)
		return 0;

	DWORD pid = 0;
	PROCESSENTRY32W pe{};
	pe.dwSize = sizeof(pe);
	if (Process32FirstW(snap, &pe)) {
		do {
			if (_wcsicmp(pe.szExeFile, L"cs2.exe") == 0) {
				pid = pe.th32ProcessID;
				break;
			}
		} while (Process32NextW(snap, &pe));
	}
	CloseHandle(snap);
	return pid;
#endif
}

inline HWND GetProcessWnd(DWORD pid) noexcept
{
	if (!pid)
		return nullptr;

	std::pair<HWND, DWORD> params{ nullptr, pid };
	const BOOL bResult = EnumWindows(
	    [](HWND hwnd, LPARAM lParam) -> BOOL {
		    auto* p = reinterpret_cast<std::pair<HWND, DWORD>*>(lParam);
		    DWORD wpid = 0;
		    if (GetWindowThreadProcessId(hwnd, &wpid) && wpid == p->second) {
			    SetLastError(static_cast<DWORD>(-1));
			    p->first = hwnd;
			    return FALSE;
		    }
		    return TRUE;
	    },
	    reinterpret_cast<LPARAM>(&params));

	if (!bResult && GetLastError() == static_cast<DWORD>(-1) && params.first && IsWindow(params.first))
		return params.first;
	return nullptr;
}

inline HWND FindCs2GameHwnd(DWORD pid = 0) noexcept
{
	if (!pid)
		pid = FindCs2ProcessId();
	if (!pid)
		return nullptr;
	return GetProcessWnd(pid);
}

inline HWND FindOverlayHwnd() noexcept
{
	HWND hwnd = FindWindowW(kOverlayClassName, nullptr);
	return (hwnd && IsWindow(hwnd)) ? hwnd : nullptr;
}

inline bool TryGetWindowBand(HWND hwnd, DWORD* out_band) noexcept
{
	if (!out_band || !hwnd || !IsWindow(hwnd))
		return false;

	HMODULE user32 = GetModuleHandleW(L"user32.dll");
	if (!user32)
		return false;

	using GetWindowBandFn = BOOL(WINAPI*)(HWND, PDWORD);
	const auto pGet = reinterpret_cast<GetWindowBandFn>(GetProcAddress(user32, "GetWindowBand"));
	if (!pGet)
		return false;

	return pGet(hwnd, out_band) != FALSE;
}

inline SECURITY_ATTRIBUTES* GetIpcSecurityAttributes() noexcept
{
	static SECURITY_ATTRIBUTES sa{};
	static PSECURITY_DESCRIPTOR sd = nullptr;
	static bool ready = false;
	if (!ready) {
		if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
		        L"D:(A;;GA;;;WD)(A;;GA;;;AC)(A;;GA;;;SY)", SDDL_REVISION_1, &sd, nullptr)) {
			sa.nLength = sizeof(sa);
			sa.lpSecurityDescriptor = sd;
			sa.bInheritHandle = FALSE;
		}
		ready = true;
	}
	return sd ? &sa : nullptr;
}

struct ZOrderCache {
	HWND game_root = nullptr;
	RECT game_rect{};
	bool band_matched = false;
	bool stacked = false;
};

inline HWND ResolveGameRoot(HWND game_hwnd) noexcept
{
	if (!game_hwnd || !IsWindow(game_hwnd))
		return nullptr;
	HWND root = GetAncestor(game_hwnd, GA_ROOT);
	if (!root || !IsWindow(root))
		root = game_hwnd;
	return root;
}

inline bool RectEqual(const RECT& a, const RECT& b) noexcept
{
	return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

inline bool IsGameTargetReady(HWND game_root) noexcept
{
	return game_root && IsWindow(game_root) && !IsIconic(game_root);
}

inline bool TryMatchWindowBand(HWND overlay, HWND game_root, ZOrderCache& cache) noexcept
{
	
	if (!overlay || !game_root || !IsWindow(overlay) || !IsWindow(game_root))
		return false;

	HMODULE user32 = GetModuleHandleW(L"user32.dll");
	if (!user32)
		return false;

	using GetWindowBandFn = BOOL(WINAPI*)(HWND, PDWORD);
	using SetWindowBandFn = BOOL(WINAPI*)(HWND, HWND, DWORD);
	const auto pGet = reinterpret_cast<GetWindowBandFn>(GetProcAddress(user32, "GetWindowBand"));
	const auto pSet = reinterpret_cast<SetWindowBandFn>(GetProcAddress(user32, "SetWindowBand"));
	if (!pGet || !pSet)
		return false;

	DWORD band = 0;
	if (!pGet(game_root, &band))
		return false;

	const BOOL ok = pSet(overlay, game_root, band);
	if (ok)
		cache.band_matched = true;
	return ok != FALSE;
}

inline void SyncOverlayBoundsFromClient(HWND overlay, HWND game_hwnd) noexcept
{
	if (!overlay || !game_hwnd || !IsWindow(overlay) || !IsWindow(game_hwnd))
		return;

	RECT rc{};
	POINT xy{};
	if (!GetClientRect(game_hwnd, &rc))
		return;
	xy.x = rc.left;
	xy.y = rc.top;
	if (!ClientToScreen(game_hwnd, &xy))
		return;

	const int w = rc.right - rc.left;
	const int h = rc.bottom - rc.top;
	if (w <= 0 || h <= 0)
		return;

	(void)SetWindowPos(overlay, nullptr, xy.x, xy.y, w, h, SWP_NOREDRAW | SWP_NOZORDER | SWP_NOACTIVATE);
}

inline void StackOverlayEskisurum(HWND overlay, HWND game_hwnd) noexcept
{
	if (!overlay || !IsWindow(overlay) || !game_hwnd || !IsWindow(game_hwnd))
		return;

	const HWND fg = GetForegroundWindow();
	if (fg != game_hwnd)
		return;

	SyncOverlayBoundsFromClient(overlay, game_hwnd);

	HWND insert = GetWindow(game_hwnd, GW_HWNDPREV);
	(void)SetWindowPos(overlay, insert, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

inline void ApplyOverlayZOrder(HWND overlay, HWND game_hwnd, HWND , bool , ZOrderCache& cache) noexcept
{
	(void)cache;
	if (!overlay || !IsWindow(overlay) || !game_hwnd || !IsWindow(game_hwnd))
		return;

	(void)ShowWindow(overlay, SW_SHOWNOACTIVATE);
	StackOverlayEskisurum(overlay, game_hwnd);

	HWND game_root = ResolveGameRoot(game_hwnd);
	if (game_root && IsWindow(game_root) && !cache.band_matched)
		(void)TryMatchWindowBand(overlay, game_root, cache);
}

inline void ResetZOrderCache(ZOrderCache& cache) noexcept
{
	cache.stacked = false;
	cache.band_matched = false;
}

struct Ipc {
	HANDLE mapping = nullptr;
	Shared* shared = nullptr;
};

inline Ipc OpenIpc(bool create_if_missing) noexcept
{
	Ipc ipc{};
	ipc.mapping = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, kMappingName);
	if (!ipc.mapping && create_if_missing) {
		ipc.mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, GetIpcSecurityAttributes(), PAGE_READWRITE, 0,
		    static_cast<DWORD>(sizeof(Shared)), kMappingName);
	}

	if (!ipc.mapping)
		return ipc;

	ipc.shared = static_cast<Shared*>(MapViewOfFile(ipc.mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(Shared)));
	if (!ipc.shared) {
		CloseHandle(ipc.mapping);
		ipc.mapping = nullptr;
		return ipc;
	}

	if (ipc.shared->magic != kMagic || ipc.shared->version != kVersion) {
		ZeroMemory(ipc.shared, sizeof(Shared));
		ipc.shared->magic = kMagic;
		ipc.shared->version = kVersion;
	}
	return ipc;
}

inline void CloseIpc(Ipc& ipc) noexcept
{
	if (ipc.shared) {
		UnmapViewOfFile(ipc.shared);
		ipc.shared = nullptr;
	}
	if (ipc.mapping) {
		CloseHandle(ipc.mapping);
		ipc.mapping = nullptr;
	}
}

inline void PublishIpc(Ipc& ipc, HWND overlay, HWND game_hwnd, HWND game_root, bool menu_open,
    bool bump_seq = false) noexcept
{
	if (!ipc.shared)
		return;

	RECT r{};
	if (game_root && IsWindow(game_root) && GetWindowRect(game_root, &r)) {
		ipc.shared->overlay_x = r.left;
		ipc.shared->overlay_y = r.top;
		ipc.shared->overlay_w = r.right - r.left;
		ipc.shared->overlay_h = r.bottom - r.top;
	}

	ipc.shared->overlay_hwnd = overlay;
	ipc.shared->game_hwnd = game_hwnd;
	ipc.shared->game_root_hwnd = game_root;
	ipc.shared->menu_open = menu_open ? TRUE : FALSE;
	ipc.shared->active = (overlay && IsWindow(overlay)) ? TRUE : FALSE;
	if (bump_seq)
		InterlockedIncrement(&ipc.shared->seq);
}

} 
