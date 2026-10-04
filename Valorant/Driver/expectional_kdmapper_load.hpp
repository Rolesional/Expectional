#pragma once

#include <Windows.h>
#include <string>

/**
 * Sistemde yuklu surucunun EnumDeviceDrivers + GetDeviceDriverBaseNameW ile gorunen adi:
 * diskteki .sys dosyasinin taban adi (ornek: L"r69-driver.sys").
 */
extern const wchar_t ExpectionalKmDriverImageBaseName[];

struct ExpectionalDriverMapResult {
	bool success = false;
	bool already_loaded = false;
	std::wstring error;
};

/** Hook self PID ile — launcher uyumluluk. */
ExpectionalDriverMapResult ExpectionalMapKernelDriverIfNeeded();

/** Eskisurum loglari yok — initdriver icinden. */
ExpectionalDriverMapResult ExpectionalMapKernelDriverSilent();

/** Hook kontrolunu atla, kdmapper calistir. */
ExpectionalDriverMapResult ExpectionalMapKernelDriverForce();

/** CS2 PID uzerinden hook dogrula; map gerekirse map et. */
ExpectionalDriverMapResult ExpectionalMapKernelDriverForGame(DWORD game_pid, bool force_remap = false);

bool ExpectionalEnsureKernelDriverMapped(HWND owner = nullptr, DWORD verify_pid = 0);

/** r69 HAL hook responds on self PID — driver usable for kernel reads. */
bool ExpectionalIsR69DriverHookActive();
