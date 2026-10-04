#include "cloud_radar.hpp"
#include "globals.hpp"

extern std::mutex g_PlayerListMutex;
#include "offsets_runtime.hpp"
#include "entity_handle.hpp"
#include "esp_extras.hpp"
#include "../Driver/driver.hpp"
#include "structs.hpp"

#include <Windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

#include "../../Includes/Imgui/imgui.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

constexpr DWORD kRadarProtocolVersion = 2;

inline int PublishIntervalMs() noexcept
{
	
	return Settings::misc::save_fps ? 33 : 16;
}

std::mutex g_status_mtx;
std::string g_status_line = "Cloud Radar: Disabled";
std::string g_session_view_url;
std::atomic<bool> g_thread_started{false};

std::wstring Utf8ToWide(const char* s) {
	if (!s) return {};
	int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
	if (n <= 0) return {};
	std::wstring w(static_cast<size_t>(n - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s, -1, &w[0], n);
	return w;
}

bool ParseWssUrl(const char* url, std::wstring& host, std::wstring& path, INTERNET_PORT& port) {
	std::string u = url ? url : "";
	while (!u.empty() && (unsigned char)u.back() <= ' ') u.pop_back();
	if (u.size() < 8) return false;
	const bool wss = (u.compare(0, 6, "wss://") == 0);
	const bool ws = (u.compare(0, 5, "ws://") == 0);
	if (!wss && !ws) return false;
	const size_t start = wss ? 6 : 5;
	const size_t slash = u.find('/', start);
	const std::string hostport = (slash == std::string::npos) ? u.substr(start) : u.substr(start, slash - start);
	path = (slash == std::string::npos) ? L"/" : Utf8ToWide(u.substr(slash).c_str());
	port = wss ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT;
	size_t colon = hostport.find(':');
	if (colon != std::string::npos) {
		port = static_cast<INTERNET_PORT>(atoi(hostport.c_str() + static_cast<int>(colon) + 1));
		host = Utf8ToWide(hostport.substr(0, colon).c_str());
	} else {
		host = Utf8ToWide(hostport.c_str());
	}
	return !host.empty();
}

void JsonEscape(const std::string& in, std::string& out) {
	out.clear();
	out.reserve(in.size() + 8);
	for (unsigned char c : in) {
		if (c == '\\') out += "\\\\";
		else if (c == '"') out += "\\\"";
		else if (c == '\n') out += "\\n";
		else if (c == '\r') out += "\\r";
		else if (c < 32) {
			char b[8];
			snprintf(b, sizeof b, "\\u%04x", c);
			out += b;
		} else
			out += static_cast<char>(c);
	}
}

static void NormalizeRadarWorldName(std::string& s) {
	while (!s.empty() && (unsigned char)s.back() <= ' ') s.pop_back();
	while (!s.empty() && (unsigned char)s.front() <= ' ') s.erase(0, 1);
	const size_t slash = s.find_last_of("\\/");
	if (slash != std::string::npos && slash + 1 < s.size())
		s.erase(0, slash + 1);
	if (s.size() > 4 && s.compare(s.size() - 4, 4, ".bsp") == 0)
		s.resize(s.size() - 4);
}

static std::string ReadWorldNameForRadarJson() {
	const uintptr_t eng = g_GameMem.engine_address();
	if (!eng || !offsets::dwNetworkGameClient)
		return "<empty>";
	const uintptr_t pNgc = g_GameMem.readv<uintptr_t>(eng + static_cast<uintptr_t>(offsets::dwNetworkGameClient));
	if (!pNgc || pNgc < 0x10000ULL)
		return "<empty>";
	constexpr uintptr_t kMapName = 0x218;
	constexpr uintptr_t kMapPath = 0x210;
	uintptr_t pStr = g_GameMem.readv<uintptr_t>(pNgc + kMapName);
	std::string raw = pStr ? g_GameMem.ReadString(pStr, 160) : std::string{};
	if (raw.empty()) {
		pStr = g_GameMem.readv<uintptr_t>(pNgc + kMapPath);
		raw = pStr ? g_GameMem.ReadString(pStr, 260) : std::string{};
	}
	NormalizeRadarWorldName(raw);
	if (raw.empty())
		return "<empty>";
	return raw;
}

uint16_t ReadWeaponDefIndex(uintptr_t pawn) {
	return ex_esp::ReadWeaponDefIndex(pawn);
}

static UE4Structs::Vector3 ReadEntityWorldPositionForRadar(uintptr_t entity) {
	if (!entity)
		return {};
	if (offsets::m_pGameSceneNode && offsets::m_vecAbsOrigin) {
		const uintptr_t sn = g_GameMem.readv<uintptr_t>(entity + static_cast<uintptr_t>(offsets::m_pGameSceneNode));
		if (sn)
			return g_GameMem.readv<UE4Structs::Vector3>(sn + static_cast<uintptr_t>(offsets::m_vecAbsOrigin));
	}
	if (offsets::m_vecOrigin)
		return g_GameMem.readv<UE4Structs::Vector3>(entity + static_cast<uintptr_t>(offsets::m_vecOrigin));
	return {};
}

static float ReadPawnEyeYawDegrees(uintptr_t pawn) {
	if (!pawn || !offsets::m_angEyeAngles)
		return 0.f;
	return g_GameMem.readv<float>(pawn + static_cast<uintptr_t>(offsets::m_angEyeAngles) + 4);
}

static float ReadRadarCurTime() {
	if (!client || !offsets::dwGlobalVars)
		return 0.f;
	const uintptr_t gv = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwGlobalVars));
	if (!gv)
		return 0.f;
	return g_GameMem.readv<float>(gv + 0x30);
}

