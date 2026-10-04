#include "driver.hpp"
#include "../../drivertesti.hpp"
#include <TlHelp32.h>
#include <vector>

#if defined(EXPECTIONAL_USE_R69)
#include "expectional_r69_bridge.hpp"
#include "r69_controller.hpp"
#include "../Game/offsets_runtime.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <unordered_map>
#endif

#ifndef TH32CS_SNAPMODULE64
#define TH32CS_SNAPMODULE64 0x00000040
#endif

bool km_device_read(uintptr_t src, void* dst, size_t sz) {
	if (!dst || sz == 0 || sz > 0x10000u)
		return false;
#if defined(EXPECTIONAL_USE_R69)
	return expectional_r69_bridge::device_read(src, dst, sz);
#else
	if (kernel_ue5::driver_handle == INVALID_HANDLE_VALUE || kernel_ue5::driver_handle == nullptr)
		return false;
	kernel_ue5::read_physical(src, dst, static_cast<DWORD>(sz));
	return true;
#endif
}

#ifndef EXPECTIONAL_USE_R69
static uintptr_t module_base_from_snapshot(DWORD pid, const wchar_t* module_name) {
	if (!pid || !module_name || !module_name[0])
		return 0;

	const DWORD flags = TH32CS_SNAPMODULE | TH32CS_SNAPMODULE64;
	HANDLE snap = CreateToolhelp32Snapshot(flags, pid);
	if (snap == INVALID_HANDLE_VALUE)
		return 0;

	MODULEENTRY32W me{};
	me.dwSize = sizeof(me);
	uintptr_t base = 0;
	if (Module32FirstW(snap, &me)) {
		do {
			if (!_wcsicmp(me.szModule, module_name)) {
				base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
				break;
			}
		} while (Module32NextW(snap, &me));
	}
	CloseHandle(snap);
	return base;
}
#endif

#if defined(EXPECTIONAL_USE_R69)
static const char* KernelModuleResolveErrorText(expectional_r69::module_resolve_error err) {
	switch (err) {
	case expectional_r69::module_resolve_error::none:
		return "OK";
	case expectional_r69::module_resolve_error::not_ready:
		return "kernel driver not ready";
	case expectional_r69::module_resolve_error::no_peb:
		return "process PEB unavailable";
	case expectional_r69::module_resolve_error::peb_unreadable:
		return "process PEB unreadable via kernel";
	case expectional_r69::module_resolve_error::no_ldr:
		return "process LDR list missing";
	case expectional_r69::module_resolve_error::ldr_unreadable:
		return "process LDR unreadable via kernel";
	case expectional_r69::module_resolve_error::list_unreadable:
		return "module list unreadable via kernel";
	case expectional_r69::module_resolve_error::not_found:
		return "module not found in process LDR";
	default:
		return "unknown kernel module lookup error";
	}
}

static bool ResolveModuleBaseKernelOnly(
    const wchar_t* module_name,
    uintptr_t& out_base,
    std::string& out_error,
    std::uint32_t* out_ldr_size = nullptr) {
	out_base = 0;
	out_error.clear();
	if (out_ldr_size)
		*out_ldr_size = 0;
	if (!module_name || !module_name[0]) {
		out_error = "Invalid module name.";
		return false;
	}

	const expectional_r69::module_resolve_result resolved =
	    expectional_r69::resolve_module_base(module_name);
	if (resolved.base) {
		out_base = static_cast<uintptr_t>(resolved.base);
		if (out_ldr_size)
			*out_ldr_size = resolved.size_of_image;
		return true;
	}

	char module_ascii[64]{};
	const int name_len = WideCharToMultiByte(
	    CP_UTF8, 0, module_name, -1, module_ascii, static_cast<int>(sizeof(module_ascii)), nullptr, nullptr);
	const char* name_for_log = (name_len > 0) ? module_ascii : "?";

	char detail[256]{};
	if (resolved.error == expectional_r69::module_resolve_error::not_found && resolved.modules_scanned > 0) {
		std::snprintf(
		    detail,
		    sizeof(detail),
		    "Kernel module lookup failed: %s not found in process LDR (%d modules scanned).",
		    name_for_log,
		    resolved.modules_scanned);
	} else {
		std::snprintf(
		    detail,
		    sizeof(detail),
		    "Kernel module lookup failed: %s (%s).",
		    name_for_log,
		    KernelModuleResolveErrorText(resolved.error));
	}
	out_error = detail;
	return false;
}

static std::unordered_map<std::wstring, uintptr_t>& KernelModuleBaseCache() {
	static std::unordered_map<std::wstring, uintptr_t> cache;
	return cache;
}

static expectional_r69::engine_probe_params MakeEngineProbeParams(uintptr_t skip_client_base) {
	expectional_r69::engine_probe_params params{};
	params.network_game_client_rva = static_cast<std::uint64_t>(offsets::dwNetworkGameClient);
	params.build_number_rva = static_cast<std::uint64_t>(offsets::dwBuildNumber);
	params.window_width_rva = static_cast<std::uint64_t>(offsets::dwWindowWidth);
	params.window_height_rva = static_cast<std::uint64_t>(offsets::dwWindowHeight);
	params.skip_module_base = static_cast<std::uint64_t>(skip_client_base);
	return params;
}

