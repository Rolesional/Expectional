#pragma once

#include <Windows.h>
#include <cstddef>
#include <cstdint>

#include <string>

namespace expectional_r69_bridge {

/** kdmapper (sessiz) + r69 attach — eskisurum initdriver icinde kernel_ue5::init yerine. */
bool init_for_process(DWORD pid, std::string* out_error = nullptr);

bool device_read(uintptr_t src, void* dst, size_t sz);

void shutdown_r69();

/** cs2.exe PID via NtQuerySystemInformation only (no kernel driver). */
DWORD find_cs2_process_id_light();

/** Alias for find_cs2_process_id_light(). */
DWORD find_cs2_process_id();

} // namespace expectional_r69_bridge