static bool PlantedC4FieldsSane(uintptr_t bomb) {
	if (!bomb || bomb < 0x10000ull || !offsets::c4_m_flC4Blow)
		return false;
	const float blow = g_GameMem.readv<float>(bomb + static_cast<uintptr_t>(offsets::c4_m_flC4Blow));
	return blow > 0.5f && blow < 100000.f;
}

static uintptr_t ResolvePlantedC4Entity() {
	if (!client || !offsets::dwPlantedC4)
		return 0;
	const uintptr_t plantedAddr = client + static_cast<uintptr_t>(offsets::dwPlantedC4);
	if (!g_GameMem.readv<bool>(plantedAddr - 8))
		return 0;
	const uintptr_t p = g_GameMem.readv<uintptr_t>(plantedAddr);
	if (PlantedC4FieldsSane(p))
		return p;
	if (!p || p < 0x10000ull)
		return 0;
	const uintptr_t q = g_GameMem.readv<uintptr_t>(p);
	if (PlantedC4FieldsSane(q))
		return q;
	return 0;
}

static void AppendC4EntityJson(std::string& out, bool& first, uint32_t entityId,
	const UE4Structs::Vector3& pos, bool owned, uint32_t ownerId) {
	char seg[220];
	if (owned) {
		snprintf(seg, sizeof seg,
			"%s{\"entityId\":%u,\"position\":[%f,%f,%f],\"ownerEntityId\":%u}",
			first ? "" : ",", entityId, pos.x, pos.y, pos.z, ownerId);
	} else {
		snprintf(seg, sizeof seg,
			"%s{\"entityId\":%u,\"position\":[%f,%f,%f],\"ownerEntityId\":null}",
			first ? "" : ",", entityId, pos.x, pos.y, pos.z);
	}
	out += seg;
	first = false;
}

struct DroppedC4Cache {
	bool valid = false;
	UE4Structs::Vector3 pos{};
	uint32_t entityId = 0;
	ULONGLONG seenTick = 0;
	int cursor = 1;
};

static DroppedC4Cache g_droppedC4;

static void NoteDroppedC4(int entityIndex, const UE4Structs::Vector3& pos) {
	g_droppedC4.valid = true;
	g_droppedC4.pos = pos;
	g_droppedC4.entityId = static_cast<uint32_t>(entityIndex);
	g_droppedC4.seenTick = GetTickCount64();
}

static void TickDroppedC4Scan() {
	if (!client || !offsets::dwEntityList)
		return;
	const uintptr_t entity_list = g_GameMem.readv<uintptr_t>(client + offsets::dwEntityList);
	if (!entity_list)
		return;
	constexpr int kScanHi = 1024;
	constexpr int kStep = 96;
	constexpr uintptr_t kEntStride = 112u;
	const int start = g_droppedC4.cursor;
	for (int n = 0; n < kStep; ++n) {
		int i = start + n;
		if (i >= kScanHi)
			i = 1 + (i - kScanHi);
		if (i < 1)
			i = 1;
		const uintptr_t list_entry = g_GameMem.readv<uintptr_t>(
			entity_list + 8ull * (static_cast<uintptr_t>(i & 0x7FFF) >> 9) + 16);
		if (!list_entry)
			continue;
		const uintptr_t ent = g_GameMem.readv<uintptr_t>(list_entry + kEntStride * (i & 0x1FF));
		if (!ent || ent < 0x10000ull)
			continue;
		if (ex_esp::ReadWeaponDefIndexFromWeaponEntity(ent) != 49)
			continue;
		const uintptr_t owner = ex_esp::ResolveOwnerEntityPtr(ent);
		if (owner)
			continue;
		const UE4Structs::Vector3 pos = ReadEntityWorldPositionForRadar(ent);
		if (pos.length2d() < 8.f && std::fabs(pos.z) < 8.f)
			continue;
		NoteDroppedC4(i, pos);
		break;
	}
	int next = start + kStep;
	if (next >= kScanHi)
		next = 1;
	g_droppedC4.cursor = next;
	if (g_droppedC4.valid && GetTickCount64() - g_droppedC4.seenTick > 1600)
		g_droppedC4.valid = false;
}

