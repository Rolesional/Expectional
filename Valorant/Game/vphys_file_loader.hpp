#pragma once

#include "catalyst_world_bvh.hpp"
#include "vpk_reader.hpp"
#include "globals.hpp"
#include "offsets_runtime.hpp"
#include "../Driver/driver.hpp"

#include <Windows.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace vphys_loader {

static std::string ExeDirectory()
{
	char buf[ MAX_PATH ]{};
	GetModuleFileNameA( nullptr, buf, MAX_PATH );
	std::string p = buf;
	const auto sl = p.find_last_of( "\\/" );
	return sl != std::string::npos ? p.substr( 0, sl + 1 ) : std::string{};
}

static std::vector<std::uint8_t> ReadFileDisk( const std::string& path )
{
	std::ifstream f( path, std::ios::binary );
	if ( !f.is_open() ) return {};
	f.seekg( 0, std::ios::end );
	const auto sz = static_cast<std::size_t>( f.tellg() );
	f.seekg( 0 );
	if ( sz == 0 || sz > 256ull * 1024 * 1024 ) return {};
	std::vector<std::uint8_t> buf( sz );
	f.read( reinterpret_cast<char*>( buf.data() ), static_cast<std::streamsize>( sz ) );
	return buf;
}

static std::string ReadCurrentMapName()
{
	const std::uintptr_t eng = g_GameMem.engine_address();
	if ( !eng || !offsets::dwNetworkGameClient ) return {};

	const std::uintptr_t pNgc = g_GameMem.readv<std::uintptr_t>(
		eng + static_cast<std::uintptr_t>( offsets::dwNetworkGameClient ) );
	if ( !pNgc || pNgc < 0x10000ULL ) return {};

	for ( std::uint32_t off : { 0x218u, 0x210u, 0x220u } )
	{
		const auto pStr = g_GameMem.readv<std::uintptr_t>( pNgc + off );
		if ( !pStr ) continue;
		std::string raw = g_GameMem.ReadString( pStr, 260 );
		if ( raw.empty() ) continue;

		const auto sl = raw.find_last_of( "\\/" );
		if ( sl != std::string::npos ) raw.erase( 0, sl + 1 );
		if ( raw.size() > 4 && raw.compare( raw.size() - 4, 4, ".bsp" ) == 0 )
			raw.resize( raw.size() - 4 );
		while ( !raw.empty() && static_cast<unsigned char>( raw.back() ) <= ' ' )
			raw.pop_back();

		if ( !raw.empty() && raw.size() < 64 &&
			 raw[0] != '\0' && std::isalnum( static_cast<unsigned char>( raw[0] ) ) )
			return raw;
	}
	return {};
}

static std::vector<unsigned char> HexToBytes( const std::string& hex )
{
	std::string h;
	h.reserve( hex.size() );
	for ( char c : hex )
		if ( !std::isspace( static_cast<unsigned char>( c ) ) ) h += c;

	std::vector<unsigned char> out;
	out.reserve( h.size() / 2 );
	for ( std::size_t i = 0; i + 1 < h.size(); i += 2 )
		out.push_back( static_cast<unsigned char>(
			std::stoul( h.substr( i, 2 ), nullptr, 16 ) ) );
	return out;
}

template<typename T>
static std::vector<std::vector<T>> ParseSection(
	const std::uint8_t* data, std::size_t size,
	const std::string& sectionName )
{
	std::vector<std::vector<T>> result;
	std::istringstream ss(
		std::string( reinterpret_cast<const char*>( data ), size ) );
	std::string line;
	bool inMeshes = false;

	while ( std::getline( ss, line ) )
	{
		if ( line.find( "m_meshes" ) != std::string::npos ) inMeshes = true;
		if ( !inMeshes ) continue;
		if ( line.find( sectionName ) == std::string::npos ) continue;

		if ( !std::getline( ss, line ) ) break;
		if ( line.find( "#[" ) == std::string::npos ) continue;

		std::string hexStr;
		while ( std::getline( ss, line ) && line.find( ']' ) == std::string::npos )
			hexStr += line;

		const auto bytes = HexToBytes( hexStr );
		if ( bytes.size() < sizeof( T ) ) continue;

		std::vector<T> elems;
		elems.reserve( bytes.size() / sizeof( T ) );
		for ( std::size_t i = 0; i + sizeof( T ) <= bytes.size(); i += sizeof( T ) )
		{
			T elem{};
			std::memcpy( &elem, bytes.data() + i, sizeof( T ) );
			elems.push_back( elem );
		}
		result.push_back( std::move( elems ) );
	}
	return result;
}