static expectional_r69::client_probe_params MakeClientProbeParams(uintptr_t skip_engine_base) {
	expectional_r69::client_probe_params params{};
	params.entity_list_rva = static_cast<std::uint64_t>(offsets::dwEntityList);
	params.view_matrix_rva = static_cast<std::uint64_t>(offsets::dwViewMatrix);
	params.local_player_pawn_rva = static_cast<std::uint64_t>(offsets::dwLocalPlayerPawn);
	params.highest_entity_index_off = static_cast<std::uint64_t>(offsets::dwGameEntitySystem_highestEntityIndex);
	params.skip_module_base = static_cast<std::uint64_t>(skip_engine_base);
	return params;
}

static std::uint64_t ProbeEngineModuleBase(uintptr_t skip_client_base) {
	return expectional_r69::find_engine_module_by_offset(MakeEngineProbeParams(skip_client_base));
}

static std::uint64_t ProbeClientModuleBase(uintptr_t skip_engine_base) {
	return expectional_r69::find_client_module_by_offset(MakeClientProbeParams(skip_engine_base));
}

static bool ValidateClientOnly(uintptr_t client_base, uintptr_t skip_engine_base, int* out_highest_entity) {
	if (!client_base)
		return false;
	return expectional_r69::validate_client_module(
	    static_cast<std::uint64_t>(client_base),
	    MakeClientProbeParams(skip_engine_base),
	    out_highest_entity);
}

static bool ValidateEngineOnly(uintptr_t engine_base, uintptr_t skip_client_base) {
	if (!engine_base)
		return false;
	return expectional_r69::validate_engine_module(
	    static_cast<std::uint64_t>(engine_base),
	    MakeEngineProbeParams(skip_client_base));
}

static bool ValidateCachedModules(uintptr_t client_base, uintptr_t engine_base, int* out_highest_entity) {
	if (!client_base || !engine_base)
		return false;
	if (client_base == engine_base)
		return false;
	if (!ValidateClientOnly(client_base, engine_base, out_highest_entity))
		return false;
	if (!ValidateEngineOnly(engine_base, client_base))
		return false;
	return true;
}

static std::uint32_t KernelReadClientSizeOfImage(uintptr_t client_base);
static bool KernelClientImageUsable(uintptr_t base, std::uint32_t ldr_size = 0);
static bool KernelEngineImageUsable(uintptr_t base, std::uint32_t ldr_size = 0);
static uintptr_t KernelSnapshotModuleBase(DWORD pid, const wchar_t* module_name);

/** LDR + offset probe; client/engine birlikte dogrulanir. */
static bool ResolveClientEngineModules(
    uintptr_t& client_out,
    uintptr_t& engine_out,
    std::string& client_resolve_error,
    std::string& engine_resolve_error,
    int* out_highest_entity) {
	client_out = 0;
	engine_out = 0;
	if (out_highest_entity)
		*out_highest_entity = -1;

	uintptr_t named_client = 0;
	uintptr_t named_engine = 0;
	std::uint32_t client_ldr_size = 0;
	std::uint32_t engine_ldr_size = 0;

	ResolveModuleBaseKernelOnly(L"client.dll", client_out, client_resolve_error, &client_ldr_size);
	named_client = client_out;
	if (client_out) {
		int dummy = 0;
		if (!ValidateClientOnly(client_out, 0, &dummy))
			client_out = 0;
	}

	ResolveModuleBaseKernelOnly(L"engine2.dll", engine_out, engine_resolve_error, &engine_ldr_size);
	named_engine = engine_out;
	if (engine_out && !ValidateEngineOnly(engine_out, client_out))
		engine_out = 0;

	if (!client_out) {
		const std::uint64_t probed = ProbeClientModuleBase(engine_out ? engine_out : named_engine);
		if (probed)
			client_out = static_cast<uintptr_t>(probed);
	}
	if (!engine_out) {
		const std::uint64_t probed = ProbeEngineModuleBase(client_out ? client_out : named_client);
		if (probed)
			engine_out = static_cast<uintptr_t>(probed);
	}

	/** Baslik sayfasi kapali olsa da LDR boyutu / isim eslesmesi yeterli. */
	if (!KernelClientImageUsable(client_out, client_ldr_size) &&
	    KernelClientImageUsable(named_client, client_ldr_size))
		client_out = named_client;
	if (!KernelEngineImageUsable(engine_out, engine_ldr_size) &&
	    KernelEngineImageUsable(named_engine, engine_ldr_size))
		engine_out = named_engine;

	const DWORD snap_pid = static_cast<DWORD>(expectional_r69::attached_process().process_id);
	if (!KernelClientImageUsable(client_out, client_ldr_size)) {
		const uintptr_t snap = KernelSnapshotModuleBase(snap_pid, L"client.dll");
		if (KernelClientImageUsable(snap, 0))
			client_out = snap;
	}
	if (!KernelEngineImageUsable(engine_out, engine_ldr_size)) {
		const uintptr_t snap = KernelSnapshotModuleBase(snap_pid, L"engine2.dll");
		if (KernelEngineImageUsable(snap, 0))
			engine_out = snap;
	}

	if (client_out && engine_out && client_out != engine_out) {
		if (ValidateCachedModules(client_out, engine_out, out_highest_entity))
			return true;
		if (KernelClientImageUsable(client_out, client_ldr_size) &&
		    KernelEngineImageUsable(engine_out, engine_ldr_size)) {
			printf(
			    "[KM] module probe soft-accept client=0x%llX engine=0x%llX ldr=0x%X/0x%X\n",
			    static_cast<unsigned long long>(client_out),
			    static_cast<unsigned long long>(engine_out),
			    client_ldr_size,
			    engine_ldr_size);
			fflush(stdout);
			return true;
		}
	}

	char diag[480]{};
	std::snprintf(
	    diag,
	    sizeof(diag),
	    "client=0x%llX ldr=0x%X pe=0x%X | engine=0x%llX ldr=0x%X pe=0x%X | %s",
	    static_cast<unsigned long long>(named_client ? named_client : client_out),
	    client_ldr_size,
	    KernelReadClientSizeOfImage(named_client ? named_client : client_out),
	    static_cast<unsigned long long>(named_engine ? named_engine : engine_out),
	    engine_ldr_size,
	    KernelReadClientSizeOfImage(named_engine ? named_engine : engine_out),
	    client_resolve_error.empty() ? "LDR/PE/probe empty" : client_resolve_error.c_str());
	client_resolve_error = diag;
	return false;
}