static std::string BuildC4EntitiesJson(const std::vector<UE4Structs::CS2Entity>* snapshot, bool plantedLive) {
	std::string out = "[";
	bool first = true;
	bool anyCarrier = false;
	if (!plantedLive && snapshot) {
		for (const UE4Structs::CS2Entity& e : *snapshot) {
			if (!e.Actor || (!e.has_c4 && e.weapon_def_index != 49))
				continue;
			if (!e.has_world_origin)
				continue;
			const UE4Structs::Vector3 pos(e.world_x, e.world_y, e.world_z);
			if (pos.length2d() < 8.f && std::fabs(pos.z) < 8.f)
				continue;
			const uint32_t pawnId = e.pawn_handle_low ? e.pawn_handle_low : static_cast<uint32_t>((std::max)(1, e.entity_index));
			AppendC4EntityJson(out, first, pawnId, pos, true, pawnId);
			anyCarrier = true;
		}
	}
	if (!plantedLive && !anyCarrier) {
		const ULONGLONG nowDrop = GetTickCount64();
		if (!g_droppedC4.valid || nowDrop - g_droppedC4.seenTick > 350)
			TickDroppedC4Scan();
		if (g_droppedC4.valid && nowDrop - g_droppedC4.seenTick > 1600)
			g_droppedC4.valid = false;
		if (g_droppedC4.valid)
			AppendC4EntityJson(out, first, g_droppedC4.entityId ? g_droppedC4.entityId : 1u, g_droppedC4.pos, false, 0);
	} else {
		g_droppedC4.valid = false;
	}
	out += "]";
	return out;
}

static const char* DefuserNameFromSnapshot(const std::vector<UE4Structs::CS2Entity>* snapshot, uintptr_t defPawn) {
	if (!snapshot || !defPawn)
		return nullptr;
	for (const UE4Structs::CS2Entity& e : *snapshot) {
		if (e.Actor == defPawn && !e.name.empty())
			return e.name.c_str();
	}
	return nullptr;
}

std::string BuildPlantedC4JsonOrEmpty(const std::vector<UE4Structs::CS2Entity>* snapshot) {
	const uintptr_t bomb = ResolvePlantedC4Entity();
	if (!bomb)
		return {};
	if (offsets::c4_m_bBombDefused) {
		if (g_GameMem.readv<bool>(bomb + static_cast<uintptr_t>(offsets::c4_m_bBombDefused)))
			return {};
	}
	const UE4Structs::Vector3 pos = ReadEntityWorldPositionForRadar(bomb);
	int site = 0;
	if (offsets::c4_m_nBombSite)
		site = g_GameMem.readv<int>(bomb + static_cast<uintptr_t>(offsets::c4_m_nBombSite));
	const float cur = ReadRadarCurTime();
	float timeDet = 0.f;
	float timeTotal = 40.f;
	if (offsets::c4_m_flC4Blow) {
		const float blow = g_GameMem.readv<float>(bomb + static_cast<uintptr_t>(offsets::c4_m_flC4Blow));
		const float timerLen = g_GameMem.readv<float>(bomb + static_cast<uintptr_t>(offsets::c4_m_flC4Blow) + 8);
		if (timerLen >= 10.f && timerLen <= 60.f)
			timeTotal = timerLen;
		if (blow > cur && blow - cur < 120.f)
			timeDet = blow - cur;
	}
	if (timeDet < 0.f)
		timeDet = 0.f;
	bool defusing = false;
	if (offsets::c4_m_bBeingDefused)
		defusing = g_GameMem.readv<bool>(bomb + static_cast<uintptr_t>(offsets::c4_m_bBeingDefused));
	float defRem = 0.f;
	float defTotal = 10.f;
	const char* defName = nullptr;
	if (defusing && offsets::c4_m_flDefuseCountDown && cur > 0.f) {
		const float defEnd = g_GameMem.readv<float>(bomb + static_cast<uintptr_t>(offsets::c4_m_flDefuseCountDown));
		defRem = defEnd - cur;
		const float defLen = g_GameMem.readv<float>(bomb + static_cast<uintptr_t>(offsets::c4_m_flDefuseCountDown) - 4);
		if (defLen >= 3.f && defLen <= 15.f)
			defTotal = defLen;
		else if (defRem > 5.5f)
			defTotal = 10.f;
		else
			defTotal = 5.f;
		if (offsets::c4_m_bBombDefused) {
			const uint32_t defH = g_GameMem.readv<uint32_t>(bomb + static_cast<uintptr_t>(offsets::c4_m_bBombDefused) + 4);
			const uintptr_t defPawn = ex_entity::ResolveHandle(defH);
			defName = DefuserNameFromSnapshot(snapshot, defPawn);
		}
	}
	if (defRem < 0.f)
		defRem = 0.f;
	const char* state = defusing ? "defusing" : "active";
	char buf[640];
	if (defusing) {
		std::string nameEsc;
		if (defName)
			JsonEscape(defName, nameEsc);
		snprintf(buf, sizeof buf,
			"\"plantedC4\":{\"position\":[%f,%f,%f],\"bombSite\":%d,\"state\":{\"state\":\"%s\",\"timeDetonation\":%f,\"timeTotal\":%f,"
			"\"defuser\":{\"timeRemaining\":%f,\"timeTotal\":%f,\"playerName\":%s%s%s}}}",
			pos.x, pos.y, pos.z, site & 0xFF, state, timeDet, timeTotal, defRem, defTotal,
			defName ? "\"" : "", defName ? nameEsc.c_str() : "null", defName ? "\"" : "");
	} else {
		snprintf(buf, sizeof buf,
			"\"plantedC4\":{\"position\":[%f,%f,%f],\"bombSite\":%d,\"state\":{\"state\":\"%s\",\"timeDetonation\":%f,\"timeTotal\":%f,\"defuser\":null}}",
			pos.x, pos.y, pos.z, site & 0xFF, state, timeDet, timeTotal);
	}
	return std::string(buf);
}