static std::vector<ex_world_bvh::bvh::triangle> TrianglesFromTextKV3(
	const std::uint8_t* data, std::size_t size )
{
	struct Tri { int a, b, c; };
	struct Vtx { float x, y, z; };

	const auto triGroups = ParseSection<Tri>( data, size, "m_Triangles" );
	const auto vtxGroups = ParseSection<Vtx>( data, size, "m_Vertices"  );
	if ( triGroups.empty() || triGroups.size() != vtxGroups.size() ) return {};

	std::vector<ex_world_bvh::bvh::triangle> out;
	out.reserve( 262144 );

	for ( std::size_t gi = 0; gi < triGroups.size(); ++gi )
	{
		const auto& tris  = triGroups[ gi ];
		const auto& verts = vtxGroups[ gi ];
		for ( const auto& t : tris )
		{
			if ( t.a < 0 || t.b < 0 || t.c < 0 ) continue;
			const auto ua = static_cast<std::size_t>( t.a );
			const auto ub = static_cast<std::size_t>( t.b );
			const auto uc = static_cast<std::size_t>( t.c );
			if ( ua >= verts.size() || ub >= verts.size() || uc >= verts.size() ) continue;

			ex_world_bvh::bvh::triangle tri{};
			tri.v0 = { verts[ua].x, verts[ua].y, verts[ua].z };
			tri.v1 = { verts[ub].x, verts[ub].y, verts[ub].z };
			tri.v2 = { verts[uc].x, verts[uc].y, verts[uc].z };
			out.push_back( tri );
		}
	}
	return out;
}

static bool IsValidMapCoord( float v )
{
	
	const auto u = *reinterpret_cast<const std::uint32_t*>( &v );
	if ( ( u & 0x7F800000u ) == 0x7F800000u ) return false; 
	return v > -65536.0f && v < 65536.0f;
}