static bool KernelReadVa(uintptr_t src, void* dst, size_t sz)
{
	return km_device_read(src, dst, sz);
}

static bool KernelLooksLikeUserVa(std::uint64_t p)
{
	return p >= 0x10000ULL && p <= 0x00007FFFFFFEFFFFULL;
}

static std::uint32_t KernelReadClientSizeOfImage(uintptr_t client_base)
{
	std::uint16_t mz = 0;
	if (!KernelReadVa(client_base, &mz, sizeof(mz)) || mz != 0x5A4D)
		return 0;
	std::int32_t e_lfanew = 0;
	if (!KernelReadVa(client_base + 0x3C, &e_lfanew, sizeof(e_lfanew)) || e_lfanew < 0x40 || e_lfanew > 0x1000)
		return 0;
	std::uint32_t pe = 0;
	if (!KernelReadVa(client_base + static_cast<uintptr_t>(e_lfanew), &pe, sizeof(pe)) || pe != 0x00004550u)
		return 0;
	std::uint32_t size = 0;
	if (!KernelReadVa(client_base + static_cast<uintptr_t>(e_lfanew) + 0x50, &size, sizeof(size)))
		return 0;
	if (size < 0x1000u || size > 0x10000000u)
		return 0;
	return size;
}

static bool KernelVaReadable(uintptr_t addr)
{
	std::uint8_t byte = 0;
	return addr && KernelReadVa(addr, &byte, sizeof(byte));
}

static bool KernelClientImageUsable(uintptr_t base, std::uint32_t ldr_size)
{
	if (!base)
		return false;
	const std::uint32_t pe = KernelReadClientSizeOfImage(base);
	const std::uint64_t sz = pe ? static_cast<std::uint64_t>(pe) : static_cast<std::uint64_t>(ldr_size);
	const auto view_rva = static_cast<std::uint64_t>(offsets::dwViewMatrix);
	const auto list_rva = static_cast<std::uint64_t>(offsets::dwEntityList);
	const std::uint64_t need = (view_rva > list_rva ? view_rva : list_rva) + 0x1000ull;
	if (sz > need)
		return true;
	if (list_rva && KernelVaReadable(base + static_cast<uintptr_t>(list_rva)))
		return true;
	if (view_rva && KernelVaReadable(base + static_cast<uintptr_t>(view_rva)))
		return true;
	return false;
}

static bool KernelEngineImageUsable(uintptr_t base, std::uint32_t ldr_size)
{
	if (!base || base < 0x10000ull)
		return false;
	if (KernelReadClientSizeOfImage(base))
		return true;
	if (ldr_size >= 0x100000u)
		return true;
	const auto build_rva = static_cast<uintptr_t>(offsets::dwBuildNumber);
	if (build_rva && KernelVaReadable(base + build_rva))
		return true;
	const auto ngc_rva = static_cast<uintptr_t>(offsets::dwNetworkGameClient);
	if (ngc_rva && KernelVaReadable(base + ngc_rva))
		return true;
	return base <= 0x00007FFFFFFEFFFFULL;
}

static uintptr_t KernelSnapshotModuleBase(DWORD pid, const wchar_t* module_name)
{
	if (!pid || !module_name || !module_name[0])
		return 0;

	const DWORD flags = TH32CS_SNAPMODULE | TH32CS_SNAPMODULE64;
	HANDLE snap = CreateToolhelp32Snapshot(flags, pid);
	if (snap == INVALID_HANDLE_VALUE)
		return 0;

	MODULEENTRY32W me{};
	me.dwSize = sizeof(me);
	uintptr_t best = 0;
	DWORD best_size = 0;
	if (Module32FirstW(snap, &me)) {
		do {
			if (_wcsicmp(me.szModule, module_name) != 0)
				continue;
			if (me.modBaseSize >= best_size) {
				best_size = me.modBaseSize;
				best = reinterpret_cast<uintptr_t>(me.modBaseAddr);
			}
		} while (Module32NextW(snap, &me));
	}
	CloseHandle(snap);
	return best;
}

