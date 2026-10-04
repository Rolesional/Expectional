#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include <string>

namespace expectional_r69_bridge {

bool init_for_process(DWORD pid, std::string* out_error = nullptr);

bool device_read(uintptr_t src, void* dst, size_t sz);

void shutdown_r69();

DWORD find_cs2_process_id_light();

DWORD find_cs2_process_id();

} 
