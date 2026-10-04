#include "cat_mem_scan.hpp"
#include "../Driver/driver.hpp"
#include "globals.hpp"

#include <Windows.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <string>
#include "Protection/vxlang_per_tu.hpp"

namespace cat_mem {

bool read(uintptr_t address, void* buffer, std::size_t size) {
	if (!buffer || size == 0)
		return false;
	
	auto* p = static_cast<std::uint8_t*>(buffer);
	std::uintptr_t addr = address;
	std::size_t left = size;
	while (left > 0) {
		const std::size_t chunk = (std::min)(static_cast<std::size_t>(0x10000u), left);
		if (!km_device_read(addr, p, chunk))
			return false;
		addr += chunk;
		p += chunk;
		left -= chunk;
	}
	return true;
}

static bool read_retry(uintptr_t address, void* buffer, std::size_t size) noexcept
{
	for (int i = 0; i < 4; ++i) {
		if (read(address, buffer, size))
			return true;
	}
	return false;
}

uintptr_t resolve_rip(uintptr_t address, std::int32_t offset, std::int32_t length) {
	const std::int32_t rva = readv<std::int32_t>(address + static_cast<uintptr_t>(offset));
	return address + static_cast<uintptr_t>(length) + static_cast<uintptr_t>(rva);
}

static std::size_t module_image_size_from_pe(uintptr_t module_base) noexcept
{
	const IMAGE_DOS_HEADER dos = readv<IMAGE_DOS_HEADER>(module_base);
	if (dos.e_magic != IMAGE_DOS_SIGNATURE)
		return 0;
	if (dos.e_lfanew < static_cast<LONG>(sizeof(IMAGE_DOS_HEADER)) || dos.e_lfanew > 0x4000)
		return 0;
	const uintptr_t nth = module_base + static_cast<uintptr_t>(dos.e_lfanew);
	const IMAGE_NT_HEADERS nt = readv<IMAGE_NT_HEADERS>(nth);
	if (nt.Signature != IMAGE_NT_SIGNATURE)
		return 0;
	if (nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC)
		return 0;
	const std::uint32_t sz = nt.OptionalHeader.SizeOfImage;
	if (sz < 0x1000u || sz > 0x10000000u)
		return 0;
	return static_cast<std::size_t>(sz);
}

std::size_t module_image_size(uintptr_t module_base) {
	if (!module_base)
		return 0;
	return module_image_size_from_pe(module_base);
}

uintptr_t client_module() {
	return client ? client : g_GameMem.client_address();
}

uintptr_t vphysics_module() {
	static uintptr_t cached{};
	if (cached)
		return cached;
	cached = g_GameMem.module_address(L"vphysics2.dll");
	if (!cached)
		cached = g_GameMem.module_address(L"vphysics.dll");
	return cached;
}

namespace detail {

static constexpr std::size_t k_chunk_size = 0x10000;

struct scan_result {
	std::uintptr_t m_address{};
	bool m_found{};
};

struct section_info {
	std::uintptr_t m_base{};
	std::size_t m_size{};
	std::uint32_t m_characteristics{};
	char m_name[9]{};
};

struct pattern_byte {
	std::uint8_t m_value{};
	bool m_wildcard{};
};

template <typename F>
static scan_result scan_chunked(uintptr_t base, std::size_t size, std::size_t overlap, F&& matcher) {
	std::vector<std::uint8_t> buffer(k_chunk_size + overlap);

	for (std::size_t offset = 0; offset < size; offset += k_chunk_size) {
		const std::size_t read_size = (std::min)(k_chunk_size + overlap, size - offset);
		if (!read(base + offset, buffer.data(), read_size))
			continue;

		const std::size_t match_offset = matcher(buffer.data(), read_size);
		if (match_offset != static_cast<std::size_t>(-1))
			return { base + offset + match_offset, true };
	}

	return {};
}

static std::vector<section_info> get_sections(uintptr_t module_base) {
	std::vector<section_info> sections;

	const IMAGE_DOS_HEADER dos = readv<IMAGE_DOS_HEADER>(module_base);
	if (dos.e_magic != IMAGE_DOS_SIGNATURE)
		return sections;

	const IMAGE_NT_HEADERS nt = readv<IMAGE_NT_HEADERS>(module_base + static_cast<uintptr_t>(dos.e_lfanew));
	if (nt.Signature != IMAGE_NT_SIGNATURE)
		return sections;

	const uintptr_t first = module_base + static_cast<uintptr_t>(dos.e_lfanew)
		+ offsetof(IMAGE_NT_HEADERS, OptionalHeader) + nt.FileHeader.SizeOfOptionalHeader;

	sections.reserve(nt.FileHeader.NumberOfSections);

	for (std::uint16_t i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
		const IMAGE_SECTION_HEADER hdr =
			readv<IMAGE_SECTION_HEADER>(first + static_cast<uintptr_t>(i) * sizeof(IMAGE_SECTION_HEADER));

		auto& sec = sections.emplace_back();
		sec.m_base = module_base + hdr.VirtualAddress;
		sec.m_size = hdr.Misc.VirtualSize;
		sec.m_characteristics = hdr.Characteristics;
		std::memcpy(sec.m_name, hdr.Name, 8);
		sec.m_name[8] = '\0';
	}

	return sections;
}

static std::vector<pattern_byte> parse_pattern(std::string_view pattern) {
	std::vector<pattern_byte> result;
	result.reserve(64);

	auto hex = [](char c) -> int {
		if (c >= '0' && c <= '9')
			return c - '0';
		if (c >= 'a' && c <= 'f')
			return c - 'a' + 10;
		if (c >= 'A' && c <= 'F')
			return c - 'A' + 10;
		return -1;
	};

	for (std::size_t i = 0; i < pattern.size();) {
		if (pattern[i] == ' ' || pattern[i] == '\t') {
			++i;
			continue;
		}

		if (pattern[i] == '?') {
			result.push_back({ 0, true });
			i += (i + 1 < pattern.size() && pattern[i + 1] == '?') ? 2 : 1;
			continue;
		}

		const int high = hex(pattern[i++]);
		if (high < 0)
			continue;

		const int low = (i < pattern.size()) ? hex(pattern[i]) : -1;
		if (low >= 0) {
			result.push_back({ static_cast<std::uint8_t>((high << 4) | low), false });
			++i;
		} else {
			result.push_back({ static_cast<std::uint8_t>(high), false });
		}
	}

	return result;
}

} 

uintptr_t find_pattern(uintptr_t module_base, std::string_view pattern) {
	if (!module_base || pattern.empty())
		return 0;

	const auto parsed = detail::parse_pattern(pattern);
	if (parsed.empty())
		return 0;

	const auto module_size = module_image_size(module_base);
	if (!module_size)
		return 0;

	const auto result = detail::scan_chunked(module_base, module_size, parsed.size(),
		[&](const std::uint8_t* data, std::size_t size) -> std::size_t {
			for (std::size_t i = 0; i + parsed.size() <= size; ++i) {
				bool match = true;
				for (std::size_t j = 0; j < parsed.size(); ++j) {
					if (!parsed[j].m_wildcard && data[i + j] != parsed[j].m_value) {
						match = false;
						break;
					}
				}
				if (match)
					return i;
			}
			return static_cast<std::size_t>(-1);
		});

	return result.m_found ? result.m_address : 0;
}

std::vector<uintptr_t> find_pattern_all(uintptr_t module_base, std::string_view pattern,
	std::size_t max_hits, std::uint32_t section_filter) {
	std::vector<uintptr_t> hits;
	if (!module_base || pattern.empty() || max_hits == 0)
		return hits;

	const auto parsed = detail::parse_pattern(pattern);
	if (parsed.empty())
		return hits;

	const std::size_t overlap = parsed.size();
	constexpr std::size_t k_chunk_local = 0x10000;
	std::vector<std::uint8_t> buffer(k_chunk_local + overlap);

	auto push_matches_in_buffer = [&](const std::uint8_t* data, std::size_t size, std::uintptr_t chunk_va) {
		for (std::size_t i = 0; i + parsed.size() <= size; ++i) {
			bool match = true;
			for (std::size_t j = 0; j < parsed.size(); ++j) {
				if (!parsed[j].m_wildcard && data[i + j] != parsed[j].m_value) {
					match = false;
					break;
				}
			}
			if (match) {
				hits.push_back(chunk_va + i);
				if (hits.size() >= max_hits)
					return true;
			}
		}
		return false;
	};

	auto scan_region = [&](std::uintptr_t base, std::size_t region_size) {
		for (std::size_t offset = 0; offset < region_size; offset += k_chunk_local) {
			const std::size_t read_size = (std::min)(k_chunk_local + overlap, region_size - offset);
			if (!read_retry(base + offset, buffer.data(), read_size))
				continue;
			if (push_matches_in_buffer(buffer.data(), read_size, base + offset))
				return;
		}
	};

	if (section_filter == 0) {
		const std::size_t module_size = module_image_size(module_base);
		if (!module_size)
			return hits;
		scan_region(module_base, module_size);
	} else {
		for (const auto& sec : detail::get_sections(module_base)) {
			if (!(sec.m_characteristics & section_filter) || sec.m_size < parsed.size())
				continue;
			scan_region(sec.m_base, sec.m_size);
			if (hits.size() >= max_hits)
				break;
		}
	}

	return hits;
}

uintptr_t find_qword_in_sections(uintptr_t module_base, uintptr_t value, std::uint32_t section_filter) {
	for (const auto& sec : detail::get_sections(module_base)) {
		if (!(sec.m_characteristics & section_filter) || sec.m_size < 8)
			continue;

		const auto result = detail::scan_chunked(sec.m_base, sec.m_size, 8,
			[&](const std::uint8_t* data, std::size_t size) -> std::size_t {
				for (std::size_t i = 0; i + 8 <= size; i += 8) {
					if (*reinterpret_cast<const std::uintptr_t*>(data + i) == value)
						return i;
				}
				return static_cast<std::size_t>(-1);
			});

		if (result.m_found)
			return result.m_address;
	}

	return 0;
}

uintptr_t find_subsequence(uintptr_t module_base, const std::uint8_t* needle, std::size_t needle_len) {
	if (!module_base || !needle || needle_len == 0 || needle_len > 512)
		return 0;
	const std::size_t mod_sz = module_image_size(module_base);
	if (mod_sz < needle_len)
		return 0;
	constexpr std::size_t k_chunk = 0x10000;
	std::vector<std::uint8_t> buf(k_chunk + 512);
	for (std::size_t off = 0; off + needle_len <= mod_sz;) {
		const std::size_t read_n = (std::min)(k_chunk + needle_len - 1, mod_sz - off);
		if (!read_retry(module_base + off, buf.data(), read_n)) {
			if (read_n < k_chunk + needle_len - 1)
				break;
			off += k_chunk;
			continue;
		}
		const std::size_t limit = read_n - needle_len + 1;
		for (std::size_t i = 0; i < limit; ++i) {
			if (std::memcmp(buf.data() + i, needle, needle_len) == 0)
				return module_base + off + i;
		}
		if (read_n < k_chunk + needle_len - 1)
			break;
		off += k_chunk;
	}
	return 0;
}

std::vector<uintptr_t> find_qword_all_in_image(uintptr_t module_base, std::uintptr_t value, std::size_t max_hits) {
	std::vector<uintptr_t> out;
	if (!module_base || max_hits == 0)
		return out;
	const std::size_t mod_sz = module_image_size(module_base);
	if (mod_sz < 8)
		return out;
	constexpr std::size_t k_chunk = 0x10000;
	std::vector<std::uint8_t> buf(k_chunk);
	for (std::size_t off = 0; off + 8 <= mod_sz && out.size() < max_hits; off += k_chunk) {
		const std::size_t read_n = (std::min)(k_chunk, mod_sz - off);
		if (!read_retry(module_base + off, buf.data(), read_n))
			continue;
		const std::size_t limit = (read_n / 8u) * 8u;
		for (std::size_t i = 0; i + 8 <= limit && out.size() < max_hits; i += 8) {
			const std::uintptr_t q = *reinterpret_cast<const std::uintptr_t*>(buf.data() + i);
			if (q == value)
				out.push_back(module_base + off + i);
		}
	}
	return out;
}

uintptr_t find_vtable(uintptr_t module_base, std::string_view class_name) {
	const std::string descriptor_name = std::string(".?AV") + std::string(class_name) + "@@";
	std::uintptr_t type_descriptor = 0;

	for (const auto& sec : detail::get_sections(module_base)) {
		constexpr auto required = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
		if ((sec.m_characteristics & required) != required || sec.m_size <= descriptor_name.size())
			continue;

		const auto result = detail::scan_chunked(sec.m_base, sec.m_size, descriptor_name.size(),
			[&](const std::uint8_t* data, std::size_t size) -> std::size_t {
				for (std::size_t i = 0; i + descriptor_name.size() < size; ++i) {
					if (std::memcmp(data + i, descriptor_name.data(), descriptor_name.size() + 1) == 0)
						return i;
				}
				return static_cast<std::size_t>(-1);
			});

		if (result.m_found) {
			type_descriptor = result.m_address - 0x10;
			break;
		}
	}

	if (!type_descriptor)
		return 0;

	const auto descriptor_rva = static_cast<std::uint32_t>(type_descriptor - module_base);
	std::uintptr_t col_address = 0;

	for (const auto& sec : detail::get_sections(module_base)) {
		if (std::string_view{ sec.m_name }.find(".rdata") == std::string_view::npos || sec.m_size < 0x30)
			continue;

		const auto result = detail::scan_chunked(sec.m_base, sec.m_size, 0x30,
			[&](const std::uint8_t* data, std::size_t size) -> std::size_t {
				for (std::size_t i = 0; i + 0x30 <= size; i += 8) {
					if (reinterpret_cast<const std::uint32_t*>(data + i)[3] == descriptor_rva)
						return i;
				}
				return static_cast<std::size_t>(-1);
			});

		if (result.m_found) {
			col_address = result.m_address;
			break;
		}
	}

	if (!col_address)
		return 0;

	const auto col_ref = find_qword_in_sections(module_base, col_address, IMAGE_SCN_MEM_READ);
	return col_ref ? col_ref + 8 : 0;
}

uintptr_t find_vtable_instance(uintptr_t module_base, std::string_view class_name) {
	const auto vtable = find_vtable(module_base, class_name);
	if (!vtable)
		return 0;
	return find_qword_in_sections(module_base, vtable, IMAGE_SCN_MEM_READ | IMAGE_SCN_MEM_WRITE);
}

} 