static bool KernelProbeGameEntitySystem(uintptr_t ges, std::int32_t* out_highest, std::ptrdiff_t* out_hi_off)
{
	if (!KernelLooksLikeUserVa(ges))
		return false;

	std::uint64_t vtable = 0;
	if (!KernelReadVa(ges, &vtable, sizeof(vtable)) || !KernelLooksLikeUserVa(vtable))
		return false;

	std::uint64_t chunk = 0;
	(void)KernelReadVa(ges + 0x10, &chunk, sizeof(chunk));
	const bool chunk_ok = KernelLooksLikeUserVa(chunk);

	static const std::uint64_t kHighestOffs[] = { 0x2090, 0x20F0, 0x20E8, 0x20A0, 0x2100, 0x1510, 0x1518 };
	for (std::uint64_t hi_off : kHighestOffs) {
		std::int32_t hi = -1;
		if (!KernelReadVa(ges + static_cast<uintptr_t>(hi_off), &hi, sizeof(hi)))
			continue;
		if (hi < 0 || hi > 16384)
			continue;
		if (!chunk_ok && hi <= 0)
			continue;
		if (out_highest)
			*out_highest = hi;
		if (out_hi_off)
			*out_hi_off = static_cast<std::ptrdiff_t>(hi_off);
		return true;
	}
	return false;
}

static bool KernelEntityListAtRva(
    uintptr_t client_base,
    std::ptrdiff_t rva,
    std::uint64_t* out_ges,
    std::int32_t* out_highest,
    std::ptrdiff_t* out_hi_off)
{
	if (!client_base || rva < 0x1000)
		return false;

	std::uint64_t ptr = 0;
	if (!KernelReadVa(client_base + static_cast<uintptr_t>(rva), &ptr, sizeof(ptr)))
		return false;
	if (!KernelProbeGameEntitySystem(static_cast<uintptr_t>(ptr), out_highest, out_hi_off))
		return false;
	if (out_ges)
		*out_ges = ptr;
	return true;
}

/** Website RVA 0 ise kernel ile CGameEntitySystem pointer'ini bul, offsets::dwEntityList'i duzelt. */
static bool KernelFixupEntityList(uintptr_t client_base)
{
	if (!client_base)
		return false;

	const std::uint32_t image_size = KernelReadClientSizeOfImage(client_base);
	auto rva_ok = [image_size](std::ptrdiff_t rva) -> bool {
		if (rva < 0x1000)
			return false;
		if (image_size && static_cast<std::uint64_t>(rva) + 8u > image_size)
			return false;
		return true;
	};

	std::uint64_t ges = 0;
	std::int32_t hi = -1;
	std::ptrdiff_t hi_off = 0;
	std::ptrdiff_t found_rva = 0;

	const std::ptrdiff_t published = offsets::dwEntityList;
	if (rva_ok(published) &&
	    KernelEntityListAtRva(client_base, published, &ges, &hi, &hi_off)) {
		found_rva = published;
	}

	if (!found_rva && rva_ok(published)) {
		static uintptr_t s_window_scanned_client = 0;
		if (s_window_scanned_client != client_base) {
			s_window_scanned_client = client_base;
		constexpr std::ptrdiff_t kWindow = 0x4000;
		const std::ptrdiff_t start =
		    (published > kWindow) ? (published - kWindow) : static_cast<std::ptrdiff_t>(0x1000);
		const std::ptrdiff_t end = published + kWindow;
		std::uint8_t page[0x1000]{};
		for (std::ptrdiff_t page_rva = start; page_rva < end; page_rva += 0x1000) {
			if (!rva_ok(page_rva))
				continue;
			if (!KernelReadVa(client_base + static_cast<uintptr_t>(page_rva), page, sizeof(page)))
				continue;
			for (std::size_t i = 0; i + 8 <= sizeof(page); i += 8) {
				std::uint64_t ptr = 0;
				std::memcpy(&ptr, page + i, sizeof(ptr));
				std::int32_t cand_hi = -1;
				std::ptrdiff_t cand_off = 0;
				if (!KernelProbeGameEntitySystem(static_cast<uintptr_t>(ptr), &cand_hi, &cand_off))
					continue;
				found_rva = page_rva + static_cast<std::ptrdiff_t>(i);
				ges = ptr;
				hi = cand_hi;
				hi_off = cand_off;
				break;
			}
			if (found_rva)
				break;
		}
		}
	}

	if (!found_rva) {
		static const std::ptrdiff_t kAltRva[] = {
		    0x2571220, 0x2554050, 0x254EE60, 0x254FE70, 0x24A1760
		};
		for (std::ptrdiff_t alt : kAltRva) {
			if (alt == published || !rva_ok(alt))
				continue;
			if (!KernelEntityListAtRva(client_base, alt, &ges, &hi, &hi_off))
				continue;
			found_rva = alt;
			break;
		}
	}

	if (!found_rva)
		return false;

	if (found_rva != offsets::dwEntityList) {
		printf(
		    "[KM] entity list RVA %llX -> %llX (kernel)\n",
		    static_cast<unsigned long long>(offsets::dwEntityList),
		    static_cast<unsigned long long>(found_rva));
		fflush(stdout);
		offsets::dwEntityList = found_rva;
	}
	if (hi_off && hi_off != offsets::dwGameEntitySystem_highestEntityIndex) {
		printf(
		    "[KM] highestEntityIndex off 0x%llX -> 0x%llX (kernel)\n",
		    static_cast<unsigned long long>(offsets::dwGameEntitySystem_highestEntityIndex),
		    static_cast<unsigned long long>(hi_off));
		fflush(stdout);
		offsets::dwGameEntitySystem_highestEntityIndex = hi_off;
	}
	(void)ges;
	(void)hi;
	return true;
}

