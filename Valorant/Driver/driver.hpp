#pragma once

#include <Windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

bool km_device_read(uintptr_t src, void* dst, size_t sz);

struct KmReadDriver {
	uintptr_t cached_client = 0;
	uintptr_t cached_engine = 0;
	DWORD attached_pid = 0;

	bool initdriver(int processid);
	void shutdown();

	const char* last_init_error() const noexcept { return last_init_error_.c_str(); }

	template <typename T>
	T readv(uintptr_t src, size_t = sizeof(T)) {
		T out{};
		(void)km_device_read(src, &out, sizeof(T));
		return out;
	}

	uintptr_t client_address();
	uintptr_t engine_address();
	uintptr_t module_address(const wchar_t* module_name);

	std::string ReadString(uintptr_t addr, size_t maxLen = 64);

private:
	std::string last_init_error_;
};

extern KmReadDriver g_GameMem;