static void AppendLocalPlayerRadarJson(std::string& j, bool& first, const std::vector<UE4Structs::CS2Entity>& snapshot) {
	if (!global_pawn || global_pawn < 0x10000ull || !offsets::m_iHealth)
		return;
	for (const UE4Structs::CS2Entity& e : snapshot) {
		if (e.Actor == global_pawn)
			return;
	}
	const int hp = g_GameMem.readv<int>(global_pawn + offsets::m_iHealth);
	if (hp <= 0 || hp > 500)
		return;
	const UE4Structs::Vector3 ori = ReadEntityWorldPositionForRadar(global_pawn);
	if (ori.length2d() < 8.f && std::fabs(ori.z) < 8.f)
		return;

	int ctrlIdx = g_localControllerEntityIndex.load(std::memory_order_relaxed);
	uintptr_t controller = 0;
	if (client && offsets::dwLocalPlayerController)
		controller = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwLocalPlayerController));
	if (ctrlIdx <= 0 && controller && client && offsets::dwEntityList) {
		const uintptr_t entity_list = g_GameMem.readv<uintptr_t>(client + offsets::dwEntityList);
		constexpr uintptr_t kEntStride = 112u;
		if (entity_list) {
			for (int i = 1; i < 64; ++i) {
				const uintptr_t list_entry = g_GameMem.readv<uintptr_t>(
					entity_list + 8ull * (static_cast<uintptr_t>(i & 0x7FFF) >> 9) + 16);
				if (!list_entry)
					continue;
				const uintptr_t ctrl = g_GameMem.readv<uintptr_t>(list_entry + kEntStride * (i & 0x1FF));
				if (ctrl == controller) {
					ctrlIdx = i;
					break;
				}
			}
		}
	}
	if (ctrlIdx <= 0)
		ctrlIdx = 1;

	std::string rawName = "You";
	if (controller && offsets::dwSanitizedName) {
		rawName = g_GameMem.ReadString(controller + static_cast<uintptr_t>(offsets::dwSanitizedName), 64);
		const size_t z = rawName.find('\0');
		if (z != std::string::npos)
			rawName.resize(z);
		while (!rawName.empty() && (unsigned char)rawName.back() <= ' ')
			rawName.pop_back();
		if (rawName.empty())
			rawName = "You";
	}
	std::string escJson;
	JsonEscape(rawName, escJson);

	const uint8_t team = offsets::m_iTeamNum
		? static_cast<uint8_t>(g_GameMem.readv<int>(global_pawn + offsets::m_iTeamNum) & 0xFF)
		: 0;
	int pawnHandle = g_localPawnHandleIndex.load(std::memory_order_relaxed);
	if (pawnHandle <= 0 && controller && offsets::dwPlayerPawn) {
		const std::uint32_t h = g_GameMem.readv<std::uint32_t>(controller + offsets::dwPlayerPawn);
		pawnHandle = static_cast<int>(h & 0x7FFFu);
	}
	const uint32_t pawnId = pawnHandle > 0 ? static_cast<uint32_t>(pawnHandle) : 1u;
	const uint16_t wpn = ReadWeaponDefIndex(global_pawn);
	const float yawDeg = ReadPawnEyeYawDegrees(global_pawn);
	const bool kit = offsets::m_ArmorValue && g_GameMem.readv<int>(global_pawn + offsets::m_ArmorValue) > 0;

	char seg[768];
	snprintf(seg, sizeof seg,
		"%s{\"controllerEntityId\":%d,\"pawnEntityId\":%u,\"teamId\":%u,\"playerName\":\"%s\","
		"\"playerHealth\":%d,\"playerHasDefuser\":%s,\"playerFlashtime\":0.0,\"weapon\":%u,"
		"\"position\":[%f,%f,%f],\"rotation\":%f}",
		first ? "" : ",", ctrlIdx, pawnId, team, escJson.c_str(), hp, kit ? "true" : "false", wpn,
		ori.x, ori.y, ori.z, yawDeg);
	j += seg;
	first = false;
}