static bool KernelFixupLocalPawn(uintptr_t client_base)
{
	if (!client_base || !offsets::dwLocalPlayerPawn)
		return false;

	std::uint64_t pawn = 0;
	if (KernelReadVa(
	        client_base + static_cast<uintptr_t>(offsets::dwLocalPlayerPawn),
	        &pawn,
	        sizeof(pawn)) &&
	    KernelLooksLikeUserVa(pawn)) {
		std::uint64_t vt = 0;
		if (KernelReadVa(static_cast<uintptr_t>(pawn), &vt, sizeof(vt)) && KernelLooksLikeUserVa(vt))
			return true;
	}

	if (!offsets::dwLocalPlayerController || !offsets::dwPlayerPawn || !offsets::dwEntityList)
		return false;

	std::uint64_t controller = 0;
	if (!KernelReadVa(
	        client_base + static_cast<uintptr_t>(offsets::dwLocalPlayerController),
	        &controller,
	        sizeof(controller)) ||
	    !KernelLooksLikeUserVa(controller))
		return false;

	std::uint32_t handle = 0;
	if (!KernelReadVa(
	        static_cast<uintptr_t>(controller) + static_cast<uintptr_t>(offsets::dwPlayerPawn),
	        &handle,
	        sizeof(handle)) ||
	    !handle ||
	    handle == 0xFFFFFFFFu)
		return false;

	std::uint64_t entity_list = 0;
	if (!KernelReadVa(
	        client_base + static_cast<uintptr_t>(offsets::dwEntityList),
	        &entity_list,
	        sizeof(entity_list)) ||
	    !KernelLooksLikeUserVa(entity_list))
		return false;

	const unsigned idx = handle & 0x7FFFu;
	if (!idx || idx == 0x7FFFu)
		return false;
	const uintptr_t stride = static_cast<uintptr_t>(
	    offsets::entity_controller_stride ? offsets::entity_controller_stride : 112u);
	std::uint64_t list_entry = 0;
	if (!KernelReadVa(
	        static_cast<uintptr_t>(entity_list) + 8ull * (static_cast<std::uint64_t>(idx) >> 9) + 16,
	        &list_entry,
	        sizeof(list_entry)) ||
	    !KernelLooksLikeUserVa(list_entry))
		return false;

	std::uint64_t resolved = 0;
	if (!KernelReadVa(
	        static_cast<uintptr_t>(list_entry) + stride * (handle & 0x1FFu),
	        &resolved,
	        sizeof(resolved)) ||
	    !KernelLooksLikeUserVa(resolved))
		return false;

	return true;
}

static int CountValidGameEntitiesKernel(uintptr_t client_base)
{
	if (!client_base || !offsets::dwEntityList || !offsets::dwPlayerPawn)
		return 0;

	std::uint64_t entity_list = 0;
	if (!KernelReadVa(client_base + static_cast<uintptr_t>(offsets::dwEntityList), &entity_list, sizeof(entity_list)))
		return 0;
	if (entity_list < 0x10000ULL || entity_list > 0x00007FFFFFFEFFFFULL)
		return 0;

	const uintptr_t kEntStride = static_cast<uintptr_t>(
	    offsets::entity_controller_stride ? offsets::entity_controller_stride : 112u);
	int count = 0;
	for (int i = 1; i < 64; ++i) {
		std::uint64_t list_entry = 0;
		if (!KernelReadVa(
		        static_cast<uintptr_t>(entity_list + 8ull * (static_cast<uintptr_t>(i & 0x7FFF) >> 9) + 16),
		        &list_entry,
		        sizeof(list_entry)) ||
		    !list_entry)
			continue;

		std::uint64_t player = 0;
		if (!KernelReadVa(static_cast<uintptr_t>(list_entry + kEntStride * (i & 0x1FF)), &player, sizeof(player)) ||
		    !player)
			continue;

		std::uint32_t playerpawn = 0;
		if (!KernelReadVa(static_cast<uintptr_t>(player + offsets::dwPlayerPawn), &playerpawn, sizeof(playerpawn)) ||
		    !playerpawn)
			continue;

		std::uint64_t list_entry2 = 0;
		if (!KernelReadVa(
		        static_cast<uintptr_t>(entity_list + 0x8 * ((playerpawn & 0x7FFF) >> 9) + 16),
		        &list_entry2,
		        sizeof(list_entry2)) ||
		    !list_entry2)
			continue;

		std::uint64_t pawn = 0;
		if (!KernelReadVa(
		        static_cast<uintptr_t>(list_entry2 + kEntStride * (playerpawn & 0x1FF)),
		        &pawn,
		        sizeof(pawn)) ||
		    pawn < 0x10000ULL)
			continue;

		++count;
		if (count >= 1)
			break;
	}
	return count;
}

struct MenuReadyInfo {
	int highest = -1;
	int pawns = 0;
	bool entity_list_ok = false;
	bool local_pawn_ok = false;
	bool view_ok = false;
};

/** Main menu hazir: entity sistemi ayakta VEYA kamera/pawn var.
 *  Oyuncu pawn zinciri main menu'de sik sik 0 olur — ona baglamak takilma sebebiydi. */
