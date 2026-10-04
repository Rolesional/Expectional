#include "expectional_kdmapper_load.hpp"
#include "r69_controller.hpp"
#include "../Driver.h"

#include <vector>
#include <cstdio>

#include <Windows.h>
#include <winternl.h>
#include <Psapi.h>

#include <kdmapper.hpp>
#include <intel_driver.hpp>

#pragma comment(lib, "Psapi.lib")

const wchar_t ExpectionalKmDriverImageBaseName[] = L"r69-driver.sys";

namespace {

static std::wstring FormatNtStatus(NTSTATUS st)
{
	wchar_t buf[32]{};
	std::swprintf(buf, 32, L"0x%08X", static_cast<unsigned>(st));
	return buf;
}

static void ExpectionalLogWideError(const std::wstring& err)
{
	if (err.empty())
		return;
	const int n = WideCharToMultiByte(CP_UTF8, 0, err.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1)
		return;
	std::string narrow(static_cast<size_t>(n - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, err.c_str(), -1, narrow.data(), n, nullptr, nullptr);
	printf("%s\n", narrow.c_str());
	fflush(stdout);
}

static bool ExpectionalKernelDriverListedByBaseName(const wchar_t* baseName)
{
	if (!baseName || !baseName[0])
		return false;

	DWORD cb = 0;
	if (!EnumDeviceDrivers(nullptr, 0, &cb)) {
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
			return false;
	}
	if (cb < sizeof(void*))
		return false;

	std::vector<void*> bases(cb / sizeof(void*));
	if (!EnumDeviceDrivers(bases.data(), cb, &cb))
		return false;

	wchar_t name[MAX_PATH];
	for (void* base : bases) {
		if (!base)
			continue;
		if (GetDeviceDriverBaseNameW(base, name, MAX_PATH) && _wcsicmp(name, baseName) == 0)
			return true;
	}
	return false;
}

static bool ExpectionalProbeR69ForPid(DWORD pid)
{
	if (!pid)
		return false;
	if (!expectional_r69::attach(static_cast<std::uint32_t>(pid)))
		return false;
	expectional_r69::detach();
	return true;
}

static bool ExpectionalProbeR69HookSelf()
{
	return ExpectionalProbeR69ForPid(GetCurrentProcessId());
}

static void WipeImageBuffer(std::vector<uint8_t>& image)
{
	if (!image.empty()) {
		SecureZeroMemory(image.data(), image.size());
		image.clear();
		image.shrink_to_fit();
	}
}

static bool LoadDriverFromEncryptedHeaderStream(std::vector<uint8_t>& out)
{
	using namespace expectional_km_driver_stream;
	out.assign(kPlainSize, 0);
	size_t g = 0;
	for (size_t pi = 0; pi < kPartCount; ++pi) {
		const Part& pt = kParts[pi];
		for (size_t j = 0; j < pt.size; ++j, ++g) {
			if (g >= kPlainSize)
				return false;
			out[g] = static_cast<uint8_t>(pt.data[j] ^ kXorKey[g % sizeof(kXorKey)]);
		}
	}
	if (g != kPlainSize)
		return false;
	if (out.size() < 2 || out[0] != 0x4D || out[1] != 0x5A)
		return false;
	return true;
}

static ExpectionalDriverMapResult ExpectionalMapKernelDriverInternal(bool force_remap, bool quiet)
{
	ExpectionalDriverMapResult result;

#ifndef EXPECTIONAL_USE_R69
	result.success = true;
	return result;
#else
	if (!force_remap && ExpectionalProbeR69HookSelf()) {
		result.success = true;
		result.already_loaded = true;
		if (!quiet) {
			printf("> kernel driver: already active\n");
			fflush(stdout);
		}
		return result;
	}

	if (!quiet) {
		if (ExpectionalKernelDriverListedByBaseName(ExpectionalKmDriverImageBaseName) && !force_remap)
			printf("> kernel driver: stale module detected, remapping...\n");
		else
			printf("> mapping kernel driver...\n");
		fflush(stdout);
	}

	std::vector<uint8_t> image;
	if (!LoadDriverFromEncryptedHeaderStream(image)) {
		result.error =
		    L"Driver image could not be decoded.\n\n"
		    L"The embedded driver stream is invalid or corrupted.";
		return result;
	}

	const NTSTATUS stLoad = intel_driver::Load();
	if (!NT_SUCCESS(stLoad)) {
		WipeImageBuffer(image);
		result.error =
		    L"Failed to load the Intel vulnerable driver (kdmapper stage 1).\n\n"
		    L"Status: " +
		    FormatNtStatus(stLoad) +
		    L"\n\n"
		    L"Disable Vulnerable Driver Blocklist if enabled, then reboot and retry.";
		return result;
	}

	if (!quiet) {
		printf("> kdmapper: mapping r69-driver.sys...\n");
		fflush(stdout);
	}

	NTSTATUS driverExit = 0;
	const ULONG64 mapped = kdmapper::MapDriver(
	    image.data(),
	    0,
	    0,
	    false,
	    true,
	    kdmapper::AllocationMode::AllocatePool,
	    false,
	    nullptr,
	    &driverExit);

	WipeImageBuffer(image);

	if (!mapped) {
		(void)intel_driver::Unload();
		result.error =
		    L"Kernel driver mapping failed (kdmapper stage 2).\n\n"
		    L"The mapper could not allocate or write the driver image.";
		return result;
	}

	(void)intel_driver::Unload();

	if (driverExit != static_cast<NTSTATUS>(0)) {
		wchar_t hexStr[32];
		swprintf(hexStr, 32, L"0x%X", static_cast<unsigned int>(driverExit));

		result.error =
		    L"The mapped driver returned an error from DriverEntry.\n\n"
		    L"Status: " + FormatNtStatus(driverExit) + L" (" + std::wstring(hexStr) + L")";
		return result;
	}

	/** Map sonrasi hook: CR3 dolu olmali (PEB/base zorunlu degil). */
	if (!ExpectionalProbeR69HookSelf()) {
		if (ExpectionalKernelDriverListedByBaseName(ExpectionalKmDriverImageBaseName)) {
			result.error =
			    L"Driver module is loaded but the r69 hook is not responding.\n\n"
			    L"Reboot once, then run Expectional again.";
		} else {
			result.error =
			    L"Driver mapping finished but hook verification failed.\n\n"
			    L"Reboot once and try again.";
		}
		return result;
	}

	if (!quiet) {
		printf("> kernel driver ready\n");
		fflush(stdout);
	}
	result.success = true;
	return result;
#endif
}

} // namespace

bool ExpectionalIsR69DriverHookActive()
{
#ifndef EXPECTIONAL_USE_R69
	return true;
#else
	return ExpectionalProbeR69HookSelf();
#endif
}

ExpectionalDriverMapResult ExpectionalMapKernelDriverIfNeeded()
{
	return ExpectionalMapKernelDriverInternal(false, false);
}

ExpectionalDriverMapResult ExpectionalMapKernelDriverSilent()
{
	return ExpectionalMapKernelDriverInternal(false, true);
}

ExpectionalDriverMapResult ExpectionalMapKernelDriverForce()
{
	return ExpectionalMapKernelDriverInternal(true, false);
}

ExpectionalDriverMapResult ExpectionalMapKernelDriverForGame(DWORD /*game_pid*/, bool force_remap)
{
	return ExpectionalMapKernelDriverInternal(force_remap, false);
}

bool ExpectionalEnsureKernelDriverMapped(HWND owner, DWORD /*verify_pid*/)
{
	ExpectionalDriverMapResult result = ExpectionalMapKernelDriverIfNeeded();
	if (!result.success)
		result = ExpectionalMapKernelDriverForce();
	if (result.success)
		return true;
	ExpectionalLogWideError(result.error);
	const std::wstring err = result.error.empty()
	    ? L"Kernel driver mapping failed."
	    : result.error;
	MessageBoxW(owner, err.c_str(), L"Expectional", MB_OK | MB_ICONERROR);
	return false;
}