static void BuildRadarStateJsonFromSnapshot(std::string& j, const std::vector<UE4Structs::CS2Entity>& snapshot) {
	std::string worldEsc;
	JsonEscape(ReadWorldNameForRadarJson(), worldEsc);
	const std::string hdr = std::string("{\"worldName\":\"") + worldEsc + "\",\"playerPawns\":[";
	j.clear();
	j.reserve(8192);
	j += hdr;

	bool first = true;
	for (const UE4Structs::CS2Entity& e : snapshot) {
		if (!e.Actor || e.health <= 0 || e.health > 500)
			continue;
		if (!e.has_world_origin)
			continue;
		const UE4Structs::Vector3 ori(e.world_x, e.world_y, e.world_z);
		if (ori.length2d() < 8.f && std::fabs(ori.z) < 8.f)
			continue;

		std::string esc = e.name.empty() ? "Unknown" : e.name;
		std::string escJson;
		JsonEscape(esc, escJson);
		const uint8_t team = static_cast<uint8_t>(e.team_num & 0xFF);
		const bool kit = e.armor > 0;
		const uint16_t wpn = e.weapon_def_index;
		const uint32_t pawnId = e.pawn_handle_low;
		const uint32_t ctrlId = static_cast<uint32_t>((std::max)(1, e.entity_index));
		const float yawDeg = e.eye_yaw_deg;

		char seg[768];
		snprintf(seg, sizeof seg,
			"%s{\"controllerEntityId\":%u,\"pawnEntityId\":%u,\"teamId\":%u,\"playerName\":\"%s\","
			"\"playerHealth\":%d,\"playerHasDefuser\":%s,\"playerFlashtime\":0.0,\"weapon\":%u,"
			"\"position\":[%f,%f,%f],\"rotation\":%f}",
			first ? "" : ",", ctrlId, pawnId, team, escJson.c_str(), e.health, kit ? "true" : "false", wpn,
			ori.x, ori.y, ori.z, yawDeg);
		j += seg;
		first = false;
	}
	AppendLocalPlayerRadarJson(j, first, snapshot);

	j += "],";
	const std::string planted = BuildPlantedC4JsonOrEmpty(&snapshot);
	if (planted.empty())
		j += "\"plantedC4\":null";
	else
		j += planted;
	j += ",\"c4Entities\":";
	j += BuildC4EntitiesJson(&snapshot, !planted.empty());
	j += ",\"localControllerEntityId\":";
	const int localIdx = g_localControllerEntityIndex.load(std::memory_order_relaxed);
	if (localIdx > 0) {
		char idbuf[32];
		snprintf(idbuf, sizeof idbuf, "%d", localIdx);
		j += idbuf;
	} else {
		j += "null";
	}
	j += "}";
}