static bool ProbeMainMenuReady(uintptr_t client_base, MenuReadyInfo* info)
{
	MenuReadyInfo local{};
	if (!client_base)
		return false;

	if (offsets::dwEntityList) {
		std::uint64_t entity_list = 0;
		if (KernelReadVa(
		        client_base + static_cast<uintptr_t>(offsets::dwEntityList),
		        &entity_list,
		        sizeof(entity_list)) &&
		    entity_list >= 0x10000ULL &&
		    entity_list <= 0x00007FFFFFFEFFFFULL) {
			local.entity_list_ok = true;
			if (offsets::dwGameEntitySystem_highestEntityIndex) {
				std::int32_t hi = -1;
				if (KernelReadVa(
				        static_cast<uintptr_t>(entity_list) +
				            static_cast<uintptr_t>(offsets::dwGameEntitySystem_highestEntityIndex),
				        &hi,
				        sizeof(hi)) &&
				    hi >= 0 &&
				    hi <= 16384)
					local.highest = hi;
			}
		}
	}

	if (offsets::dwLocalPlayerPawn) {
		std::uint64_t pawn = 0;
		if (KernelReadVa(
		        client_base + static_cast<uintptr_t>(offsets::dwLocalPlayerPawn),
		        &pawn,
		        sizeof(pawn)) &&
		    pawn >= 0x10000ULL &&
		    pawn <= 0x00007FFFFFFEFFFFULL)
			local.local_pawn_ok = true;
	}

	if (offsets::dwViewMatrix) {
		float m[16]{};
		if (KernelReadVa(client_base + static_cast<uintptr_t>(offsets::dwViewMatrix), m, sizeof(m))) {
			int nz = 0;
			bool finite = true;
			for (int i = 0; i < 16; ++i) {
				if (!std::isfinite(m[i])) {
					finite = false;
					break;
				}
				if (m[i] != 0.f)
					++nz;
			}
			if (finite && nz >= 4 && (std::fabs(m[0]) > 0.0001f || std::fabs(m[5]) > 0.0001f))
				local.view_ok = true;
		}
	}

	if (local.entity_list_ok)
		local.pawns = CountValidGameEntitiesKernel(client_base);

	if (info)
		*info = local;

	if (local.entity_list_ok)
		return true;
	if (local.highest > 0)
		return true;
	if (local.local_pawn_ok)
		return true;
	if (local.pawns >= 1)
		return true;
	if (local.view_ok)
		return true;
	return false;
}

static bool ReattachCs2Process(DWORD pid)
{
	expectional_r69::detach();
	return expectional_r69_bridge::init_for_process(pid);
}

static void RefreshAttachedProcessMeta()
{
	const auto& pd = expectional_r69::attached_process();
	virtualaddy = reinterpret_cast<uintptr_t>(pd.base_address);
	cr3 = pd.cr3;
}
#endif

