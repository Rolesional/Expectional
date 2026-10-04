#pragma once

#include "globals.hpp"
#include "../Driver/driver.hpp"
#include "offsets_runtime.hpp"

namespace ex_entity {

inline uintptr_t ResolveEntityFromListSlot(int slot) {
	if (!client || slot <= 0 || slot >= 0x7FFF)
		return 0;
	const uintptr_t entity_list = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwEntityList));
	if (!entity_list)
		return 0;
	const uintptr_t stride = static_cast<uintptr_t>(offsets::entity_controller_stride ? offsets::entity_controller_stride : 112u);
	const uintptr_t list_entry = g_GameMem.readv<uintptr_t>(
	    entity_list + 8ull * (static_cast<uintptr_t>(slot & 0x7FFF) >> 9) + 16);
	if (!list_entry)
		return 0;
	return g_GameMem.readv<uintptr_t>(list_entry + stride * (slot & 0x1FF));
}

inline uintptr_t ResolveHandle(uint32_t handle) {
	if (!handle || handle == 0xFFFFFFFFu || !client)
		return 0;
	const unsigned idx = handle & 0x7FFFu;
	if (!idx || idx == 0x7FFFu)
		return 0;
	const uintptr_t entity_list = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwEntityList));
	if (!entity_list)
		return 0;
	const uintptr_t stride = static_cast<uintptr_t>(offsets::entity_controller_stride ? offsets::entity_controller_stride : 112u);
	const uintptr_t list_entry = g_GameMem.readv<uintptr_t>(entity_list + 8ull * (static_cast<uintptr_t>(idx) >> 9) + 16);
	if (!list_entry)
		return 0;
	return g_GameMem.readv<uintptr_t>(list_entry + stride * (handle & 0x1FFu));
}

inline uintptr_t ResolvePawnFromCrosshairIndexDirect(int entIndex) {
	const uint32_t slot = static_cast<uint32_t>(entIndex) & 0x7FFFu;
	if (!slot || slot == 0x7FFFu)
		return 0;
	return ResolveHandle(slot);
}

inline uintptr_t ResolvePlayerPawnFromCrosshairIndex(int entIndex) {
	const int slot = entIndex & 0x7FFF;
	if (slot <= 0 || slot >= 0x7FFF)
		return 0;
	const uintptr_t ent = ResolveEntityFromListSlot(slot);
	if (ent && offsets::dwPlayerPawn) {
		const std::uint32_t hPawn =
		    g_GameMem.readv<std::uint32_t>(ent + static_cast<uintptr_t>(offsets::dwPlayerPawn));
		const uintptr_t pawn = ResolveHandle(hPawn);
		if (pawn >= 0x10000ull)
			return pawn;
	}
	if (ent && offsets::m_iHealth) {
		const int hp = g_GameMem.readv<int>(ent + static_cast<uintptr_t>(offsets::m_iHealth));
		if (hp > 0 && hp <= 100)
			return ent;
	}
	return ResolveHandle(static_cast<uint32_t>(slot));
}

} 
