#include "expectional_r69_bridge.hpp"

#include "expectional_kdmapper_load.hpp"

#include "r69_controller.hpp"

#include <cstdio>

namespace {

static void EnableSeDebugPrivilegeOnce()
{
	static bool done = false;
	if (done)
		return;
	done = true;

	HANDLE hToken = nullptr;
	if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
		return;

	LUID luid{};
	if (LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid)) {
		TOKEN_PRIVILEGES tp{};
		tp.PrivilegeCount = 1;
		tp.Privileges[0].Luid = luid;
		tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
		AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), nullptr, nullptr);
	}
	CloseHandle(hToken);
}

static bool g_km_mapped = false;

static bool EnsureKernelDriverMapped(std::string* out_error)
{
	if (g_km_mapped && ExpectionalIsR69DriverHookActive())
		return true;

	static bool logged = false;
	const auto log_once = [&](const char* msg) {
		if (logged)
			return;
		logged = true;
		printf("%s", msg);
		fflush(stdout);
	};

	if (ExpectionalIsR69DriverHookActive()) {
		g_km_mapped = true;
		log_once("[KM] driver: OK\n");
		return true;
	}

	log_once("[KM] driver: NO\n");

	EnableSeDebugPrivilegeOnce();
	const ExpectionalDriverMapResult driverMap = ExpectionalMapKernelDriverSilent();
	if (!driverMap.success) {
		if (out_error) {
			*out_error = std::string(driverMap.error.begin(), driverMap.error.end());
		}
		return false;
	}

	if (ExpectionalIsR69DriverHookActive()) {
		g_km_mapped = true;
		log_once("[KM] driver: OK\n");
		return true;
	}

	return false;
}

} 

namespace expectional_r69_bridge {

bool init_for_process(DWORD pid, std::string* out_error)
{
	if (!pid)
		return false;

	if (!EnsureKernelDriverMapped(out_error))
		return false;

	expectional_r69::detach();
	return expectional_r69::attach(static_cast<std::uint32_t>(pid));
}

bool device_read(uintptr_t src, void* dst, size_t sz)
{
	if (!dst || sz == 0 || sz > 0x10000u)
		return false;
	if (!expectional_r69::ready())
		return false;

	const NTSTATUS st = expectional_r69::read_memory(
	    static_cast<std::uint64_t>(src), dst, static_cast<std::uint64_t>(sz));
	return NT_SUCCESS(st);
}

void shutdown_r69()
{
	expectional_r69::detach();
}

DWORD find_cs2_process_id_light()
{
	return static_cast<DWORD>(expectional_r69::find_process_id_by_image_name_light(L"cs2.exe"));
}

DWORD find_cs2_process_id()
{
	return find_cs2_process_id_light();
}

} 
