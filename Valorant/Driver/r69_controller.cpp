#include "r69_controller.hpp"

#include <algorithm>
#include <cmath>
#include <cwctype>
#include <string>
#include <unordered_set>
#include <vector>

namespace expectional_r69 {

using NtQueryAuxiliaryCounterFrequency_compat_t = NTSTATUS (*)(std::uint64_t*, c_packet*);

static NtQueryAuxiliaryCounterFrequency_compat_t g_nt_query_aux = nullptr;

static query_process_data_packet g_pd{};
static bool g_ready = false;

static NTSTATUS issue_syscall(c_packet* packet) {
	if (!packet)
		return static_cast<NTSTATUS>(0xC0000005L);

	if (!g_nt_query_aux) {
		HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
		if (!ntdll)
			return static_cast<NTSTATUS>(0xC000007Au);
		g_nt_query_aux = reinterpret_cast<NtQueryAuxiliaryCounterFrequency_compat_t>(
			GetProcAddress(ntdll, "NtQueryAuxiliaryCounterFrequency"));
	}
	if (!g_nt_query_aux)
		return static_cast<NTSTATUS>(0xC000007Au);

	std::uint64_t dummy_counter = 0;
	return g_nt_query_aux(&dummy_counter, packet);
}

NTSTATUS read_memory(std::uint64_t src_va, void* dest, std::uint64_t size) {
	if (!src_va || !dest || !size)
		return static_cast<NTSTATUS>(0xC000000DL);
	if (!g_ready)
		return static_cast<NTSTATUS>(0xC0000001L);

	copy_process_memory_packet input{};
	input.process_id = g_pd.process_id;
	input.source = src_va;
	input.dest = dest;
	input.size = size;
	c_packet pkt(e_syscall::read_process_memory, &input, sizeof(input));
	return issue_syscall(&pkt);
}

namespace {

template <typename T>
bool ReadVa(std::uint64_t src, T& out) {
	if (!src)
		return false;
	return NT_SUCCESS(read_memory(src, &out, sizeof(T)));
}

static bool ReadLdrUnicodeString(const UNICODE_STRING& module_string, std::wstring& out) {
	out.clear();
	if (!module_string.Buffer || !module_string.Length)
		return false;

	const std::uint64_t byte_len = module_string.Length;
	const size_t wchar_count = static_cast<size_t>(byte_len / sizeof(wchar_t));
	if (wchar_count == 0)
		return false;

	out.resize(wchar_count);
	const NTSTATUS st = read_memory(
	    reinterpret_cast<std::uint64_t>(module_string.Buffer),
	    out.data(),
	    byte_len);
	return NT_SUCCESS(st);
}

static bool BaseDllNameExact(const UNICODE_STRING& base_name, const wchar_t* module_name) {
	std::wstring name;
	if (!ReadLdrUnicodeString(base_name, name))
		return false;
	return _wcsicmp(name.c_str(), module_name) == 0;
}

static bool FullDllNameEndsWithModule(const UNICODE_STRING& full_name, const wchar_t* module_name) {
	std::wstring path;
	if (!ReadLdrUnicodeString(full_name, path) || path.empty() || !module_name || !module_name[0])
		return false;

	const size_t name_len = wcslen(module_name);
	if (path.size() < name_len)
		return false;
	if (path.size() == name_len)
		return _wcsicmp(path.c_str(), module_name) == 0;

	const wchar_t sep = path[path.size() - name_len - 1];
	if (sep != L'\\' && sep != L'/')
		return false;
	return _wcsicmp(path.c_str() + (path.size() - name_len), module_name) == 0;
}

static bool ProbeImageHeaderLocal(std::uint64_t image_base) {
	if (!image_base)
		return false;
	std::uint16_t magic = 0;
	return ReadVa(image_base, magic) && magic == 0x5A4D;
}

static std::uint32_t ReadPeSizeOfImage(std::uint64_t image_base) {
	if (!image_base)
		return 0;
	std::uint16_t mz = 0;
	if (!ReadVa(image_base, mz) || mz != 0x5A4D)
		return 0;
	std::int32_t e_lfanew = 0;
	if (!ReadVa(image_base + 0x3C, e_lfanew) || e_lfanew < 0x40 || e_lfanew > 0x1000)
		return 0;
	std::uint32_t pe = 0;
	if (!ReadVa(image_base + static_cast<std::uint64_t>(e_lfanew), pe) || pe != 0x00004550u)
		return 0;
	std::uint32_t size = 0;
	if (!ReadVa(image_base + static_cast<std::uint64_t>(e_lfanew) + 0x50, size))
		return 0;
	if (size < 0x1000u || size > 0x10000000u)
		return 0;
	return size;
}

static bool ModuleImageCoversRva(std::uint64_t image_base, std::uint64_t rva, std::uint32_t known_size = 0) {
	if (!rva)
		return true;
	std::uint32_t size = known_size;
	if (!size)
		size = ReadPeSizeOfImage(image_base);
	if (!size)
		return false;
	return rva + 16ull < static_cast<std::uint64_t>(size);
}

struct PEB_LDR_DATA_FULL {
	ULONG Length;
	UCHAR Initialized;
	UCHAR Reserved[3];
	PVOID SsHandle;
	LIST_ENTRY InLoadOrderModuleList;
	LIST_ENTRY InMemoryOrderModuleList;
	LIST_ENTRY InInitializationOrderModuleList;
};

constexpr std::size_t kLdrEntryDllBase = 0x30;
constexpr std::size_t kLdrEntryFullDllName = 0x48;
constexpr std::size_t kLdrEntryBaseDllName = 0x58;
constexpr std::size_t kLdrEntryInInitOrderLinks = 0x20;
constexpr std::size_t kLdrEntryInMemoryOrderLinks = 0x10;

static bool ModuleEntryMatches(const UNICODE_STRING& full_name, const UNICODE_STRING& base_name, const wchar_t* module_name) {
	if (BaseDllNameExact(base_name, module_name))
		return true;
	if (FullDllNameEndsWithModule(full_name, module_name))
		return true;

	return false;
}

struct LdrModuleSnapshot {
	std::uint64_t base = 0;
	std::uint32_t size_of_image = 0;
	std::wstring base_name;
	std::wstring full_name;
};

static void WalkLdrModuleListCollect(
    std::uint64_t list_head,
    std::uint64_t list_entry,
    std::size_t entry_link_offset,
    std::vector<LdrModuleSnapshot>& out,
    std::unordered_set<std::uint64_t>& seen) {
	constexpr int kMaxModules = 512;

	for (int i = 0; i < kMaxModules && list_entry && list_entry != list_head; ++i) {
		const std::uint64_t entry_va = list_entry - entry_link_offset;

		PVOID dll_base = nullptr;
		UNICODE_STRING full_name{};
		UNICODE_STRING base_name{};
		if (!ReadVa(entry_va + kLdrEntryDllBase, dll_base)) {
			std::uint64_t next = 0;
			if (!ReadVa(list_entry, next))
				break;
			list_entry = next;
			continue;
		}
		(void)ReadVa(entry_va + kLdrEntryFullDllName, full_name);
		(void)ReadVa(entry_va + kLdrEntryBaseDllName, base_name);

		const std::uint64_t base = reinterpret_cast<std::uint64_t>(dll_base);
		if (base && seen.insert(base).second) {
			LdrModuleSnapshot snap{};
			snap.base = base;
			(void)ReadVa(entry_va + 0x40, snap.size_of_image);
			(void)ReadLdrUnicodeString(base_name, snap.base_name);
			(void)ReadLdrUnicodeString(full_name, snap.full_name);
			out.push_back(std::move(snap));
		}

		std::uint64_t next = 0;
		if (!ReadVa(list_entry, next))
			break;
		list_entry = next;
	}
}

static bool CollectAllLdrModules(std::vector<LdrModuleSnapshot>& out) {
	out.clear();
	if (!g_ready || !g_pd.peb)
		return false;

	PEB peb{};
	if (!ReadVa(reinterpret_cast<std::uint64_t>(g_pd.peb), peb) || !peb.Ldr)
		return false;

	PEB_LDR_DATA_FULL ldr{};
	if (!ReadVa(reinterpret_cast<std::uint64_t>(peb.Ldr), ldr))
		return false;

	std::unordered_set<std::uint64_t> seen;
	const std::uint64_t ldr_va = reinterpret_cast<std::uint64_t>(peb.Ldr);
	WalkLdrModuleListCollect(
	    ldr_va + offsetof(PEB_LDR_DATA_FULL, InLoadOrderModuleList),
	    reinterpret_cast<std::uint64_t>(ldr.InLoadOrderModuleList.Flink),
	    0x0,
	    out,
	    seen);
	WalkLdrModuleListCollect(
	    ldr_va + offsetof(PEB_LDR_DATA_FULL, InMemoryOrderModuleList),
	    reinterpret_cast<std::uint64_t>(ldr.InMemoryOrderModuleList.Flink),
	    kLdrEntryInMemoryOrderLinks,
	    out,
	    seen);
	WalkLdrModuleListCollect(
	    ldr_va + offsetof(PEB_LDR_DATA_FULL, InInitializationOrderModuleList),
	    reinterpret_cast<std::uint64_t>(ldr.InInitializationOrderModuleList.Flink),
	    kLdrEntryInInitOrderLinks,
	    out,
	    seen);
	return !out.empty();
}

static bool LooksLikeNetworkGameClient(std::uint64_t ngc_ptr) {
	if (ngc_ptr < 0x10000 || ngc_ptr > 0x00007FFFFFFEFFFF)
		return false;

	std::uint64_t header = 0;
	if (!ReadVa(ngc_ptr, header) || header < 0x10000)
		return false;

	std::int32_t sign_on = 0;
	if (ReadVa(ngc_ptr + 0x230, sign_on) && sign_on >= 0 && sign_on <= 8)
		return true;

	std::int32_t max_clients = 0;
	if (ReadVa(ngc_ptr + 0x240, max_clients) && max_clients >= 0 && max_clients <= 64)
		return true;

	return true;
}

static bool LooksLikeEngineBuildNumber(std::uint64_t module_base, std::uint64_t build_rva) {
	if (!module_base || !build_rva)
		return false;
	std::uint32_t build = 0;
	if (!ReadVa(module_base + build_rva, build))
		return false;
	return build >= 14000000u && build <= 200000000u;
}

static bool LooksLikeEngineWindowSize(
    std::uint64_t module_base,
    std::uint64_t width_rva,
    std::uint64_t height_rva) {
	if (!module_base || !width_rva || !height_rva)
		return false;
	std::uint32_t width = 0;
	std::uint32_t height = 0;
	if (!ReadVa(module_base + width_rva, width) || !ReadVa(module_base + height_rva, height))
		return false;
	if (width < 640u || width > 7680u || height < 480u || height > 4320u)
		return false;
	if (height > width + 200u)
		return false;
	return true;
}

static bool WeakNetworkGameClientPointer(std::uint64_t module_base, std::uint64_t ngc_rva, std::uint64_t& out_ptr) {
	out_ptr = 0;
	if (!module_base || !ngc_rva)
		return false;
	std::uint64_t ngc_ptr = 0;
	if (!ReadVa(module_base + ngc_rva, ngc_ptr))
		return false;
	if (ngc_ptr < 0x10000 || ngc_ptr > 0x00007FFFFFFEFFFF)
		return false;
	out_ptr = ngc_ptr;
	return true;
}

static int EngineNameScore(const LdrModuleSnapshot& mod) {
	int score = 0;
	if (!mod.base_name.empty() && mod.base_name.find(L"engine2") != std::wstring::npos)
		score += 8;
	if (!mod.full_name.empty() && mod.full_name.find(L"engine2") != std::wstring::npos)
		score += 8;
	if (!mod.base_name.empty() && mod.base_name.find(L"engine") != std::wstring::npos)
		score += 4;
	if (!mod.full_name.empty() && mod.full_name.find(L"engine") != std::wstring::npos)
		score += 4;
	return score;
}

static int ClientNameScore(const LdrModuleSnapshot& mod) {
	int score = 0;
	if (!mod.base_name.empty() && mod.base_name.find(L"client") != std::wstring::npos)
		score += 8;
	if (!mod.full_name.empty() && mod.full_name.find(L"client") != std::wstring::npos)
		score += 8;
	if (!mod.base_name.empty() && mod.base_name.find(L"client.dll") != std::wstring::npos)
		score += 4;
	if (!mod.full_name.empty() && mod.full_name.find(L"client.dll") != std::wstring::npos)
		score += 4;
	return score;
}

static bool LooksLikeViewMatrix(std::uint64_t module_base, std::uint64_t view_matrix_rva) {
	if (!module_base || !view_matrix_rva)
		return false;

	float matrix[4][4]{};
	if (!ReadVa(module_base + view_matrix_rva, matrix))
		return false;

	int non_zero = 0;
	for (int row = 0; row < 4; ++row) {
		for (int col = 0; col < 4; ++col) {
			const float v = matrix[row][col];
			if (!std::isfinite(v))
				return false;
			if (v != 0.f)
				++non_zero;
		}
	}
	if (non_zero < 4)
		return false;

	const float abs00 = std::fabs(matrix[0][0]);
	const float abs11 = std::fabs(matrix[1][1]);
	if (abs00 < 0.0001f && abs11 < 0.0001f)
		return false;

	return true;
}

static bool LooksLikeEntitySystem(
    std::uint64_t module_base,
    std::uint64_t entity_list_rva,
    std::uint64_t highest_entity_index_off) {
	if (!module_base || !entity_list_rva || !highest_entity_index_off)
		return false;

	std::uint64_t entity_system = 0;
	if (!ReadVa(module_base + entity_list_rva, entity_system))
		return false;
	if (entity_system < 0x10000 || entity_system > 0x00007FFFFFFEFFFF)
		return false;

	std::int32_t highest = -1;
	if (!ReadVa(entity_system + highest_entity_index_off, highest))
		return false;
	return highest >= 0 && highest <= 16384;
}

static bool LooksLikeLocalPlayerPawnSlot(std::uint64_t module_base, std::uint64_t local_pawn_rva) {
	if (!module_base || !local_pawn_rva)
		return false;

	std::uint64_t pawn_ptr = 0;
	if (!ReadVa(module_base + local_pawn_rva, pawn_ptr))
		return false;
	if (!pawn_ptr)
		return true;
	if (pawn_ptr < 0x10000 || pawn_ptr > 0x00007FFFFFFEFFFF)
		return false;

	std::uint64_t header = 0;
	return ReadVa(pawn_ptr, header) && header >= 0x10000;
}

static std::uint64_t WalkLdrModuleList(
    std::uint64_t list_head,
    std::uint64_t list_entry,
    std::size_t entry_link_offset,
    const wchar_t* module_name,
    int& modules_scanned,
    std::uint32_t* out_size) {
	constexpr int kMaxModules = 512;
	std::uint64_t named_match = 0;
	std::uint32_t named_size = 0;
	bool named_hdr = false;

	for (int i = 0; i < kMaxModules && list_entry && list_entry != list_head; ++i) {
		++modules_scanned;

		const std::uint64_t entry_va = list_entry - entry_link_offset;

		PVOID dll_base = nullptr;
		UNICODE_STRING full_name{};
		UNICODE_STRING base_name{};
		if (!ReadVa(entry_va + kLdrEntryDllBase, dll_base)) {
			std::uint64_t next = 0;
			if (!ReadVa(list_entry, next))
				break;
			list_entry = next;
			continue;
		}
		(void)ReadVa(entry_va + kLdrEntryFullDllName, full_name);
		(void)ReadVa(entry_va + kLdrEntryBaseDllName, base_name);

		if (dll_base && ModuleEntryMatches(full_name, base_name, module_name)) {
			const std::uint64_t base = reinterpret_cast<std::uint64_t>(dll_base);
			std::uint32_t sz = 0;
			(void)ReadVa(entry_va + 0x40, sz);
			if (!sz)
				sz = ReadPeSizeOfImage(base);
			const bool hdr = ProbeImageHeaderLocal(base);
			
			const bool better = !named_match || (hdr && !named_hdr) || (hdr == named_hdr && sz > named_size);
			if (better) {
				named_match = base;
				named_size = sz;
				named_hdr = hdr;
			}
		}

		std::uint64_t next = 0;
		if (!ReadVa(list_entry, next))
			break;
		list_entry = next;
	}

	if (out_size)
		*out_size = named_size;
	return named_match;
}

struct SYSTEM_PROCESS_INFO {
	ULONG NextEntryOffset;
	ULONG NumberOfThreads;
	LARGE_INTEGER WorkingSetPrivateSize;
	ULONG HardFaultCount;
	ULONG NumberOfThreadsHighWatermark;
	ULONGLONG CycleTime;
	LARGE_INTEGER CreateTime;
	LARGE_INTEGER UserTime;
	LARGE_INTEGER KernelTime;
	UNICODE_STRING ImageName;
	KPRIORITY BasePriority;
	HANDLE UniqueProcessId;
};

static bool ImageNameMatchesProcess(const UNICODE_STRING& image, const wchar_t* target) {
	if (!target || !target[0])
		return false;
	if (!image.Buffer || !image.Length)
		return false;

	const size_t wchar_count = static_cast<size_t>(image.Length / sizeof(wchar_t));
	if (wchar_count == 0)
		return false;

	std::wstring name(image.Buffer, wchar_count);
	const size_t pos = name.find_last_of(L"\\/");
	const wchar_t* base = (pos == std::wstring::npos) ? name.c_str() : name.c_str() + pos + 1;
	return _wcsicmp(base, target) == 0;
}

static std::vector<std::uint32_t> CollectProcessIdsByImageName(const wchar_t* image_name) {
	std::vector<std::uint32_t> pids;
	if (!image_name || !image_name[0])
		return pids;

	HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
	if (!ntdll)
		return pids;

	using NtQuerySystemInformation_t = NTSTATUS (NTAPI*)(ULONG, PVOID, ULONG, PULONG);
	const auto NtQuerySystemInformation =
	    reinterpret_cast<NtQuerySystemInformation_t>(GetProcAddress(ntdll, "NtQuerySystemInformation"));
	if (!NtQuerySystemInformation)
		return pids;

	constexpr ULONG kSystemProcessInformation = 5;
	ULONG buffer_size = 1u << 20;
	std::vector<std::uint8_t> buffer(buffer_size);
	NTSTATUS status = NtQuerySystemInformation(
	    kSystemProcessInformation, buffer.data(), buffer_size, &buffer_size);
	if (status == static_cast<NTSTATUS>(0xC0000004L)) {
		buffer.resize(buffer_size);
		status = NtQuerySystemInformation(
		    kSystemProcessInformation, buffer.data(), buffer_size, &buffer_size);
	}
	if (!NT_SUCCESS(status))
		return pids;

	const auto* entry = reinterpret_cast<const SYSTEM_PROCESS_INFO*>(buffer.data());
	for (;;) {
		if (ImageNameMatchesProcess(entry->ImageName, image_name)) {
			const std::uint32_t pid = static_cast<std::uint32_t>(
			    reinterpret_cast<std::uintptr_t>(entry->UniqueProcessId));
			if (pid)
				pids.push_back(pid);
		}
		if (!entry->NextEntryOffset)
			break;
		entry = reinterpret_cast<const SYSTEM_PROCESS_INFO*>(
		    reinterpret_cast<const std::uint8_t*>(entry) + entry->NextEntryOffset);
	}
	return pids;
}

} 

std::uint32_t find_process_id_by_image_name_light(const wchar_t* image_name) {
	if (!image_name || !image_name[0])
		return 0;
	const std::vector<std::uint32_t> candidates = CollectProcessIdsByImageName(image_name);
	return candidates.empty() ? 0 : candidates.front();
}

std::uint32_t find_process_id_by_image_name(const wchar_t* image_name, const wchar_t* verify_module) {
	if (!image_name || !image_name[0])
		return 0;

	const std::vector<std::uint32_t> candidates = CollectProcessIdsByImageName(image_name);
	for (const std::uint32_t pid : candidates) {
		if (!attach(pid))
			continue;

		if (verify_module && verify_module[0]) {
			const module_resolve_result verified = resolve_module_base(verify_module);
			if (!verified.base) {
				detach();
				continue;
			}
		}

		return pid;
	}

	detach();
	return 0;
}

bool probe_image_header(std::uint64_t image_base) {
	if (!g_ready || !image_base)
		return false;
	std::uint16_t magic = 0;
	return ReadVa(image_base, magic) && magic == 0x5A4D; 
}

module_resolve_result resolve_module_base(const wchar_t* module_name) {
	module_resolve_result result{};
	if (!g_ready || !module_name || !module_name[0]) {
		result.error = module_resolve_error::not_ready;
		return result;
	}
	if (!g_pd.peb) {
		result.error = module_resolve_error::no_peb;
		return result;
	}

	PEB peb{};
	if (!ReadVa(reinterpret_cast<std::uint64_t>(g_pd.peb), peb)) {
		result.error = module_resolve_error::peb_unreadable;
		return result;
	}
	if (!peb.Ldr) {
		result.error = module_resolve_error::no_ldr;
		return result;
	}

	PEB_LDR_DATA_FULL ldr{};
	if (!ReadVa(reinterpret_cast<std::uint64_t>(peb.Ldr), ldr)) {
		result.error = module_resolve_error::ldr_unreadable;
		return result;
	}

	const std::uint64_t ldr_va = reinterpret_cast<std::uint64_t>(peb.Ldr);
	std::uint32_t image_size = 0;
	std::uint64_t base = WalkLdrModuleList(
	    ldr_va + offsetof(PEB_LDR_DATA_FULL, InLoadOrderModuleList),
	    reinterpret_cast<std::uint64_t>(ldr.InLoadOrderModuleList.Flink),
	    0x0,
	    module_name,
	    result.modules_scanned,
	    &image_size);
	if (!base) {
		base = WalkLdrModuleList(
		    ldr_va + offsetof(PEB_LDR_DATA_FULL, InMemoryOrderModuleList),
		    reinterpret_cast<std::uint64_t>(ldr.InMemoryOrderModuleList.Flink),
		    kLdrEntryInMemoryOrderLinks,
		    module_name,
		    result.modules_scanned,
		    &image_size);
	}
	if (!base) {
		base = WalkLdrModuleList(
		    ldr_va + offsetof(PEB_LDR_DATA_FULL, InInitializationOrderModuleList),
		    reinterpret_cast<std::uint64_t>(ldr.InInitializationOrderModuleList.Flink),
		    kLdrEntryInInitOrderLinks,
		    module_name,
		    result.modules_scanned,
		    &image_size);
	}
	if (base) {
		result.base = base;
		result.size_of_image = image_size;
		result.error = module_resolve_error::none;
		return result;
	}
	if (result.modules_scanned == 0) {
		result.error = module_resolve_error::list_unreadable;
		return result;
	}

	result.error = module_resolve_error::not_found;
	return result;
}

static bool ClientModuleLooksValid(
    std::uint64_t base,
    const client_probe_params& params,
    int* out_highest_entity) {
	if (out_highest_entity)
		*out_highest_entity = -1;
	if (!g_ready || !base)
		return false;

	const std::uint64_t entity_list_rva = params.entity_list_rva;
	const std::uint64_t view_matrix_rva = params.view_matrix_rva;
	const std::uint64_t local_pawn_rva = params.local_player_pawn_rva;
	const std::uint64_t highest_entity_index_off = params.highest_entity_index_off;

	if (!ModuleImageCoversRva(base, view_matrix_rva) ||
	    !ModuleImageCoversRva(base, entity_list_rva) ||
	    !ModuleImageCoversRva(base, local_pawn_rva))
		return false;

	if (!LooksLikeViewMatrix(base, view_matrix_rva)) {
		if (!highest_entity_index_off ||
		    !LooksLikeEntitySystem(base, entity_list_rva, highest_entity_index_off))
			return false;
	}

	if (out_highest_entity && highest_entity_index_off) {
		std::uint64_t entity_system = 0;
		if (ReadVa(base + entity_list_rva, entity_system) &&
		    entity_system >= 0x10000 &&
		    entity_system <= 0x00007FFFFFFEFFFF) {
			std::int32_t highest = -1;
			if (ReadVa(entity_system + highest_entity_index_off, highest) &&
			    highest >= 0 &&
			    highest <= 16384) {
				*out_highest_entity = static_cast<int>(highest);
			}
		}
	}
	return true;
}

static bool EngineModuleLooksValid(std::uint64_t base, const engine_probe_params& params) {
	if (!g_ready || !base)
		return false;

	if (params.build_number_rva && LooksLikeEngineBuildNumber(base, params.build_number_rva))
		return true;
	if (params.window_width_rva && params.window_height_rva &&
	    LooksLikeEngineWindowSize(base, params.window_width_rva, params.window_height_rva))
		return true;

	static const std::uint64_t kNgcRvaCandidates[] = {0x90A1A0, 0x90A0C0, 0x9090A0};
	for (std::uint64_t ngc_rva : kNgcRvaCandidates) {
		if (!ngc_rva)
			continue;
		std::uint64_t ngc_ptr = 0;
		if (WeakNetworkGameClientPointer(base, ngc_rva, ngc_ptr) && LooksLikeNetworkGameClient(ngc_ptr))
			return true;
	}

	if (params.network_game_client_rva) {
		std::uint64_t ngc_ptr = 0;
		if (WeakNetworkGameClientPointer(base, params.network_game_client_rva, ngc_ptr) &&
		    LooksLikeNetworkGameClient(ngc_ptr))
			return true;
	}

	return false;
}

std::uint64_t find_engine_module_by_offset(const engine_probe_params& params) {
	if (!g_ready)
		return 0;

	const std::uint64_t network_game_client_rva = params.network_game_client_rva;
	const std::uint64_t build_number_rva = params.build_number_rva;
	const std::uint64_t window_width_rva = params.window_width_rva;
	const std::uint64_t window_height_rva = params.window_height_rva;
	const std::uint64_t skip_module_base = params.skip_module_base;

	const std::uint64_t process_base = reinterpret_cast<std::uint64_t>(g_pd.base_address);
	std::vector<LdrModuleSnapshot> modules;
	if (!CollectAllLdrModules(modules))
		return 0;

	static const std::uint64_t kNgcRvaCandidates[] = {0x90A1A0, 0x90A0C0, 0x9090A0};

	auto skip_mod = [&](const LdrModuleSnapshot& mod) -> bool {
		if (!mod.base || mod.base < 0x10000)
			return true;
		if (skip_module_base && mod.base == skip_module_base)
			return true;
		if (process_base && mod.base == process_base)
			return true;
		return false;
	};

	auto module_score = [&](const LdrModuleSnapshot& mod, int base_score) -> int {
		int score = base_score + EngineNameScore(mod);
		if (ProbeImageHeaderLocal(mod.base))
			score += 4;
		if (mod.size_of_image >= 0x01000000u)
			score += static_cast<int>(mod.size_of_image >> 22);
		return score;
	};

	std::uint64_t best = 0;
	int best_score = -1;

	for (const LdrModuleSnapshot& mod : modules) {
		if (skip_mod(mod))
			continue;

		const int name_score = EngineNameScore(mod);
		if (name_score > 0) {
			const int score = name_score + 100;
			if (score > best_score) {
				best_score = score;
				best = mod.base;
			}
		}
	}

	if (best && EngineModuleLooksValid(best, params))
		return best;
	best = 0;
	best_score = -1;

	if (window_width_rva && window_height_rva) {
		for (const LdrModuleSnapshot& mod : modules) {
			if (skip_mod(mod))
				continue;
			if (!LooksLikeEngineWindowSize(mod.base, window_width_rva, window_height_rva))
				continue;
			const int score = module_score(mod, 95);
			if (score > best_score) {
				best_score = score;
				best = mod.base;
			}
		}
	}

	if (best && EngineModuleLooksValid(best, params))
		return best;
	best = 0;
	best_score = -1;

	if (build_number_rva) {
		for (const LdrModuleSnapshot& mod : modules) {
			if (skip_mod(mod))
				continue;
			if (!LooksLikeEngineBuildNumber(mod.base, build_number_rva))
				continue;
			const int score = module_score(mod, 85);
			if (score > best_score) {
				best_score = score;
				best = mod.base;
			}
		}
	}

	if (best && EngineModuleLooksValid(best, params))
		return best;

	for (std::uint64_t ngc_rva : kNgcRvaCandidates) {
		if (!ngc_rva)
			continue;
		for (const LdrModuleSnapshot& mod : modules) {
			if (skip_mod(mod))
				continue;
			std::uint64_t ngc_ptr = 0;
			if (!WeakNetworkGameClientPointer(mod.base, ngc_rva, ngc_ptr))
				continue;
			if (!LooksLikeNetworkGameClient(ngc_ptr))
				continue;
			const int score = module_score(mod, 70);
			if (score > best_score) {
				best_score = score;
				best = mod.base;
			}
		}
		if (best && EngineModuleLooksValid(best, params))
			return best;
	}

	if (network_game_client_rva) {
		for (const LdrModuleSnapshot& mod : modules) {
			if (skip_mod(mod))
				continue;
			std::uint64_t ngc_ptr = 0;
			if (!WeakNetworkGameClientPointer(mod.base, network_game_client_rva, ngc_ptr))
				continue;
			if (!LooksLikeNetworkGameClient(ngc_ptr))
				continue;
			const int score = module_score(mod, 60);
			if (score > best_score) {
				best_score = score;
				best = mod.base;
			}
		}
	}

	if (best && EngineModuleLooksValid(best, params))
		return best;
	return 0;
}

std::uint64_t find_client_module_by_offset(const client_probe_params& params) {
	if (!g_ready)
		return 0;

	const std::uint64_t entity_list_rva = params.entity_list_rva;
	const std::uint64_t view_matrix_rva = params.view_matrix_rva;
	const std::uint64_t local_player_pawn_rva = params.local_player_pawn_rva;
	const std::uint64_t highest_entity_index_off = params.highest_entity_index_off;
	const std::uint64_t skip_module_base = params.skip_module_base;

	const std::uint64_t process_base = reinterpret_cast<std::uint64_t>(g_pd.base_address);
	std::vector<LdrModuleSnapshot> modules;
	if (!CollectAllLdrModules(modules))
		return 0;

	auto skip_mod = [&](const LdrModuleSnapshot& mod) -> bool {
		if (!mod.base || mod.base < 0x10000)
			return true;
		if (skip_module_base && mod.base == skip_module_base)
			return true;
		if (process_base && mod.base == process_base)
			return true;
		const std::uint32_t pe_sz = ReadPeSizeOfImage(mod.base);
		const std::uint32_t sz = pe_sz ? pe_sz : mod.size_of_image;
		if (!ModuleImageCoversRva(mod.base, entity_list_rva, sz) ||
		    !ModuleImageCoversRva(mod.base, view_matrix_rva, sz))
			return true;
		return false;
	};

	auto module_score = [&](const LdrModuleSnapshot& mod, int base_score) -> int {
		int score = base_score + ClientNameScore(mod);
		if (ProbeImageHeaderLocal(mod.base))
			score += 4;
		if (mod.size_of_image >= 0x01000000u)
			score += static_cast<int>(mod.size_of_image >> 22);
		return score;
	};

	std::uint64_t best = 0;
	int best_score = -1;

	for (const LdrModuleSnapshot& mod : modules) {
		if (skip_mod(mod))
			continue;

		const int name_score = ClientNameScore(mod);
		if (name_score > 0) {
			const int score = name_score + 100;
			if (score > best_score) {
				best_score = score;
				best = mod.base;
			}
		}
	}

	if (best && ClientModuleLooksValid(best, params, nullptr))
		return best;
	best = 0;
	best_score = -1;

	for (const LdrModuleSnapshot& mod : modules) {
		if (skip_mod(mod))
			continue;
		if (!ClientModuleLooksValid(mod.base, params, nullptr))
			continue;

		int score = module_score(mod, 100);
		if (local_player_pawn_rva && LooksLikeLocalPlayerPawnSlot(mod.base, local_player_pawn_rva))
			score += 4;
		if (score > best_score) {
			best_score = score;
			best = mod.base;
		}
	}

	if (best && ClientModuleLooksValid(best, params, nullptr))
		return best;
	return 0;
}

bool validate_client_module(std::uint64_t base, const client_probe_params& params, int* out_highest_entity) {
	return ClientModuleLooksValid(base, params, out_highest_entity);
}

bool validate_engine_module(std::uint64_t base, const engine_probe_params& params) {
	return EngineModuleLooksValid(base, params);
}

std::uint64_t module_base_by_name(const wchar_t* module_name) {
	return resolve_module_base(module_name).base;
}

bool is_process_alive(std::uint32_t process_id) {
	if (!g_ready || !process_id || g_pd.process_id != process_id || !g_pd.cr3)
		return false;

	if (g_pd.base_address &&
	    probe_image_header(reinterpret_cast<std::uint64_t>(g_pd.base_address)))
		return true;

	if (g_pd.peb) {
		std::uint8_t probe = 0;
		return NT_SUCCESS(read_memory(reinterpret_cast<std::uint64_t>(g_pd.peb), &probe, sizeof(probe)));
	}

	return false;
}

bool attach(std::uint32_t process_id) {
	g_pd = {};
	g_ready = false;
	g_pd.process_id = process_id;
	c_packet pkt(e_syscall::query_process_data, &g_pd, sizeof(g_pd));
	const NTSTATUS st = issue_syscall(&pkt);
	if (!NT_SUCCESS(st) || g_pd.cr3 == 0 || !g_pd.peb)
		return false;
	g_ready = true;
	return true;
}

void detach() {
	g_ready = false;
	g_pd = {};
}

bool ready() {
	return g_ready;
}

const query_process_data_packet& attached_process() {
	return g_pd;
}

} 