void BuildRadarStateJson(std::string& j) {
	{
		std::vector<UE4Structs::CS2Entity> snapshot;
		{
			std::lock_guard<std::mutex> lk(g_PlayerListMutex);
			snapshot = UE4Structs::PlayerList;
		}
		if (!snapshot.empty()) {
			BuildRadarStateJsonFromSnapshot(j, snapshot);
			return;
		}
	}

	std::string worldEsc;
	JsonEscape(ReadWorldNameForRadarJson(), worldEsc);
	const std::string hdr = std::string("{\"worldName\":\"") + worldEsc + "\",\"playerPawns\":[";

	j.clear();
	j.reserve(8192);
	j += hdr;

	if (!client || !offsets::dwEntityList || !offsets::dwLocalPlayerPawn) {
		j += "],\"plantedC4\":null,\"c4Entities\":[],\"localControllerEntityId\":null}";
		return;
	}

	uintptr_t local_controller = 0;
	if (offsets::dwLocalPlayerController)
		local_controller = g_GameMem.readv<uintptr_t>(client + static_cast<uintptr_t>(offsets::dwLocalPlayerController));

	const uintptr_t entity_list = g_GameMem.readv<uintptr_t>(client + offsets::dwEntityList);
	if (!entity_list) {
		j += "],\"plantedC4\":null,\"c4Entities\":[],\"localControllerEntityId\":null}";
		return;
	}

	constexpr uintptr_t kEntStride = 112u;
	bool first = true;
	for (int i = 1; i < 64; ++i) {
		const uintptr_t list_entry = g_GameMem.readv<uintptr_t>(entity_list + 8ull * (static_cast<uintptr_t>(i & 0x7FFF) >> 9) + 16);
		if (!list_entry) continue;
		const uintptr_t player = g_GameMem.readv<uintptr_t>(list_entry + kEntStride * (i & 0x1FF));
		if (!player) continue;
		const std::uint32_t playerpawn = g_GameMem.readv<std::uint32_t>(player + offsets::dwPlayerPawn);
		const uintptr_t list_entry2 = g_GameMem.readv<uintptr_t>(entity_list + 0x8 * ((playerpawn & 0x7FFF) >> 9) + 16);
		if (!list_entry2) continue;
		const uintptr_t pawn = g_GameMem.readv<uintptr_t>(list_entry2 + kEntStride * (playerpawn & 0x1FF));
		if (!pawn) continue;
		const int hp = g_GameMem.readv<int>(pawn + offsets::m_iHealth);
		if (hp <= 0 || hp > 500) continue;
		if (offsets::m_bPawnIsAlive) {
			const int alive = g_GameMem.readv<int>(player + static_cast<uintptr_t>(offsets::m_bPawnIsAlive));
			if (alive != 1) continue;
		}
		if (offsets::m_pGameSceneNode && offsets::m_bDormant) {
			const uintptr_t sn = g_GameMem.readv<uintptr_t>(pawn + static_cast<uintptr_t>(offsets::m_pGameSceneNode));
			if (sn) {
				const uint8_t dorm = g_GameMem.readv<uint8_t>(sn + static_cast<uintptr_t>(offsets::m_bDormant));
				if (dorm) continue;
			}
		}
		const UE4Structs::Vector3 ori = ReadEntityWorldPositionForRadar(pawn);
		if (ori.length2d() < 8.f && std::fabs(ori.z) < 8.f) continue;

		std::string rawName;
		if (offsets::dwSanitizedName) {
			rawName = g_GameMem.ReadString(player + static_cast<uintptr_t>(offsets::dwSanitizedName), 64);
			const size_t z = rawName.find('\0');
			if (z != std::string::npos) rawName.resize(z);
			while (!rawName.empty() && (unsigned char)rawName.back() <= ' ') rawName.pop_back();
		}
		if (rawName.empty()) rawName = "Unknown";
		std::string esc;
		JsonEscape(rawName, esc);
		const uint8_t team = static_cast<uint8_t>(g_GameMem.readv<int>(pawn + offsets::m_iTeamNum) & 0xFF);
		const bool kit = offsets::m_ArmorValue ? (g_GameMem.readv<int>(pawn + offsets::m_ArmorValue) > 0) : false;
		const uint16_t wpn = ReadWeaponDefIndex(pawn);
		const uint32_t pawnId = playerpawn & 0x7FFFu;
		const uint32_t ctrlId = static_cast<uint32_t>(i);
		const float yawDeg = ReadPawnEyeYawDegrees(pawn);

		char seg[768];
		snprintf(seg, sizeof seg,
			"%s{\"controllerEntityId\":%u,\"pawnEntityId\":%u,\"teamId\":%u,\"playerName\":\"%s\","
			"\"playerHealth\":%d,\"playerHasDefuser\":%s,\"playerFlashtime\":0.0,\"weapon\":%u,"
			"\"position\":[%f,%f,%f],\"rotation\":%f}",
			first ? "" : ",", ctrlId, pawnId, team, esc.c_str(), hp, kit ? "true" : "false", wpn,
			ori.x, ori.y, ori.z, yawDeg);
		j += seg;
		first = false;
	}

	j += "],";
	const std::string planted = BuildPlantedC4JsonOrEmpty(nullptr);
	if (planted.empty())
		j += "\"plantedC4\":null";
	else
		j += planted;
	j += ",\"c4Entities\":";
	j += BuildC4EntitiesJson(nullptr, !planted.empty());
	j += ",\"localControllerEntityId\":";
	if (local_controller) {
		for (int i = 1; i < 64; ++i) {
			const uintptr_t list_entry = g_GameMem.readv<uintptr_t>(entity_list + 8ull * (static_cast<uintptr_t>(i & 0x7FFF) >> 9) + 16);
			if (!list_entry) continue;
			const uintptr_t ctrl = g_GameMem.readv<uintptr_t>(list_entry + kEntStride * (i & 0x1FF));
			if (ctrl == local_controller) {
				char b[32];
				snprintf(b, sizeof b, "%d", i);
				j += b;
				goto done_local;
			}
		}
	}
	j += "null";
done_local:
	j += "}";
}

