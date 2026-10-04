#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

/** Catalyst tarzi pattern / PE section / RIP — kernel driver okuma ile (g_GameMem). */
namespace cat_mem {

bool read(uintptr_t address, void* buffer, std::size_t size);

template <typename T>
inline T readv(uintptr_t address) {
	T value{};
	read(address, &value, sizeof(T));
	return value;
}

uintptr_t resolve_rip(uintptr_t address, std::int32_t offset = 3, std::int32_t length = 7);

std::size_t module_image_size(uintptr_t module_base);

uintptr_t client_module();
uintptr_t vphysics_module();

uintptr_t find_pattern(uintptr_t module_base, std::string_view pattern);
/** section_filter 0: tum image; degilse PE section ozellik maskesi (or. IMAGE_SCN_MEM_EXECUTE). */
std::vector<uintptr_t> find_pattern_all(uintptr_t module_base, std::string_view pattern,
	std::size_t max_hits = 512, std::uint32_t section_filter = 0);
uintptr_t find_vtable(uintptr_t module_base, std::string_view class_name);
/** RTTI vtable adresinden modul icinde ornek pointer (Catalyst memory::find_vtable_instance). */
uintptr_t find_vtable_instance(uintptr_t module_base, std::string_view class_name);
uintptr_t find_qword_in_sections(uintptr_t module_base, uintptr_t value, std::uint32_t section_filter);

/** Modul image boyunca value ile eslesen QWORD adresleri (max isabet). */
std::vector<uintptr_t> find_qword_all_in_image(uintptr_t module_base, std::uintptr_t value, std::size_t max_hits = 512);

/** Ilk bayt eslesmesi (ASCII / sabit imza); wildcard yok. 0 = bulunamadi. */
uintptr_t find_subsequence(uintptr_t module_base, const std::uint8_t* needle, std::size_t needle_len);

} // namespace cat_mem
