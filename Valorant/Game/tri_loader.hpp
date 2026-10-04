#pragma once

#include "catalyst_world_bvh.hpp"
#include "globals.hpp"
#include "map_tri_export.hpp"
#include "offsets_runtime.hpp"
#include "expectional_paths.hpp"
#include "expectional_winio.hpp"
#include "../Driver/driver.hpp"

#include <Windows.h>
#include <ShlObj.h>
#include <cctype>
#include <cstring>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace tri_loader {

static const std::map<std::string, std::string> kMapMeshFiles = {
	{ "de_dust2", "de_dust2.tri" },
	{ "de_mirage", "de_mirage.tri" },
	{ "de_inferno", "de_inferno.tri" },
	{ "de_nuke", "de_nuke.tri" },
	{ "de_overpass", "de_overpass.tri" },
	{ "de_vertigo", "de_vertigo.tri" },
	{ "de_ancient", "de_ancient.tri" },
	{ "de_anubis", "de_anubis.tri" },
	{ "de_train", "de_train.tri" },
	{ "de_golden", "de_golden.tri" },
	{ "de_rooftop", "de_rooftop.tri" },
	{ "de_palacio", "de_palacio.tri" },
	{ "de_ancient_night", "de_ancient_night.tri" },
	{ "cs_office", "cs_office.tri" },
	{ "cs_italy", "cs_italy.tri" },
	{ "cs_agency", "cs_agency.tri" },
	{ "ar_baggage", "ar_baggage.tri" },
	{ "ar_shoots", "ar_shoots.tri" },
	{ "ar_shoots_night", "ar_shoots_night.tri" },
	{ "de_poseidon", "de_poseidon.tri" },
	{ "cs_alpine", "cs_alpine.tri" },
	{ "de_sanctum", "de_sanctum.tri" },
	{ "de_stronghold", "de_stronghold.tri" },
	{ "de_warden", "de_warden.tri" },
	{ "de_cache", "de_cache.tri" },
};

static std::string CleanMapName( std::string mapName )
{
	if ( mapName.empty() )
		return {};

	const size_t dotPos = mapName.rfind( '.' );
	if ( dotPos != std::string::npos && dotPos > 0 )
		mapName = mapName.substr( 0, dotPos );

	const size_t slashPos = mapName.find_last_of( "/\\" );
	if ( slashPos != std::string::npos && slashPos + 1 < mapName.size() )
		mapName = mapName.substr( slashPos + 1 );

	if ( mapName.size() >= 5 && mapName.compare( 0, 5, "maps/" ) == 0 )
		mapName = mapName.substr( 5 );
	if ( mapName.size() >= 5 && mapName.compare( 0, 5, "maps\\" ) == 0 )
		mapName = mapName.substr( 5 );

	while ( !mapName.empty() && ( mapName.front() == ' ' || mapName.front() == '\t' ) )
		mapName.erase( 0, 1 );
	while ( !mapName.empty() && ( mapName.back() == ' ' || mapName.back() == '\t' ) )
		mapName.pop_back();

	return mapName;
}

