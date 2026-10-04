#pragma once

#include <Windows.h>
#include <string>

extern const wchar_t ExpectionalKmDriverImageBaseName[];

struct ExpectionalDriverMapResult {
	bool success = false;
	bool already_loaded = false;
	std::wstring error;
};

ExpectionalDriverMapResult ExpectionalMapKernelDriverIfNeeded();

ExpectionalDriverMapResult ExpectionalMapKernelDriverSilent();

ExpectionalDriverMapResult ExpectionalMapKernelDriverForce();

ExpectionalDriverMapResult ExpectionalMapKernelDriverForGame(DWORD game_pid, bool force_remap = false);

bool ExpectionalEnsureKernelDriverMapped(HWND owner = nullptr, DWORD verify_pid = 0);

bool ExpectionalIsR69DriverHookActive();