bool WsSendText(HINTERNET ws, const std::string& utf8) {
	const DWORD r = WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
		(void*)utf8.data(), static_cast<DWORD>(utf8.size()));
	return r == ERROR_SUCCESS;
}

static bool WsDrainPendingNonBlocking(HINTERNET ws, std::string* outComplete = nullptr) {
	DWORD timeout = 0;
	(void)WinHttpSetOption(ws, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
	std::vector<char> buf(8192);
	for (;;) {
		WINHTTP_WEB_SOCKET_BUFFER_TYPE typ{};
		DWORD read = 0;
		const DWORD st = WinHttpWebSocketReceive(ws, buf.data(), static_cast<DWORD>(buf.size()), &read, &typ);
		if (st != ERROR_SUCCESS)
			return false;
		if (typ == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
			return false;
		if (typ == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || typ == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
			if (outComplete && read)
				outComplete->assign(buf.data(), read);
			return true;
		}
	}
}

bool WsRecvText(HINTERNET ws, std::string& out, DWORD timeoutMs) {
	(void)WinHttpSetOption(ws, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeoutMs, sizeof(timeoutMs));
	out.clear();
	std::vector<char> buf(65536);
	for (;;) {
		WINHTTP_WEB_SOCKET_BUFFER_TYPE typ{};
		DWORD read = 0;
		const DWORD st = WinHttpWebSocketReceive(ws, buf.data(), static_cast<DWORD>(buf.size()), &read, &typ);
		if (st != ERROR_SUCCESS) return false;
		if (read)
			out.append(buf.data(), read);
		if (typ == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || typ == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE)
			return true;
		if (typ == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE)
			return false;
	}
}

bool FindJsonStringValue(const std::string& json, const char* key, std::string& val) {
	const std::string pat = std::string("\"") + key + "\":\"";
	const size_t p = json.find(pat);
	if (p == std::string::npos) return false;
	size_t q = p + pat.size();
	size_t e = q;
	while (e < json.size() && json[e] != '"') {
		if (json[e] == '\\' && e + 1 < json.size()) {
			e += 2;
			continue;
		}
		++e;
	}
	if (e > q)
		val.assign(json, q, e - q);
	return !val.empty();
}

static std::string BuildCloudRadarViewerUrl(const std::string& sessionId) {
	std::string base = Settings::misc::cloudRadarViewerBase;
	while (!base.empty() && (unsigned char)base.back() <= ' ') base.pop_back();
	while (!base.empty() && base.back() == '/') base.pop_back();
	if (base.empty())
		return std::string("https://radar.valth.run/session/") + sessionId;
	return base + "?session=" + sessionId;
}

bool RunOneRadarSession(const char* publishUrl, std::string& errOut) {
	errOut.clear();
	std::wstring host, path;
	INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT;
	if (!ParseWssUrl(publishUrl, host, path, port)) {
		errOut = "URL (wss://...) parse";
		return false;
	}

	HINTERNET hSession = WinHttpOpen(L"Expectional-CloudRadar/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
		WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!hSession) {
		errOut = "WinHttpOpen";
		return false;
	}
	HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
	if (!hConnect) {
		errOut = "WinHttpConnect";
		WinHttpCloseHandle(hSession);
		return false;
	}
	const DWORD flags = (port == INTERNET_DEFAULT_HTTPS_PORT) ? WINHTTP_FLAG_SECURE : 0;
	HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
		WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (!hRequest) {
		errOut = "WinHttpOpenRequest";
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	if (!WinHttpSetOption(hRequest, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
		errOut = "WebSocket upgrade option";
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
		!WinHttpReceiveResponse(hRequest, nullptr)) {
		errOut = "HTTP 101 upgrade";
		WinHttpCloseHandle(hRequest);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	HINTERNET ws = WinHttpWebSocketCompleteUpgrade(hRequest, NULL);
	WinHttpCloseHandle(hRequest);
	if (!ws) {
		errOut = "WebSocketCompleteUpgrade";
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	const std::string hsReq = std::string("{\"type\":\"request-initialize\",\"payload\":{\"clientVersion\":") +
		std::to_string(kRadarProtocolVersion) + "}}";
	std::string hsResp;
	if (!WsSendText(ws, hsReq) || !WsRecvText(ws, hsResp, 8000)) {
		errOut = "Handshake v2 alis";
		WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
		WinHttpCloseHandle(ws);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	if (hsResp.find("response-success") == std::string::npos) {
		errOut = "Handshake: " + hsResp.substr(0, 240);
		WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
		WinHttpCloseHandle(ws);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	const std::string initPub = "{\"type\":\"initialize-publish\",\"payload\":{}}";
	std::string initResp;
	if (!WsSendText(ws, initPub) || !WsRecvText(ws, initResp, 10000)) {
		errOut = "initialize-publish";
		WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
		WinHttpCloseHandle(ws);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	if (initResp.find("response-error") != std::string::npos) {
		errOut = initResp.substr(0, 400);
		WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
		WinHttpCloseHandle(ws);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}
	std::string sessionId, authTok;
	if (!FindJsonStringValue(initResp, "session_id", sessionId))
		FindJsonStringValue(initResp, "sessionId", sessionId);
	FindJsonStringValue(initResp, "session_auth_token", authTok);
	if (authTok.empty())
		FindJsonStringValue(initResp, "sessionAuthToken", authTok);
	(void)authTok;
	if (sessionId.empty()) {
		errOut = std::string("session_id: ") + initResp.substr(0, 280);
		WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
		WinHttpCloseHandle(ws);
		WinHttpCloseHandle(hConnect);
		WinHttpCloseHandle(hSession);
		return false;
	}

	const std::string viewUrl = BuildCloudRadarViewerUrl(sessionId);
	{
		std::lock_guard<std::mutex> lk(g_status_mtx);
		g_session_view_url = viewUrl;
		g_status_line = "Connected to API | ";
		g_status_line += g_session_view_url;
	}

	const auto tick = std::chrono::milliseconds(PublishIntervalMs());
	auto next_publish = std::chrono::steady_clock::now();
	while (Settings::misc::cloudRadar && client) {
		const auto now = std::chrono::steady_clock::now();
		if (now < next_publish)
			std::this_thread::sleep_until(next_publish);

		std::string body;
		BuildRadarStateJson(body);
		const std::string msg = "{\"type\":\"notify-radar-state\",\"payload\":{\"state\":" + body + "}}";
		if (!WsSendText(ws, msg)) {
			errOut = "notify test";
			break;
		}
		(void)WsDrainPendingNonBlocking(ws);
		next_publish = std::chrono::steady_clock::now() + tick;
	}

	(void)WsSendText(ws, std::string("{\"type\":\"disconnect\",\"payload\":{\"reason\":\"expectional\"}}"));
	WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
	WinHttpCloseHandle(ws);
	WinHttpCloseHandle(hConnect);
	WinHttpCloseHandle(hSession);
	return errOut.empty();
}

void CloudRadarWorker() {
	for (;;) {
		if (!Settings::misc::cloudRadar) {
			std::this_thread::sleep_for(std::chrono::milliseconds(400));
			std::lock_guard<std::mutex> lk(g_status_mtx);
			g_status_line = "Cloud Radar: Disabled";
			g_session_view_url.clear();
			continue;
		}
		if (!client) {
			std::this_thread::sleep_for(std::chrono::milliseconds(500));
			continue;
		}
		std::string err;
		{
			std::lock_guard<std::mutex> lk(g_status_mtx);
			g_status_line = "Connecting...";
			g_session_view_url.clear();
		}
		RunOneRadarSession(Settings::misc::cloudRadarPublishUrl, err);
		if (!err.empty()) {
			std::lock_guard<std::mutex> lk(g_status_mtx);
			g_status_line = std::string("Cloud Radar: ") + err;
			g_session_view_url.clear();
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1200));
	}
}

} 

void cloud_radar_start_thread() {
	bool e = false;
	if (!g_thread_started.compare_exchange_strong(e, true))
		return;
	std::thread(CloudRadarWorker).detach();
}

void cloud_radar_render_menu_misc() {
	ImGui::Separator();
	ImGui::TextUnformatted("Cloud Radar");
	ImGui::Checkbox("Enable##cloudradar", &Settings::misc::cloudRadar);
	
	std::string status, url;
	{
		std::lock_guard<std::mutex> lk(g_status_mtx);
		status = g_status_line;
		url = g_session_view_url;
	}
	ImGui::TextWrapped("%s", status.c_str());
	if (!url.empty() && ImGui::SmallButton("Copy viewer URL##crd"))
		ImGui::SetClipboardText(url.c_str());
	
}