static bool MapBasenameOk( const std::string& test )
{
	if ( test.empty() || test.size() > 200 )
		return false;
	for ( unsigned char c : test )
	{
		if ( c < 32 )
			return false;
		if ( c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|' )
			return false;
	}
	return true;
}

static std::string ExeDir()
{
	char buf[ MAX_PATH ]{};
	GetModuleFileNameA( nullptr, buf, MAX_PATH );
	std::string p = buf;
	const auto sl = p.find_last_of( "\\/" );
	return sl != std::string::npos ? p.substr( 0, sl + 1 ) : std::string{};
}

inline std::string ReadMapName()
{
	const std::uintptr_t eng = g_GameMem.engine_address();
	if (!eng || !offsets::dwNetworkGameClient)
		return {};

	const std::uintptr_t pNgc = g_GameMem.readv<std::uintptr_t>(
		eng + static_cast<std::uintptr_t>(offsets::dwNetworkGameClient));
	if (!pNgc || pNgc < 0x10000ULL)
		return {};

	constexpr std::uint32_t kMapPath = 0x210u;
	constexpr std::uint32_t kMapName = 0x218u;

	std::string raw;
	const std::uintptr_t pName = g_GameMem.readv<std::uintptr_t>(pNgc + kMapName);
	if (pName)
		raw = g_GameMem.ReadString(pName, 160);
	if (raw.empty()) {
		const std::uintptr_t pPath = g_GameMem.readv<std::uintptr_t>(pNgc + kMapPath);
		if (pPath)
			raw = g_GameMem.ReadString(pPath, 260);
	}
	if (raw.empty()) {
		for (std::uint32_t off : { 0x220u }) {
			const auto pStr = g_GameMem.readv<std::uintptr_t>(pNgc + off);
			if (!pStr)
				continue;
			raw = g_GameMem.ReadString(pStr, 260);
			if (!raw.empty())
				break;
		}
	}

	const std::string cleaned = CleanMapName(raw);
	if (!cleaned.empty() && cleaned.size() <= 200 && MapBasenameOk(cleaned))
		return cleaned;
	return {};
}

static std::filesystem::path LocalExpectionalMapsPath()
{
	const std::wstring dirW = ExpectionalPaths::GlobalMapsDirWide();
	if (dirW.empty())
		return {};
	(void)ExpectionalWinIO::EnsureDirectoryWide(dirW);
	return std::filesystem::path(dirW);
}

static std::vector<ex_world_bvh::bvh::triangle> LoadTriFile( const std::filesystem::path& filePath )
{
	
#pragma pack( push, 1 )
	struct TriRawUm {
		float v0x, v0y, v0z;
		float v1x, v1y, v1z;
		float v2x, v2y, v2z;
	};
#pragma pack( pop )
	static_assert( sizeof( TriRawUm ) == 36, "um .tri layout" );

	std::vector<uint8_t> file_bytes;
	if (!ExpectionalWinIO::ReadAllBytesWide(filePath.wstring(), file_bytes))
		return {};

	const std::size_t byte_sz = file_bytes.size();
	if ( byte_sz % sizeof( TriRawUm ) != 0 )
		return {};

	const std::size_t triCount = byte_sz / sizeof( TriRawUm );
	std::vector<TriRawUm> raw( triCount );
	std::memcpy(raw.data(), file_bytes.data(), byte_sz);

	std::vector<ex_world_bvh::bvh::triangle> out;
	out.reserve( triCount );
	for ( const auto& r : raw )
	{
		ex_world_bvh::bvh::triangle t{};
		t.v0 = ex_world_bvh::Vector3{ r.v0x, r.v0y, r.v0z };
		t.v1 = ex_world_bvh::Vector3{ r.v1x, r.v1y, r.v1z };
		t.v2 = ex_world_bvh::Vector3{ r.v2x, r.v2y, r.v2z };
		out.push_back( t );
	}
	return out;
}

inline std::string g_last_loaded_map;
inline std::string g_load_source;

inline std::string g_tri_gave_up_on_map;

static constexpr std::size_t kMinLoadedTriangles = 256u;
inline constexpr DWORD k_map_check_interval_ms = 2500;
inline constexpr DWORD k_map_check_export_ms = 1200;

static bool TryLoadTriFromPath(const std::filesystem::path& path,
                               std::vector<ex_world_bvh::bvh::triangle>& tris) {
	RemoveTriMeshFileIfInvalid(path);
	if (!TriMeshFileValid(path))
		return false;
	tris = LoadTriFile(path);
	if (tris.size() < kMinLoadedTriangles) {
		tris.clear();
		RemoveTriMeshFileIfInvalid(path);
		return false;
	}
	return true;
}

static bool TickFileLoader()
{
	if (g_GameMem.attached_pid == 0 || !client)
		return false;

	const std::string map = ReadMapName();

	if (map.empty()) {
		g_tri_gave_up_on_map.clear();
		if (g_load_source.empty())
			g_load_source = "E:nomap";
		return false;
	}

	if (map != g_last_loaded_map) {
		if (ex_world_bvh::g_world_bvh.valid())
			ex_world_bvh::g_world_bvh.clear();
		g_last_loaded_map.clear();
	}

	if (map == g_last_loaded_map && ex_world_bvh::g_world_bvh.valid())
		return true;

	std::vector<ex_world_bvh::bvh::triangle> tris;

	const std::filesystem::path appdir = LocalExpectionalMapsPath();
	if (!appdir.empty()) {
		const std::filesystem::path appTri = appdir / (map + ".tri");
		if (!TryLoadTriFromPath(appTri, tris) && std::filesystem::exists(appTri))
			RemoveTriMeshFileIfInvalid(appTri);
	}

	if (tris.empty()) {
		const std::string exeDir = ExeDir();
		if (!exeDir.empty()) {
			std::filesystem::path mapsDir = std::filesystem::path(exeDir) / "maps";
			std::error_code ec;
			std::filesystem::create_directories(mapsDir, ec);
			const std::filesystem::path exeTri = mapsDir / (map + ".tri");
			if (!TryLoadTriFromPath(exeTri, tris)) {
				const auto it = kMapMeshFiles.find(map);
				if (it != kMapMeshFiles.end())
					TryLoadTriFromPath(mapsDir / it->second, tris);
			}
		}
	}

	if (tris.empty()) {
		g_load_source = "E:nofile";
		MapTriExport_KickIfNeeded(map);
		return false;
	}

	ex_world_bvh::g_world_bvh.load_triangles(std::move(tris));
	g_last_loaded_map = map;
	g_tri_gave_up_on_map.clear();
	g_load_source = "tri:" + map;
	return true;
}

} 
