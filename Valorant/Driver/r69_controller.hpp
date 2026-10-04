#pragma once

#include <Windows.h>
#include <winternl.h>
#include <cstdint>

#include "r69_syscall.hpp"

namespace expectional_r69 {

NTSTATUS read_memory(std::uint64_t src_va, void* dest, std::uint64_t size);

bool attach(std::uint32_t process_id);

void detach();

bool ready();

enum class module_resolve_error {
	none = 0,
	not_ready,
	no_peb,
	peb_unreadable,
	no_ldr,
	ldr_unreadable,
	list_unreadable,
	not_found,
};

struct module_resolve_result {
	std::uint64_t base = 0;
	std::uint32_t size_of_image = 0;
	module_resolve_error error = module_resolve_error::not_ready;
	int modules_scanned = 0;
};

bool probe_image_header(std::uint64_t image_base);

module_resolve_result resolve_module_base(const wchar_t* module_name);

std::uint32_t find_process_id_by_image_name_light(const wchar_t* image_name);

std::uint32_t find_process_id_by_image_name(const wchar_t* image_name, const wchar_t* verify_module = nullptr);

std::uint64_t module_base_by_name(const wchar_t* module_name);

struct engine_probe_params {
	std::uint64_t network_game_client_rva = 0;
	std::uint64_t build_number_rva = 0;
	std::uint64_t window_width_rva = 0;
	std::uint64_t window_height_rva = 0;
	std::uint64_t skip_module_base = 0;
};

std::uint64_t find_engine_module_by_offset(const engine_probe_params& params);

struct client_probe_params {
	std::uint64_t entity_list_rva = 0;
	std::uint64_t view_matrix_rva = 0;
	std::uint64_t local_player_pawn_rva = 0;
	std::uint64_t highest_entity_index_off = 0;
	std::uint64_t skip_module_base = 0;
};

std::uint64_t find_client_module_by_offset(const client_probe_params& params);

bool validate_client_module(std::uint64_t base, const client_probe_params& params, int* out_highest_entity = nullptr);
bool validate_engine_module(std::uint64_t base, const engine_probe_params& params);

bool is_process_alive(std::uint32_t process_id);

const query_process_data_packet& attached_process();

} 
