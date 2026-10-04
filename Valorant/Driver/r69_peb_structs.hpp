#pragma once

#include <Windows.h>
#include <cstdint>

struct ExpectionalPebMin {
	std::uint8_t reserved0[0x18];
	void* Ldr;
};

struct ExpectionalPebLdrData {
	std::uint32_t Length;
	std::uint8_t Initialized;
	std::uint8_t pad0[3];
	void* SsHandle;
	LIST_ENTRY InLoadOrderModuleList;
	LIST_ENTRY InMemoryOrderModuleList;
	LIST_ENTRY InInitializationOrderModuleList;
};

struct ExpectionalLdrEntry {
	LIST_ENTRY InLoadOrderLinks;
	LIST_ENTRY InMemoryOrderLinks;
	LIST_ENTRY InInitializationOrderLinks;
	void* DllBase;
	void* EntryPoint;
	std::uint32_t SizeOfImage;
	std::uint32_t pad0;
	UNICODE_STRING FullDllName;
	UNICODE_STRING BaseDllName;
};