bool KmReadDriver::initdriver(int processid) {
	shutdown();
	last_init_error_.clear();
	cached_client = 0;
	cached_engine = 0;
	attached_pid = static_cast<DWORD>(processid);
	kernel_ue5::process_id = static_cast<INT32>(processid);

	const auto fail = [this](const char* msg) -> bool {
		last_init_error_ = msg ? msg : "Driver init failed.";
		printf("[KM] %s\n", last_init_error_.c_str());
		fflush(stdout);
		return false;
	};

	if (!attached_pid)
		return fail("Invalid CS2 process ID.");

#if defined(EXPECTIONAL_USE_R69)
	std::string attach_err;
	if (!expectional_r69_bridge::init_for_process(attached_pid, &attach_err)) {
		std::string msg = "Kernel driver attach failed.\n";
		if (!attach_err.empty())
			msg += "Error details: " + attach_err + "\n";
		msg += "Run as Administrator, reboot if a stale driver is loaded, then try again.";
		return fail(msg.c_str());
	}

	const auto& pd = expectional_r69::attached_process();
	virtualaddy = reinterpret_cast<uintptr_t>(pd.base_address);
	cr3 = pd.cr3;
	printf("[KM] attach pid=%lu base=0x%llX cr3=0x%llX\n",
	    static_cast<unsigned long>(attached_pid),
	    static_cast<unsigned long long>(virtualaddy),
	    static_cast<unsigned long long>(cr3));
	fflush(stdout);

	if (!virtualaddy || !cr3) {
		expectional_r69_bridge::shutdown_r69();
		return fail("Kernel driver attached but process data is invalid.");
	}

	if (!expectional_r69::probe_image_header(static_cast<std::uint64_t>(virtualaddy))) {
		expectional_r69_bridge::shutdown_r69();
		return fail(
		    "Kernel memory read failed at the CS2 image base.\n"
		    "Reboot, run as Administrator, then try again.");
	}
	printf("[KM] kernel read: OK\n");
	fflush(stdout);

	constexpr int kModuleResolveAttempts = 40;
	std::string client_resolve_error;
	std::string engine_resolve_error;
	int verified_highest_entity = -1;

	for (int attempt = 0; attempt < kModuleResolveAttempts; ++attempt) {
		if (attempt > 0 && (attempt % 8) == 0 && ReattachCs2Process(attached_pid)) {
			RefreshAttachedProcessMeta();
			printf("[KM] reattach cr3=0x%llX\n", static_cast<unsigned long long>(cr3));
			fflush(stdout);
		}
		cached_client = 0;
		cached_engine = 0;
		if (ResolveClientEngineModules(
		        cached_client,
		        cached_engine,
		        client_resolve_error,
		        engine_resolve_error,
		        &verified_highest_entity))
			break;
		if (attempt + 1 < kModuleResolveAttempts)
			std::this_thread::sleep_for(std::chrono::milliseconds(250));
	}

	if (!cached_client || !cached_engine) {
		if (ResolveClientEngineModules(
		        cached_client,
		        cached_engine,
		        client_resolve_error,
		        engine_resolve_error,
		        &verified_highest_entity)) {
			/* ok */
		}
	}

	if (!cached_client) {
		expectional_r69_bridge::shutdown_r69();
		char detail[640]{};
		std::snprintf(
		    detail,
		    sizeof(detail),
		    "%s\nJoin CS2 main menu or a match, then restart Expectional.",
		    client_resolve_error.empty()
		        ? "Kernel client lookup failed (LDR name + view matrix / entity probe)."
		        : client_resolve_error.c_str());
		return fail(detail);
	}
	if (!cached_engine) {
		expectional_r69_bridge::shutdown_r69();
		char detail[320]{};
		std::snprintf(
		    detail,
		    sizeof(detail),
		    "Kernel engine lookup failed (LDR name + offset/build probe).\n"
		    "Modules in LDR: name match and dwNetworkGameClient probe both failed.\n"
		    "Join CS2 main menu or a match, then restart Expectional.");
		if (!engine_resolve_error.empty()) {
			std::snprintf(
			    detail,
			    sizeof(detail),
			    "%s\n\nOffset/build probe also found no engine module.",
			    engine_resolve_error.c_str());
		}
		return fail(detail);
	}

	{
		const DWORD live_pid = expectional_r69_bridge::find_cs2_process_id_light();
		if (live_pid && live_pid != attached_pid) {
			attached_pid = live_pid;
			kernel_ue5::process_id = static_cast<INT32>(live_pid);
		}
		if (ReattachCs2Process(attached_pid)) {
			RefreshAttachedProcessMeta();
			uintptr_t new_client = 0;
			uintptr_t new_engine = 0;
			std::string tmp_client_err;
			std::string tmp_engine_err;
			int tmp_hi = -1;
			if (ResolveClientEngineModules(new_client, new_engine, tmp_client_err, tmp_engine_err, &tmp_hi) &&
			    new_client &&
			    new_engine) {
				cached_client = new_client;
				cached_engine = new_engine;
				verified_highest_entity = tmp_hi;
			}
		}
	}

	{
		const std::uint32_t client_sz = KernelReadClientSizeOfImage(cached_client);
		if (client_sz) {
			printf("[KM] client SizeOfImage=0x%X (kernel PE)\n", client_sz);
			fflush(stdout);
		}
		std::uint64_t need = 0;
		if (offsets::dwEntityList)
			need = (std::max)(need, static_cast<std::uint64_t>(offsets::dwEntityList) + 8ull);
		if (offsets::dwViewMatrix)
			need = (std::max)(need, static_cast<std::uint64_t>(offsets::dwViewMatrix) + 64ull);
		if (offsets::dwLocalPlayerPawn)
			need = (std::max)(need, static_cast<std::uint64_t>(offsets::dwLocalPlayerPawn) + 8ull);
		if (client_sz && need && static_cast<std::uint64_t>(client_sz) < need) {
			printf(
			    "[KM] wrong client.dll (size 0x%X < 0x%llX), kernel rescan\n",
			    client_sz,
			    static_cast<unsigned long long>(need));
			fflush(stdout);
			const uintptr_t bad = cached_client;
			const std::uint64_t probed = ProbeClientModuleBase(bad);
			if (probed && probed != bad) {
				cached_client = static_cast<uintptr_t>(probed);
				const std::uint32_t sz2 = KernelReadClientSizeOfImage(cached_client);
				printf(
				    "[KM] client.dll @ 0x%llX SizeOfImage=0x%X (kernel)\n",
				    static_cast<unsigned long long>(cached_client),
				    sz2);
				fflush(stdout);
			}
		}
	}

	MenuReadyInfo menu_info{};
	constexpr int kMenuPollMs = 400;
	constexpr int kMenuWaitAttempts = 25; /* ~10 sn: entity list kernel'den gelsin */
	for (int menu_wait = 0; menu_wait < kMenuWaitAttempts; ++menu_wait) {
		const std::uint32_t live_sz = KernelReadClientSizeOfImage(cached_client);
		if (live_sz && offsets::dwEntityList &&
		    static_cast<std::uint64_t>(live_sz) < static_cast<std::uint64_t>(offsets::dwEntityList) + 8ull) {
			printf("[KM] entity list RVA is outside this module — not CS2 client.dll\n");
			fflush(stdout);
			break;
		}
		(void)KernelFixupEntityList(cached_client);
		(void)KernelFixupLocalPawn(cached_client);
		(void)ProbeMainMenuReady(cached_client, &menu_info);
		if (menu_info.entity_list_ok)
			break;
		if ((menu_wait % 5) == 0) {
			std::uint64_t elist_raw = 0;
			(void)KernelReadVa(
			    cached_client + static_cast<uintptr_t>(offsets::dwEntityList),
			    &elist_raw,
			    sizeof(elist_raw));
			printf(
			    "> waiting for entity list... (kernel raw=0x%llX rva=0x%llX)\n",
			    static_cast<unsigned long long>(elist_raw),
			    static_cast<unsigned long long>(offsets::dwEntityList));
			fflush(stdout);
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(kMenuPollMs));

		const DWORD live_pid = expectional_r69_bridge::find_cs2_process_id_light();
		if (live_pid && live_pid != attached_pid) {
			attached_pid = live_pid;
			kernel_ue5::process_id = static_cast<INT32>(live_pid);
		}
		if (!ReattachCs2Process(attached_pid))
			continue;
		RefreshAttachedProcessMeta();

		uintptr_t new_client = 0;
		uintptr_t new_engine = 0;
		std::string tmp_client_err;
		std::string tmp_engine_err;
		int tmp_hi = -1;
		if (ResolveClientEngineModules(new_client, new_engine, tmp_client_err, tmp_engine_err, &tmp_hi) &&
		    new_client &&
		    new_engine) {
			cached_client = new_client;
			cached_engine = new_engine;
			verified_highest_entity = tmp_hi;
		}
	}

	if (!cached_client || !cached_engine) {
		expectional_r69_bridge::shutdown_r69();
		return fail(
		    "Kernel module verify failed.\n"
		    "client/engine bases were found but game offsets do not read correctly.\n"
		    "Join a match or main menu, then restart Expectional.");
	}

	(void)KernelFixupEntityList(cached_client);
	(void)KernelFixupLocalPawn(cached_client);
	(void)ProbeMainMenuReady(cached_client, &menu_info);

	std::uint64_t elist_raw = 0;
	std::uint64_t pawn_raw = 0;
	(void)KernelReadVa(
	    cached_client + static_cast<uintptr_t>(offsets::dwEntityList),
	    &elist_raw,
	    sizeof(elist_raw));
	(void)KernelReadVa(
	    cached_client + static_cast<uintptr_t>(offsets::dwLocalPlayerPawn),
	    &pawn_raw,
	    sizeof(pawn_raw));

	printf("[KM] client.dll @ 0x%llX\n", static_cast<unsigned long long>(cached_client));
	printf("[KM] engine2.dll @ 0x%llX\n", static_cast<unsigned long long>(cached_engine));
	printf(
	    "[KM] modules verify: OK (elist=%d raw=0x%llX highest=%d pawn=%d pawn_raw=0x%llX view=%d)\n",
	    menu_info.entity_list_ok ? 1 : 0,
	    static_cast<unsigned long long>(elist_raw),
	    menu_info.highest,
	    menu_info.local_pawn_ok ? 1 : 0,
	    static_cast<unsigned long long>(pawn_raw),
	    menu_info.view_ok ? 1 : 0);
	fflush(stdout);
	return true;
#else
	if (!kernel_ue5::init())
		return fail("Kernel driver init failed.");

	virtualaddy = kernel_ue5::base_address();
	(void)kernel_ue5::fetch_cr3();

	cached_client = module_base_from_snapshot(attached_pid, L"client.dll");
	cached_engine = module_base_from_snapshot(attached_pid, L"engine2.dll");
	if (!cached_client)
		return fail("Could not find client.dll.");
	if (!cached_engine)
		return fail("Could not find engine2.dll.");
#endif

	printf("[KM] client.dll @ 0x%llX\n", static_cast<unsigned long long>(cached_client));
	printf("[KM] engine2.dll @ 0x%llX\n", static_cast<unsigned long long>(cached_engine));
	fflush(stdout);
	return true;
}