static std::vector<ex_world_bvh::bvh::triangle> TrianglesFromBinaryBlob(
	const std::uint8_t* bin, std::size_t bin_size )
{
	struct Vtx { float x, y, z; };
	struct Tri { std::int32_t a, b, c; };

	std::vector<ex_world_bvh::bvh::triangle> best;

	for ( std::size_t vi = 0; vi + 12 <= bin_size; vi += 4 )
	{
		Vtx v0;
		std::memcpy( &v0, bin + vi, 12 );
		if ( !IsValidMapCoord( v0.x ) || !IsValidMapCoord( v0.y ) ||
			 !IsValidMapCoord( v0.z ) ) continue;

		std::size_t nv = 1;
		while ( vi + nv * 12 + 12 <= bin_size )
		{
			Vtx vt;
			std::memcpy( &vt, bin + vi + nv * 12, 12 );
			if ( !IsValidMapCoord( vt.x ) || !IsValidMapCoord( vt.y ) ||
				 !IsValidMapCoord( vt.z ) ) break;
			++nv;
		}
		if ( nv < 64 ) continue; 

		const std::size_t vbytes   = nv * 12;
		const std::size_t ibytes   = vi; 
		const std::size_t ni_max   = ibytes / 12;
		if ( ni_max < 32 ) continue;

		std::size_t ti_end = vi;
		std::size_t ti     = ti_end;
		while ( ti >= 12 )
		{
			ti -= 12;
			Tri t;
			std::memcpy( &t, bin + ti, 12 );
			if ( t.a < 0 || t.b < 0 || t.c < 0 ||
				 t.a >= static_cast<int>( nv ) ||
				 t.b >= static_cast<int>( nv ) ||
				 t.c >= static_cast<int>( nv ) )
				break;
		}
		const std::size_t ti_start = ti + 12;
		const std::size_t ni       = ( ti_end - ti_start ) / 12;
		if ( ni < 32 ) continue;

		std::vector<ex_world_bvh::bvh::triangle> cand;
		cand.reserve( ni );

		std::vector<Vtx> verts( nv );
		std::memcpy( verts.data(), bin + vi, nv * 12 );

		for ( std::size_t ii = ti_start; ii < ti_end; ii += 12 )
		{
			Tri t;
			std::memcpy( &t, bin + ii, 12 );
			const auto ua = static_cast<std::size_t>( t.a );
			const auto ub = static_cast<std::size_t>( t.b );
			const auto uc = static_cast<std::size_t>( t.c );
			if ( ua >= nv || ub >= nv || uc >= nv ) continue;

			ex_world_bvh::bvh::triangle tri{};
			tri.v0 = { verts[ua].x, verts[ua].y, verts[ua].z };
			tri.v1 = { verts[ub].x, verts[ub].y, verts[ub].z };
			tri.v2 = { verts[uc].x, verts[uc].y, verts[uc].z };
			cand.push_back( tri );
		}
		if ( cand.size() > best.size() )
			best = std::move( cand );

		{
			const std::size_t idx_start = vi + vbytes;
			std::size_t ni2 = 0;
			while ( idx_start + ( ni2 + 1 ) * 12 <= bin_size )
			{
				Tri t;
				std::memcpy( &t, bin + idx_start + ni2 * 12, 12 );
				if ( t.a < 0 || t.b < 0 || t.c < 0 ||
					 t.a >= static_cast<int>( nv ) ||
					 t.b >= static_cast<int>( nv ) ||
					 t.c >= static_cast<int>( nv ) ) break;
				++ni2;
			}
			if ( ni2 >= 32 )
			{
				std::vector<Vtx> verts2( nv );
				std::memcpy( verts2.data(), bin + vi, nv * 12 );

				std::vector<ex_world_bvh::bvh::triangle> cand2;
				cand2.reserve( ni2 );
				for ( std::size_t ii = 0; ii < ni2; ++ii )
				{
					Tri t;
					std::memcpy( &t, bin + idx_start + ii * 12, 12 );
					const auto ua = static_cast<std::size_t>( t.a );
					const auto ub = static_cast<std::size_t>( t.b );
					const auto uc = static_cast<std::size_t>( t.c );
					if ( ua >= nv || ub >= nv || uc >= nv ) continue;
					ex_world_bvh::bvh::triangle tri{};
					tri.v0 = { verts2[ua].x, verts2[ua].y, verts2[ua].z };
					tri.v1 = { verts2[ub].x, verts2[ub].y, verts2[ub].z };
					tri.v2 = { verts2[uc].x, verts2[uc].y, verts2[uc].z };
					cand2.push_back( tri );
				}
				if ( cand2.size() > best.size() )
					best = std::move( cand2 );
			}
		}

		vi += vbytes - 4; 
	}
	return best;
}

static std::vector<ex_world_bvh::bvh::triangle> TryExtractFromRawBlock(
	const std::uint8_t* blk_data, std::size_t blk_size )
{
	
	if ( blk_size >= 4 && blk_data[0] == 'V' && blk_data[1] == 'K' &&
		 blk_data[2] == 'V' && blk_data[3] == 0x03 )
	{
		const auto bin = vpk_reader::ExtractBinaryBytesSection( blk_data, blk_size );
		if ( !bin.empty() )
		{
			auto t = TrianglesFromBinaryBlob( bin.data(), bin.size() );
			if ( !t.empty() ) return t;
		}
	}
	
	auto t2 = TrianglesFromTextKV3( blk_data, blk_size );
	if ( !t2.empty() ) return t2;

	return TrianglesFromBinaryBlob( blk_data, blk_size );
}

