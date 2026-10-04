#pragma once
/**
 * drayvir/r69-driver ile ayni kullanici-mod paketleri (communication_types.hpp ile senkron).
 * Surucu HAL hook'ta trap_frame->Rdx uzerinden c_packet* bekler; burada NtQueryAuxiliaryCounterFrequency
 * yanlis imzayla (dummy ULONGLONG* + c_packet*) cagrilarak MSVC'nin RDX=sanal ikinci arg kurali kullanilir.
 */
#include <Windows.h>
#include <winternl.h>
#include <cstdint>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

/** drayvir communication_types.hpp ile ayni (alt tur belirtilmez — MSVC ile surucu sinifi es hizalar). */
enum class e_syscall {
	null = 0,
	read_process_memory,
	write_process_memory,
	query_process_data
};

class c_packet {
public:
	c_packet(e_syscall syscall, void* buffer, std::uint64_t size)
		: m_buffer(buffer)
		, m_size(size)
		, m_syscall(syscall) {}

	template <typename T>
	T* get() const {
		if (!m_buffer || !m_size)
			return nullptr;
		if (sizeof(T) != m_size)
			return nullptr;
		return reinterpret_cast<T*>(m_buffer);
	}

	const e_syscall get_syscall() const { return m_syscall; }

private:
	void* m_buffer = nullptr;
	std::uint64_t m_size = 0;
	e_syscall m_syscall = e_syscall::null;
};

struct copy_process_memory_packet {
	std::uint32_t process_id = 0;
	std::uint64_t source = 0;
	void* dest = nullptr;
	std::uint64_t size = 0;
};

struct query_process_data_packet {
	std::uint32_t process_id = 0;
	void* peb = nullptr;
	void* base_address = nullptr;
	std::uint64_t cr3 = 0;
};