void KmReadDriver::shutdown() {
#if defined(EXPECTIONAL_USE_R69)
	KernelModuleBaseCache().clear();
	expectional_r69_bridge::shutdown_r69();
#else
	if (kernel_ue5::driver_handle != INVALID_HANDLE_VALUE && kernel_ue5::driver_handle != nullptr) {
		CloseHandle(kernel_ue5::driver_handle);
		kernel_ue5::driver_handle = INVALID_HANDLE_VALUE;
	}
#endif
	cached_client = 0;
	cached_engine = 0;
	attached_pid = 0;
	kernel_ue5::process_id = 0;
	virtualaddy = 0;
	cr3 = 0;
}

uintptr_t KmReadDriver::client_address() {
	if (cached_client)
		return cached_client;
#if !defined(EXPECTIONAL_USE_R69)
	if (attached_pid)
		cached_client = module_base_from_snapshot(attached_pid, L"client.dll");
#endif
	return cached_client;
}

uintptr_t KmReadDriver::engine_address() {
	if (cached_engine)
		return cached_engine;
#if !defined(EXPECTIONAL_USE_R69)
	if (attached_pid)
		cached_engine = module_base_from_snapshot(attached_pid, L"engine2.dll");
#endif
	return cached_engine;
}

uintptr_t KmReadDriver::module_address(const wchar_t* module_name) {
	if (!module_name || !module_name[0] || !attached_pid)
		return 0;

#if defined(EXPECTIONAL_USE_R69)
	auto& cache = KernelModuleBaseCache();
	const std::wstring key(module_name);
	const auto it = cache.find(key);
	if (it != cache.end())
		return it->second;

	uintptr_t base = 0;
	std::string ignored_error;
	if (ResolveModuleBaseKernelOnly(module_name, base, ignored_error) && base) {
		cache[key] = base;
		return base;
	}
	return 0;
#else
	return module_base_from_snapshot(attached_pid, module_name);
#endif
}

std::string KmReadDriver::ReadString(uintptr_t addr, size_t maxLen) {
	if (!addr || maxLen == 0)
		return {};
	constexpr size_t kCap = 512;
	if (maxLen > kCap)
		maxLen = kCap;
	std::vector<char> buf(maxLen + 1, 0);
	for (size_t i = 0; i < maxLen; ++i) {
		char c = 0;
		if (!km_device_read(addr + i, &c, 1))
			break;
		buf[static_cast<size_t>(i)] = c;
		if (c == '\0')
			break;
	}
	return std::string(buf.data());
}

KmReadDriver g_GameMem;