static std::vector<ex_world_bvh::bvh::triangle> TrianglesFromBinaryVphys(
	const std::uint8_t* data, std::size_t size )
{
	
	const auto blocks = vpk_reader::FindAllBlocks( data, size );

	auto GetOrder = []( const char* t ) -> int
	{
		if ( std::memcmp( t, "PHYS", 4 ) == 0 ) return 0;
		if ( std::memcmp( t, "DATA", 4 ) == 0 ) return 1;
		if ( std::memcmp( t, "MBUF", 4 ) == 0 ) return 2;
		return 9;
	};

	std::vector<vpk_reader::Src2Block> sorted = blocks;
	std::sort( sorted.begin(), sorted.end(),
		[&]( const auto& a, const auto& b )
		{ return GetOrder( a.type ) < GetOrder( b.type ); } );

	for ( const auto& blk : sorted )
	{
		if ( blk.offset + blk.size > size ) continue;
		auto t = TryExtractFromRawBlock( data + blk.offset, blk.size );
		if ( !t.empty() ) return t;
	}

	{
		auto t = TryExtractFromRawBlock( data, size );
		if ( !t.empty() ) return t;
	}

	return {};
}

static std::vector<ex_world_bvh::bvh::triangle> LoadTrianglesForMap(
	const std::string& mapName, std::string& source_out )
{
	source_out = "E:none";
	const std::string exeDir = ExeDirectory();

	const std::vector<std::string> manual_paths = {
		exeDir + "vphys\\" + mapName + ".vphys",
		exeDir + "vphys\\" + mapName + "_c0.vphys",
		"C:\\vphys\\" + mapName + ".vphys",
		"C:\\vphys\\" + mapName + "_c0.vphys",
	};
	for ( const auto& p : manual_paths )
	{
		if ( GetFileAttributesA( p.c_str() ) == INVALID_FILE_ATTRIBUTES ) continue;
		auto buf = ReadFileDisk( p );
		if ( buf.empty() ) continue;
		auto tris = TrianglesFromTextKV3( buf.data(), buf.size() );
		if ( !tris.empty() )
		{
			source_out = "file:" + mapName;
			return tris;
		}
	}

	const std::string game_dir = vpk_reader::GetCs2GameDir();
	if ( game_dir.empty() )
	{
		source_out = "E:nodir";
		return {};
	}

	bool        is_binary  = false;
	std::string found_path;
	auto vpk_data = vpk_reader::FindMapVphys(
		game_dir, mapName, is_binary, found_path );
	if ( vpk_data.empty() )
	{
		source_out = "E:novpk";
		return {};
	}

	std::vector<ex_world_bvh::bvh::triangle> tris;
	if ( is_binary )
		tris = TrianglesFromBinaryVphys( vpk_data.data(), vpk_data.size() );
	else
		tris = TrianglesFromTextKV3( vpk_data.data(), vpk_data.size() );

	if ( tris.empty() )
	{
		
		source_out = "E:parse:" + found_path;
		return {};
	}

	source_out = "vpk:" + mapName;
	return tris;
}

inline std::string g_last_loaded_map;
inline std::string g_load_source; 

static bool TickFileLoader()
{
	const std::string mapName = ReadCurrentMapName();
	if ( mapName.empty() )
	{
		if ( g_load_source.empty() || g_load_source == "E:none" )
			g_load_source = "E:nomap";
		return false;
	}

	if ( mapName == g_last_loaded_map && ex_world_bvh::g_world_bvh.valid() )
		return true;

	std::string src;
	auto tris = LoadTrianglesForMap( mapName, src );
	g_load_source = src;
	if ( tris.empty() ) return false;

	ex_world_bvh::g_world_bvh.load_triangles( std::move( tris ) );
	g_last_loaded_map = mapName;
	return true;
}

} 
