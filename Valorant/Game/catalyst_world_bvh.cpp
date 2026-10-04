#include "catalyst_world_bvh.hpp"
#include "tri_loader.hpp"
#include "map_tri_export.hpp"
#include "cat_mem_scan.hpp"
#include "globals.hpp"
#include "../Driver/driver.hpp"
#include <Windows.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <limits>
#include <shared_mutex>
#include <thread>
#include <unordered_set>
namespace ex_world_bvh {

namespace {

std::atomic<bool>          g_bvh_thread_started{ false };
std::atomic<std::uint32_t> g_bvh_parse_attempts{ 0 };
std::atomic<std::uint32_t> g_bvh_parse_successes{ 0 };
std::atomic<std::uintptr_t> g_dbg_client_module{ 0 };
std::atomic<std::uintptr_t> g_dbg_vphys_module{ 0 };
std::atomic<std::uintptr_t> g_dbg_pattern_trace{ 0 };
std::atomic<std::uintptr_t> g_dbg_pattern_surface{ 0 };
std::atomic<std::uintptr_t> g_dbg_vphys2_world{ 0 };
std::atomic<int>           g_dbg_body_count{ 0 };
std::atomic<int>           g_dbg_last_extracted{ 0 };
std::atomic<std::uint32_t> g_dbg_world_vtable_found{ 0 };

static int g_world_inner_off = 0x30;
static int g_world_arr_off   = 0x110;
static int g_world_cnt_off   = 0x268;

static std::uintptr_t g_bvh_movrip_rva_cache = 0;

static void BvhRememberMovRip( std::uintptr_t cli, std::uintptr_t instr )
{
	if ( cli && instr > cli )
		g_bvh_movrip_rva_cache = instr - cli;
}

static bool BvhIsInModuleRange( std::uintptr_t ptr, std::uintptr_t mod_base )
{
	if ( !ptr || !mod_base || ptr < mod_base )
		return false;
	const std::size_t sz = cat_mem::module_image_size( mod_base );
	return sz > 0 && ptr < mod_base + sz;
}

static bool BvhLooksLikeWorld( std::uintptr_t world )
{
	if ( !world || world < 0x10000 || world > 0x00007FFFFFFFFFFFULL )
		return false;

	{
		const std::uintptr_t vph = cat_mem::vphysics_module( );
		const std::uintptr_t cli = cat_mem::client_module( );
		if ( BvhIsInModuleRange( world, vph ) || BvhIsInModuleRange( world, cli ) )
			return false;
	}

	static constexpr int k_inner_offs[] = { 0x28, 0x30, 0x38, 0x40, 0x20, 0x48, 0x50, 0x60 };
	static constexpr int k_arr_offs[]   = { 0x100, 0x108, 0x110, 0x118, 0x120, 0xF8, 0x128, 0x130 };
	static constexpr int k_cnt_offs[]   = { 0x260, 0x268, 0x270, 0x258, 0x278, 0x280, 0x250, 0x248 };

	for ( int io : k_inner_offs )
	{
		const std::uintptr_t inner = cat_mem::readv<std::uintptr_t>( world + io );
		if ( !inner || inner < 0x10000 || inner > 0x00007FFFFFFFFFFFULL || inner == world )
			continue;
		if ( BvhIsInModuleRange( inner, cat_mem::vphysics_module() ) ||
		     BvhIsInModuleRange( inner, cat_mem::client_module() ) )
			continue;

		for ( int ao : k_arr_offs )
		{
			const std::uintptr_t bodies = cat_mem::readv<std::uintptr_t>( inner + ao );
			if ( !bodies || bodies < 0x10000 || bodies > 0x00007FFFFFFFFFFFULL )
				continue;
			if ( bodies == world || bodies == inner )
				continue;
			if ( BvhIsInModuleRange( bodies, cat_mem::vphysics_module() ) ||
			     BvhIsInModuleRange( bodies, cat_mem::client_module() ) )
				continue;

			for ( int co : k_cnt_offs )
			{
				const std::int32_t bc = cat_mem::readv<std::int32_t>( bodies + co );
				
				if ( bc > 50 && bc < 200000 )
				{
					g_world_inner_off = io;
					g_world_arr_off   = ao;
					g_world_cnt_off   = co;
					return true;
				}
			}
		}
	}
	return false;
}

static std::uintptr_t BvhWorldFromMovRip( std::uintptr_t instr, std::uintptr_t cli )
{
	std::uint8_t b[ 3 ]{};
	if ( !cat_mem::read( instr, b, 3 ) )
		return 0;
	if ( !( b[ 0 ] == 0x48 || b[ 0 ] == 0x4C ) || b[ 1 ] != 0x8B )
		return 0;
	if ( b[ 2 ] != 0x05 && b[ 2 ] != 0x0D && b[ 2 ] != 0x15 && b[ 2 ] != 0x1D )
		return 0;
	const std::uintptr_t glob_va = cat_mem::resolve_rip( instr );
	const std::uintptr_t deref1 = cat_mem::readv<std::uintptr_t>( glob_va );
	if ( BvhLooksLikeWorld( deref1 ) )
	{
		BvhRememberMovRip( cli, instr );
		return deref1;
	}
	if ( !deref1 || deref1 < 0x10000 )
		return 0;
	const std::uintptr_t deref2 = cat_mem::readv<std::uintptr_t>( deref1 );
	if ( BvhLooksLikeWorld( deref2 ) )
	{
		BvhRememberMovRip( cli, instr );
		return deref2;
	}
	return 0;
}

static std::uintptr_t BvhWorldFastPath( std::uintptr_t cli, std::uintptr_t mod_hi )
{
	if ( !g_bvh_movrip_rva_cache )
		return 0;
	const std::uintptr_t instr = cli + g_bvh_movrip_rva_cache;
	if ( instr < cli + 0x1000 || instr + 7 > mod_hi )
	{
		g_bvh_movrip_rva_cache = 0;
		return 0;
	}
	const std::uintptr_t w = BvhWorldFromMovRip( instr, cli );
	if ( !w )
		g_bvh_movrip_rva_cache = 0;
	return w;
}

static std::uintptr_t BvhScanBackwardMovRip( std::uintptr_t anchor, std::uintptr_t mod_lo, std::size_t max_back,
	std::uintptr_t cli )
{
	const std::uintptr_t stop = anchor > max_back ? anchor - max_back : mod_lo;
	for ( std::uintptr_t p = anchor; p > stop && p >= mod_lo + 7; --p )
	{
		const std::uintptr_t w = BvhWorldFromMovRip( p, cli );
		if ( w )
			return w;
	}
	return 0;
}

static std::uintptr_t BvhTryWorldNearAnchor( std::uintptr_t hit, std::uintptr_t cli, std::uintptr_t mod_hi,
	std::uintptr_t* trace_anchor_out )
{
	static constexpr int k_deltas[] = {
		-0x48, -0x44, -0x40, -0x3c, -0x38, -0x34, -0x30, -0x2c, -0x28,
		-0x24, -0x22, -0x20, -0x1e, -0x1c, -0x1a, -0x18, -0x16, -0x14,
		-0x12, -0x10, -0xe, -0xc,
	};
	for ( int delta : k_deltas )
	{
		const std::uintptr_t site = hit + static_cast<std::uintptr_t>( static_cast<std::intptr_t>( delta ) );
		if ( site < cli + 0x1000 || site + 7 > mod_hi )
			continue;
		const std::uintptr_t w = BvhWorldFromMovRip( site, cli );
		if ( w )
		{
			if ( trace_anchor_out )
				*trace_anchor_out = hit;
			return w;
		}
	}
	const std::uintptr_t w2 = BvhScanBackwardMovRip( hit, cli, 0x280, cli );
	if ( w2 && trace_anchor_out )
		*trace_anchor_out = hit;
	return w2;
}

static std::uintptr_t BvhResolveVPhys2World( std::uintptr_t cli, std::uintptr_t* trace_anchor_out )
{
	static constexpr const char* k_trace_patterns[] = {
		"E8 ? ? ? ? C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B CF",
		"E8 ? ? ? ? C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B CE",
		"E8 ? ? ? ? C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B D9",
		"E8 ? ? ? ? C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B F9",
		"E8 ? ? ? ? C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B E9",
		"E8 ? ? ? ? C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B DE",
		"E8 ? ? ? ? 48 8D 54 24 ? 48 8B CF",
		"E8 ? ? ? ? 48 8D 54 24 ? 48 8B CE",
		"E8 ? ? ? ? 48 8D 54 24 ? 48 8B D9",
		"E8 ? ? ? ? 48 8D 54 24 ? 48 8B F9",
	};
	const std::size_t mod_sz = cat_mem::module_image_size( cli );
	const std::uintptr_t mod_hi = cli + mod_sz;

	const std::uintptr_t cached_world = BvhWorldFastPath( cli, mod_hi );
	if ( cached_world )
	{
		if ( trace_anchor_out && g_bvh_movrip_rva_cache )
			*trace_anchor_out = cli + g_bvh_movrip_rva_cache;
		return cached_world;
	}

	static constexpr int k_deltas[] = {
		-0x48, -0x44, -0x40, -0x3c, -0x38, -0x34, -0x30, -0x2c, -0x28,
		-0x24, -0x22, -0x20, -0x1e, -0x1c, -0x1a, -0x18, -0x16, -0x14,
		-0x12, -0x10, -0xe, -0xc,
	};
	for ( const char* pat : k_trace_patterns )
	{
		const std::uintptr_t hit = cat_mem::find_pattern( cli, pat );
		if ( !hit )
			continue;
		if ( trace_anchor_out )
			*trace_anchor_out = hit;
		for ( int delta : k_deltas )
		{
			const std::uintptr_t site = hit + static_cast<std::uintptr_t>( static_cast<std::intptr_t>( delta ) );
			if ( site < cli + 0x1000 || site + 7 > mod_hi )
				continue;
			const std::uintptr_t w = BvhWorldFromMovRip( site, cli );
			if ( w )
				return w;
		}
		const std::uintptr_t w2 = BvhScanBackwardMovRip( hit, cli, 0x200, cli );
		if ( w2 )
			return w2;
	}

	static constexpr const char* k_anchor_patterns[] = {
		"C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B CF",
		"C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B CE",
		"C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B D9",
		"C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B F9",
		"C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B E9",
		"C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B DE",
		"C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B C7",
		"C7 87 ? ? ? ? ? ? ? ? 48 8D 54 24 ? 48 8B CB",
		"48 8D 54 24 ? 48 8B CF",
		"48 8D 54 24 ? 48 8B CE",
		"48 8D 54 24 ? 48 8B D9",
		"48 8D 54 24 ? 48 8B F9",
		"48 8D 54 24 ? 48 8B E9",
		"48 8D 54 24 ? 48 8B DE",
		"48 8D 54 24 ? 48 8B C7",
		"48 8D 54 24 ? 48 8B CB",
		"48 8D 54 24 ? 48 8B D8",
		"48 8D 54 24 ? 48 8B F8",
	};
	constexpr std::uint32_t k_exec = IMAGE_SCN_MEM_EXECUTE;
	for ( const char* ap : k_anchor_patterns )
	{
		const auto anchors = cat_mem::find_pattern_all( cli, ap, 400, k_exec );
		for ( std::uintptr_t hit : anchors )
		{
			const std::uintptr_t w = BvhTryWorldNearAnchor( hit, cli, mod_hi, trace_anchor_out );
			if ( w )
				return w;
		}
	}

	return 0;
}

static void BvhEnumDataSections( std::uintptr_t vph,
	std::vector<std::pair<std::uintptr_t, std::size_t>>& sections )
{
	sections.clear( );
	const IMAGE_DOS_HEADER dos = cat_mem::readv<IMAGE_DOS_HEADER>( vph );
	if ( dos.e_magic != IMAGE_DOS_SIGNATURE )
		return;
	const IMAGE_NT_HEADERS nt = cat_mem::readv<IMAGE_NT_HEADERS>(
		vph + static_cast<std::uintptr_t>( dos.e_lfanew ) );
	if ( nt.Signature != IMAGE_NT_SIGNATURE )
		return;
	const std::uintptr_t first_sec = vph + static_cast<std::uintptr_t>( dos.e_lfanew )
		+ offsetof( IMAGE_NT_HEADERS, OptionalHeader )
		+ nt.FileHeader.SizeOfOptionalHeader;
	constexpr std::uint32_t k_data_mask = IMAGE_SCN_CNT_INITIALIZED_DATA | IMAGE_SCN_MEM_READ;
	for ( std::uint16_t si = 0; si < nt.FileHeader.NumberOfSections; ++si )
	{
		const IMAGE_SECTION_HEADER hdr = cat_mem::readv<IMAGE_SECTION_HEADER>(
			first_sec + static_cast<std::uintptr_t>( si ) * sizeof( IMAGE_SECTION_HEADER ) );
		if ( ( hdr.Characteristics & k_data_mask ) != k_data_mask )
			continue;
		if ( hdr.Misc.VirtualSize < 8 || hdr.Misc.VirtualSize > 0x2000000 )
			continue;
		sections.emplace_back( vph + hdr.VirtualAddress, hdr.Misc.VirtualSize );
	}
}

static std::uintptr_t BvhResolveWorldFromVPhysMod( std::uintptr_t vph )
{
	if ( !vph )
		return 0;

	static constexpr const char* k_world_classes[] = {
		"CRnWorld", "RnWorld", "CPhysics2World", "CVPhys2World",
		"CPhysicsWorld", "RnWorldContainer", nullptr,
	};

	std::uintptr_t world_vtable = 0;
	for ( int ci = 0; k_world_classes[ ci ]; ++ci )
	{
		world_vtable = cat_mem::find_vtable( vph, k_world_classes[ ci ] );
		if ( world_vtable )
		{
			g_dbg_world_vtable_found.store( 1, std::memory_order_relaxed );
			break;
		}
	}

	std::vector<std::pair<std::uintptr_t, std::size_t>> data_secs;
	BvhEnumDataSections( vph, data_secs );

	const std::size_t vph_sz = cat_mem::module_image_size( vph );
	const std::size_t cli_sz = cat_mem::module_image_size( cat_mem::client_module() );
	const std::uintptr_t cli_base = cat_mem::client_module();

	auto is_heap = [&]( std::uintptr_t P ) -> bool
	{
		if ( P < 0x10000 || P > 0x00007FFFFFFFFFFFULL )
			return false;
		if ( vph_sz > 0 && P >= vph && P < vph + vph_sz )
			return false;
		if ( cli_sz > 0 && P >= cli_base && P < cli_base + cli_sz )
			return false;
		return true;
	};

	static constexpr std::size_t k_chunk = 0x8000;
	std::vector<std::uint8_t> buf( k_chunk + 8 );

	for ( const auto& [ sec_base, sec_size ] : data_secs )
	{
		for ( std::size_t off = 0; off < sec_size; off += k_chunk )
		{
			const std::size_t read_sz = ( std::min )( k_chunk + 8, sec_size - off );
			if ( !cat_mem::read( sec_base + off, buf.data( ), read_sz ) )
				continue;

			for ( std::size_t i = 0; i + 8 <= read_sz; i += 8 )
			{
				const std::uintptr_t P = *reinterpret_cast<const std::uintptr_t*>( buf.data( ) + i );
				if ( !is_heap( P ) )
					continue;

				if ( world_vtable )
				{
					const std::uintptr_t first_qw = cat_mem::readv<std::uintptr_t>( P );
					if ( first_qw == world_vtable )
						return P;
				}
				else
				{
					if ( BvhLooksLikeWorld( P ) )
						return P;
				}
			}
		}
	}

	if ( world_vtable )
	{
		for ( const auto& [ sec_base, sec_size ] : data_secs )
		{
			for ( std::size_t off = 0; off < sec_size; off += k_chunk )
			{
				const std::size_t read_sz = ( std::min )( k_chunk + 8, sec_size - off );
				if ( !cat_mem::read( sec_base + off, buf.data( ), read_sz ) )
					continue;
				for ( std::size_t i = 0; i + 8 <= read_sz; i += 8 )
				{
					const std::uintptr_t P = *reinterpret_cast<const std::uintptr_t*>( buf.data( ) + i );
					if ( is_heap( P ) && BvhLooksLikeWorld( P ) )
						return P;
				}
			}
		}
	}

	return 0;
}

static bool BvhLooksLikeSurfaceManager( std::uintptr_t sm )
{
	if ( sm < 0x10000 )
		return false;
	for ( const auto off : { 32, 36, 24, 28, 48 } )
	{
		const std::int32_t c = cat_mem::readv<std::int32_t>( sm + off );
		if ( c > 4 && c < 500000 )
			return true;
	}
	const std::uintptr_t ab = cat_mem::readv<std::uintptr_t>( sm + 40 );
	return ab > 0x10000 && ab < 0x00007FFFFFFFFFFFULL;
}

static std::uintptr_t BvhResolveSurfaceManager( std::uintptr_t cli, std::uintptr_t* pattern_hit_out )
{
	static constexpr const char* k_surface_patterns[] = {
		"48 63 41 ? 48 8B 0D",
		"48 63 41 ? 48 8B 05",
		"44 8B 41 ? 48 8B 0D",
		"44 8B 41 ? 48 8B 05",
		"8B 41 ? 48 8B 0D",
		"8B 41 ? 48 8B 05",
		"48 8B 41 ? 48 8B 0D",
		"48 8B 41 ? 48 8B 05",
		"48 63 41 ? 4C 8B 05",
		"48 63 41 ? 4C 8B 0D",
	};
	constexpr std::uint32_t k_exec = IMAGE_SCN_MEM_EXECUTE;
	for ( const char* pat : k_surface_patterns )
	{
		const auto hits = cat_mem::find_pattern_all( cli, pat, 120, k_exec );
		for ( std::uintptr_t hit : hits )
		{
			const std::uintptr_t smglob = cat_mem::resolve_rip( hit + 4 );
			const std::uintptr_t sm = cat_mem::readv<std::uintptr_t>( smglob );
			if ( sm > 0x10000 && sm < 0x00007FFFFFFFFFFFULL && BvhLooksLikeSurfaceManager( sm ) )
			{
				if ( pattern_hit_out )
					*pattern_hit_out = hit;
				return sm;
			}
		}
	}
	return 0;
}

static void BvhParseThreadFn( )
{
	using namespace std::chrono_literals;
	for ( ;; )
	{
		try
		{
			if ( g_GameMem.attached_pid == 0 || !client )
			{
				std::this_thread::sleep_for( 500ms );
				continue;
			}

			if (tri_loader::TickFileLoader()) {
				const std::size_t n = g_world_bvh.count();
				g_dbg_last_extracted.store(static_cast<int>(n), std::memory_order_relaxed);
				if (n > 0)
					g_bvh_parse_successes.store(1, std::memory_order_relaxed);
				std::this_thread::sleep_for(g_world_bvh.valid() ? 8000ms : 1500ms);
				continue;
			}

			if (!g_world_bvh.valid()) {
				const std::string map = tri_loader::ReadMapName();
				if (!map.empty())
					MapTriExport_KickIfNeeded(map);
			}

			g_dbg_last_extracted.store(0, std::memory_order_relaxed);
		}
		catch (...)
		{
		}
		const auto wait_ms = g_world_bvh.valid()
		                         ? 8000ms
		                         : std::chrono::milliseconds(
		                               MapTriExportInProgress() ? tri_loader::k_map_check_export_ms
		                                                        : tri_loader::k_map_check_interval_ms);
		std::this_thread::sleep_for(wait_ms);
	}
}

} 

	namespace detail {

		static constexpr std::size_t k_inner_node_size{ 32 };
		static constexpr std::size_t k_outer_node_size{ 48 };

		struct inner_node_t
		{
			float min[ 3 ];
			std::uint32_t packed0;
			float max[ 3 ];
			std::uint32_t packed1;

			[[nodiscard]] std::uint32_t type( ) const { return packed0 >> 30; }
			[[nodiscard]] std::uint32_t payload( ) const { return packed0 & 0x3FFFFFFFu; }
		};

		struct hedge_t
		{
			std::uint8_t next;
			std::uint8_t twin;
			std::uint8_t vert;
			std::uint8_t face;
		};

		struct quat_t { float x, y, z, w; };
		struct mat3_t { float m[ 3 ][ 3 ]; };

		static mat3_t quat_to_matrix( const quat_t& q )
		{
			const auto xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
			const auto xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
			const auto wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;

			mat3_t m{};
			m.m[ 0 ][ 0 ] = 1 - 2 * ( yy + zz );
			m.m[ 0 ][ 1 ] = 2 * ( xy + wz );
			m.m[ 0 ][ 2 ] = 2 * ( xz - wy );
			m.m[ 1 ][ 0 ] = 2 * ( xy - wz );
			m.m[ 1 ][ 1 ] = 1 - 2 * ( xx + zz );
			m.m[ 1 ][ 2 ] = 2 * ( yz + wx );
			m.m[ 2 ][ 0 ] = 2 * ( xz + wy );
			m.m[ 2 ][ 1 ] = 2 * ( yz - wx );
			m.m[ 2 ][ 2 ] = 1 - 2 * ( xx + yy );
			return m;
		}

		static Vector3 rotate_point( const mat3_t& m, const Vector3& v )
		{
			return Vector3(
				m.m[ 0 ][ 0 ] * v.x + m.m[ 1 ][ 0 ] * v.y + m.m[ 2 ][ 0 ] * v.z,
				m.m[ 0 ][ 1 ] * v.x + m.m[ 1 ][ 1 ] * v.y + m.m[ 2 ][ 1 ] * v.z,
				m.m[ 0 ][ 2 ] * v.x + m.m[ 1 ][ 2 ] * v.y + m.m[ 2 ][ 2 ] * v.z
			);
		}

		static Vector3 transform_point( const mat3_t& rot, const float scale[ 3 ], const float pos[ 3 ], const Vector3& local )
		{
			const auto scaled = Vector3( local.x * scale[ 0 ], local.y * scale[ 1 ], local.z * scale[ 2 ] );
			const auto rotated = rotate_point( rot, scaled );
			return Vector3( rotated.x + pos[ 0 ], rotated.y + pos[ 1 ], rotated.z + pos[ 2 ] );
		}

		static bool extract_mesh( std::uintptr_t bvh_ptr, std::uintptr_t vert_ptr, std::uintptr_t tri_ptr, std::uint32_t node_count, const mat3_t& rot, const float scale[ 3 ], const float pos[ 3 ], std::uintptr_t mat_arr_ptr, std::int32_t mat_count, const std::vector<bvh::global_surface_entry>& global_table, const bvh::surface_info& default_surface, std::vector<bvh::triangle>& out )
		{
			if ( !bvh_ptr || !vert_ptr || !tri_ptr || node_count == 0 || node_count > 0x1000000 )
			{
				return false;
			}

			std::vector<std::uint8_t> bvh_buf( static_cast< std::size_t >( node_count ) * k_inner_node_size );
			cat_mem::read( bvh_ptr, bvh_buf.data( ), bvh_buf.size( ) );

			std::uint32_t min_tri = UINT32_MAX, max_tri = 0;
			std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
			std::vector<std::uint32_t> stack;
			stack.reserve( 256 );

			std::uint32_t cursor{ 0 };

			while ( true )
			{
				if ( cursor >= node_count )
				{
					if ( stack.empty( ) )
					{
						break;
					}

					cursor = stack.back( );
					stack.pop_back( );
					continue;
				}

				const auto node = reinterpret_cast< const inner_node_t* >( bvh_buf.data( ) + static_cast< std::size_t >( cursor ) * k_inner_node_size );
				const auto type = node->type( );
				const auto payload = node->payload( );

				if ( type == 3 )
				{
					if ( payload > 0 && payload < 0x1000000 )
					{
						ranges.push_back( { node->packed1, payload } );

						if ( node->packed1 < min_tri )
						{
							min_tri = node->packed1;
						}

						if ( node->packed1 + payload > max_tri )
						{
							max_tri = node->packed1 + payload;
						}
					}

					if ( stack.empty( ) )
					{
						break;
					}

					cursor = stack.back( );
					stack.pop_back( );
				}
				else
				{
					if ( payload == 0 )
					{
						if ( stack.empty( ) )
						{
							break;
						}

						cursor = stack.back( );
						stack.pop_back( );
						continue;
					}

					if ( cursor + payload < node_count )
					{
						stack.push_back( cursor + payload );
					}

					cursor++;
				}
			}

			if ( ranges.empty( ) || max_tri <= min_tri )
			{
				return false;
			}

			const auto total_tris = max_tri - min_tri;
			if ( total_tris > 0x1000000 )
			{
				return false;
			}

			std::vector<std::int32_t> indices( total_tris * 3 );
			cat_mem::read( tri_ptr + static_cast< std::uintptr_t >( min_tri ) * 12, indices.data( ), total_tris * 12 );

			std::int32_t max_vert{ 0 };
			for ( const auto idx : indices )
			{
				if ( idx > max_vert )
				{
					max_vert = idx;
				}
			}

			if ( max_vert <= 0 || max_vert > 0x1000000 )
			{
				return false;
			}

			const auto vert_count = static_cast< std::uint32_t >( max_vert + 1 );
			std::vector<float> vertices( vert_count * 3 );
			cat_mem::read( vert_ptr, vertices.data( ), static_cast< std::size_t >( vert_count ) * 12 );

			const bool has_materials = mat_arr_ptr > 0x10000 && mat_count > 0;
			std::vector<std::uint8_t> materials;

			if ( has_materials )
			{
				materials.resize( total_tris );
				cat_mem::read( mat_arr_ptr + static_cast< std::uintptr_t >( min_tri ), materials.data( ), total_tris );
			}

			const auto global_count = static_cast< int >( global_table.size( ) );
			const auto before = out.size( );

			for ( const auto& [start, count] : ranges )
			{
				for ( std::uint32_t i = 0; i < count; ++i )
				{
					const auto local_idx = start - min_tri + i;
					if ( local_idx >= total_tris )
					{
						continue;
					}

					auto surf = default_surface;

					if ( has_materials && local_idx < materials.size( ) )
					{
						const auto gi = materials[ local_idx ];
						if ( gi < global_count )
						{
							const auto& gs = global_table[ gi ];
							surf.penetration = gs.penetration_mod;
							surf.surface_type = gs.surface_type;
							surf.global_index = gi;
						}
					}

					const auto base = local_idx * 3;
					const auto i0 = indices[ base ];
					const auto i1 = indices[ static_cast< std::size_t >( base ) + 1 ];
					const auto i2 = indices[ static_cast< std::size_t >( base ) + 2 ];

					if ( i0 < 0 || i1 < 0 || i2 < 0 )
					{
						continue;
					}

					if ( static_cast< std::uint32_t >( i0 ) >= vert_count || static_cast< std::uint32_t >( i1 ) >= vert_count || static_cast< std::uint32_t >( i2 ) >= vert_count )
					{
						continue;
					}

					auto xf = [ & ]( std::int32_t vi ) -> Vector3 {
						return transform_point( rot, scale, pos,
							Vector3( vertices[ vi * 3 ], vertices[ vi * 3 + 1 ], vertices[ vi * 3 + 2 ] ) );
					};

					bvh::triangle tr{};
					tr.v0 = xf( i0 );
					tr.v1 = xf( i1 );
					tr.v2 = xf( i2 );
					tr.surface = surf;
					out.push_back( tr );
				}
			}

			return out.size( ) > before;
		}

		static bool extract_hull( std::uintptr_t hull_data, float uniform_scale, const bvh::surface_info& surface, std::vector<bvh::triangle>& out )
		{
			if ( !hull_data )
			{
				return false;
			}

			std::uint8_t hd[ 0x100 ]{};
			cat_mem::read( hull_data, hd, sizeof( hd ) );

			const auto vert_count = *reinterpret_cast< const std::int32_t* >( hd + 0x88 );
			const auto vert_ptr = *reinterpret_cast< const std::uintptr_t* >( hd + 0x90 );
			const auto hedge_count = *reinterpret_cast< const std::int32_t* >( hd + 0xa0 );
			const auto hedge_ptr = *reinterpret_cast< const std::uintptr_t* >( hd + 0xa8 );
			const auto face_count = *reinterpret_cast< const std::int32_t* >( hd + 0xb8 );
			const auto face_ptr = *reinterpret_cast< const std::uintptr_t* >( hd + 0xc0 );

			if ( vert_count <= 0 || vert_count > 0xffff )
			{
				return false;
			}

			if ( hedge_count <= 0 || hedge_count > 0xffff )
			{
				return false;
			}

			if ( face_count <= 0 || face_count > 0xffff )
			{
				return false;
			}

			if ( !vert_ptr || !hedge_ptr || !face_ptr )
			{
				return false;
			}

			std::vector<float> verts( vert_count * 3 );
			cat_mem::read( vert_ptr, verts.data( ), static_cast< std::size_t >( vert_count ) * 12 );

			std::vector<hedge_t> hedges( hedge_count );
			cat_mem::read( hedge_ptr, hedges.data( ), static_cast< std::size_t >( hedge_count ) * 4 );

			std::vector<std::uint8_t> faces( face_count );
			cat_mem::read( face_ptr, faces.data( ), face_count );

			const auto before = out.size( );

			for ( int fi = 0; fi < face_count; ++fi )
			{
				const auto start_he = faces[ fi ];
				if ( start_he >= hedge_count )
				{
					continue;
				}

				std::vector<int> face_verts;
				face_verts.reserve( 8 );

				auto he = start_he;
				auto safety{ 0 };

				do
				{
					if ( he >= hedge_count )
					{
						break;
					}

					face_verts.push_back( hedges[ he ].vert );
					he = hedges[ he ].next;
				} while ( he != start_he && ++safety < 64 );

				if ( face_verts.size( ) < 3 )
				{
					continue;
				}

				auto vert = [ & ]( int vi ) -> Vector3
					{
						if ( vi < 0 || vi >= vert_count )
						{
							return Vector3();
						}

						return Vector3(
							verts[ vi * 3 ] * uniform_scale,
							verts[ vi * 3 + 1 ] * uniform_scale,
							verts[ vi * 3 + 2 ] * uniform_scale
						);
					};

				const auto v0 = vert( face_verts[ 0 ] );

				for ( std::size_t i = 1; i + 1 < face_verts.size( ); ++i )
				{
					bvh::triangle tr{};
					tr.v0 = v0;
					tr.v1 = vert( face_verts[ i ] );
					tr.v2 = vert( face_verts[ i + 1 ] );
					tr.surface = surface;
					out.push_back( tr );
				}
			}

			return out.size( ) > before;
		}

		static void process_shape( std::uintptr_t shape_body, std::uintptr_t hull_vtable, std::uintptr_t mesh_vtable, const std::vector<bvh::global_surface_entry>& global_table, std::vector<bvh::triangle>& out )
		{
			const auto vtable = cat_mem::readv<std::uintptr_t>( shape_body );

			if ( vtable == hull_vtable )
			{
				const auto hull_data = cat_mem::readv<std::uintptr_t>( shape_body + 0xb8 );
				if ( hull_data > 0x10000 && hull_data < 0x7fffffffffff )
				{
					const auto scale = cat_mem::readv<float>( shape_body + 0xb0 );

					bvh::surface_info hull_surface{};
					hull_surface.penetration = cat_mem::readv<float>( shape_body + 0x28 );

					extract_hull( hull_data, ( scale > 0.0f && std::isfinite( scale ) ) ? scale : 1.0f, hull_surface, out );
				}

				return;
			}

			if ( vtable != mesh_vtable )
			{
				return;
			}

			const auto mesh_data = cat_mem::readv<std::uintptr_t>( shape_body + 0xc0 );
			if ( !mesh_data )
			{
				return;
			}

			bvh::surface_info default_surface{};
			default_surface.penetration = cat_mem::readv<float>( shape_body + 0x28 );

			const auto default_damage = cat_mem::readv<float>( shape_body + 0x2c );
			if ( default_damage < 0.0f )
			{
				return;
			}

			std::uint8_t md[ 0xA0 ]{};
			cat_mem::read( mesh_data, md, sizeof( md ) );

			const auto mat_count = *reinterpret_cast< const std::int32_t* >( md + 0x90 );
			const auto mat_arr_ptr = *reinterpret_cast< const std::uintptr_t* >( md + 0x98 );
			const bool has_materials = mat_arr_ptr > 0x10000 && mat_count > 0;

			float scale[ 3 ]{};
			cat_mem::read( shape_body + 0xB0, scale, 12 );

			if ( scale[ 0 ] == 0.0f && scale[ 1 ] == 0.0f && scale[ 2 ] == 0.0f )
			{
				return;
			}

			if ( !std::isfinite( scale[ 0 ] ) || !std::isfinite( scale[ 1 ] ) || !std::isfinite( scale[ 2 ] ) )
			{
				return;
			}

			float world_pos[ 3 ]{};
			cat_mem::read( shape_body + 0x100, world_pos, 12 );

			detail::quat_t quat{};
			cat_mem::read( shape_body + 0x130, &quat, sizeof( quat ) );

			const auto ql = quat.x * quat.x + quat.y * quat.y + quat.z * quat.z + quat.w * quat.w;
			if ( ql < 0.5f || ql > 1.5f )
			{
				quat = { 0, 0, 0, 1 };
			}

			const auto rot = quat_to_matrix( quat );
			const auto bvh_ptr = *reinterpret_cast< const std::uintptr_t* >( md + 0x20 );
			const auto vert_ptr = *reinterpret_cast< const std::uintptr_t* >( md + 0x38 );
			const auto tri_ptr = *reinterpret_cast< const std::uintptr_t* >( md + 0x50 );

			auto node_count{ 0u };
			for ( auto c : { *reinterpret_cast< const std::int32_t* >( md + 0x28 ), *reinterpret_cast< const std::int32_t* >( md + 0x30 ), *reinterpret_cast< const std::int32_t* >( md + 0x48 ), *reinterpret_cast< const std::int32_t* >( md + 0x58 ) } )
			{
				if ( c > 0 && c < 0x1000000 )
				{
					node_count = static_cast< std::uint32_t >( c );
					break;
				}
			}

			if ( node_count > 0 )
			{
				extract_mesh( bvh_ptr, vert_ptr, tri_ptr, node_count, rot, scale, world_pos, mat_arr_ptr, mat_count, global_table, default_surface, out );
			}
		}
		
	} 

	void bvh::parse( )
	{
		const std::uintptr_t cli = cat_mem::client_module( );
		const std::uintptr_t vph = cat_mem::vphysics_module( );

		std::uintptr_t vphys2_world = BvhResolveWorldFromVPhysMod( vph );

		std::uintptr_t trace_anchor = 0;
		if ( !vphys2_world )
			vphys2_world = BvhResolveVPhys2World( cli, &trace_anchor );

		g_dbg_pattern_trace.store( trace_anchor, std::memory_order_relaxed );
		g_dbg_vphys2_world.store( vphys2_world, std::memory_order_relaxed );

		if ( !vphys2_world )
		{
			return;
		}

		std::uintptr_t surface_hit = 0;
		const std::uintptr_t surface_manager = BvhResolveSurfaceManager( cli, &surface_hit );
		g_dbg_pattern_surface.store( surface_hit, std::memory_order_relaxed );

		std::vector<global_surface_entry> global_table;
		if ( surface_manager )
		{
			const auto array_base = cat_mem::readv<std::uintptr_t>( surface_manager + 40 );
			if ( array_base )
			{
				std::int32_t surface_count{ 0 };

				for ( const auto off : { 32, 36, 24, 28, 48 } )
				{
					const auto candidate = cat_mem::readv<std::int32_t>( surface_manager + off );
					if ( candidate > 0 )
					{
						surface_count = candidate;
						break;
					}
				}

				if ( surface_count <= 0 )
				{
					for ( int i = 0; i < 1024; ++i )
					{
						global_surface_entry sd{};
						cat_mem::read( array_base + static_cast< std::size_t >( i ) * 32, &sd, sizeof( sd ) );

						if ( sd.penetration_mod == 0.0f && sd.surface_type == 0 && sd.unk_00 == 0.0f )
						{
							if ( surface_count > 0 && i - surface_count > 8 )
							{
								break;
							}

							continue;
						}

						surface_count = i + 1;
					}
				}

				if ( surface_count )
				{
					global_table.resize( surface_count );
					cat_mem::read( array_base, global_table.data( ), static_cast< std::size_t >( surface_count ) * sizeof( global_surface_entry ) );
				}
			}
		}

		const auto inner_world = cat_mem::readv<std::uintptr_t>( vphys2_world + g_world_inner_off );
		if ( !inner_world )
		{
			return;
		}

		const auto body_array = cat_mem::readv<std::uintptr_t>( inner_world + g_world_arr_off );
		if ( !body_array )
		{
			return;
		}

		const auto body_count = cat_mem::readv<std::int32_t>( body_array + g_world_cnt_off );
		g_dbg_body_count.store( body_count, std::memory_order_relaxed );
		if ( !body_count )
		{
			return;
		}

		const auto hull_vtable = cat_mem::find_vtable( cat_mem::vphysics_module(), "CRnHullShape" );
		const auto mesh_vtable = cat_mem::find_vtable( cat_mem::vphysics_module(), "CRnMeshShape" );

		if ( !hull_vtable || !mesh_vtable )
		{
			return;
		}

		std::vector<triangle> fresh;
		fresh.reserve( 262144 );

		for ( std::int32_t body_idx = 0; body_idx < body_count; ++body_idx )
		{
			const auto body = body_array + static_cast< std::uintptr_t >( body_idx ) * 88;
			const auto bvh_root = cat_mem::readv<std::int32_t>( body );
			const auto bvh_nodes_ptr = cat_mem::readv<std::uintptr_t>( body + 0x18 );

			if ( !bvh_nodes_ptr )
			{
				continue;
			}

			const auto femboys = cat_mem::readv<std::uint32_t>( body + 0x40 );
			if ( femboys != 2 )
			{
				continue;
			}

			if ( bvh_root >= 0 )
			{
				const auto count_a = static_cast< std::uint32_t >( bvh_root + 1 );
				const auto count_b = static_cast< std::uint32_t >( cat_mem::readv<std::int32_t>( body + 0x08 ) );
				const auto count_c = static_cast< std::uint32_t >( cat_mem::readv<std::int32_t>( body + 0x10 ) );
				const auto outer_node_count =
					count_a >= count_b && count_a >= count_c ? count_a
					: (count_b >= count_c ? count_b : count_c);

				if ( outer_node_count > 0x100000 )
				{
					continue;
				}

				std::vector<std::uint8_t> outer_buf( outer_node_count * detail::k_outer_node_size );
				cat_mem::read( bvh_nodes_ptr, outer_buf.data( ), outer_buf.size( ) );

				std::vector<std::uintptr_t> leaves;
				leaves.reserve( 256 );

				std::vector<std::int32_t> outer_stack;
				outer_stack.reserve( 128 );
				outer_stack.push_back( bvh_root );

				while ( !outer_stack.empty( ) )
				{
					const auto idx = outer_stack.back( );
					outer_stack.pop_back( );

					if ( idx < 0 || static_cast< std::uint32_t >( idx ) >= outer_node_count )
					{
						continue;
					}

					const auto node = outer_buf.data( ) + static_cast< std::uintptr_t >( idx ) * detail::k_outer_node_size;
					const auto left = *reinterpret_cast< const std::int32_t* >( node + 12 );

					if ( left == -1 )
					{
						const auto shape_ptr = *reinterpret_cast< const std::uintptr_t* >( node + 0x28 );
						if ( shape_ptr )
						{
							leaves.push_back( shape_ptr );
						}
					}
					else
					{
						const auto right = *reinterpret_cast< const std::int32_t* >( node + 28 );
						if ( left >= 0 )
						{
							outer_stack.push_back( left );
						}

						if ( right >= 0 )
						{
							outer_stack.push_back( right );
						}
					}
				}

				std::unordered_set<std::uintptr_t> seen;

				for ( const auto shape : leaves )
				{
					if ( seen.count( shape ) )
					{
						continue;
					}

					seen.insert( shape );
					detail::process_shape( shape, hull_vtable, mesh_vtable, global_table, fresh );
				}
			}
			else
			{
				const auto shape = cat_mem::readv<std::uintptr_t>( body + 0x28 );
				if ( shape )
				{
					detail::process_shape( shape, hull_vtable, mesh_vtable, global_table, fresh );
				}
			}
		}

		{
			std::unique_lock lock( this->m_mutex );
			this->m_triangles = std::move( fresh );
			this->rebuild_accel_unlocked( );
		}
	}

	void bvh::clear( )
	{
		std::unique_lock lock( this->m_mutex );
		this->m_triangles.clear( );
		this->m_nodes.clear( );
		this->m_indices.clear( );
		this->m_tri_bounds.clear( );
		this->m_centroids.clear( );
	}

	bvh::trace_result bvh::trace_ray( const Vector3& start, const Vector3& end, std::int32_t exclude_tri ) const
	{
		std::shared_lock lock( this->m_mutex );
		trace_result result{};
		result.end_pos = end;

		if ( this->m_nodes.empty( ) )
		{
			return result;
		}

		const auto dx = end.x - start.x;
		const auto dy = end.y - start.y;
		const auto dz = end.z - start.z;
		const auto max_dist = std::sqrt( dx * dx + dy * dy + dz * dz );

		if ( max_dist < 1e-8f )
		{
			return result;
		}

		const auto inv_dist = 1.0f / max_dist;
		const float dir[ 3 ]{ dx * inv_dist, dy * inv_dist, dz * inv_dist };
		const float origin[ 3 ]{ start.x, start.y, start.z };
		const float inv_dir[ 3 ]{ std::abs( dir[ 0 ] ) > 1e-8f ? 1.0f / dir[ 0 ] : ( dir[ 0 ] >= 0 ? 1e12f : -1e12f ), std::abs( dir[ 1 ] ) > 1e-8f ? 1.0f / dir[ 1 ] : ( dir[ 1 ] >= 0 ? 1e12f : -1e12f ), std::abs( dir[ 2 ] ) > 1e-8f ? 1.0f / dir[ 2 ] : ( dir[ 2 ] >= 0 ? 1e12f : -1e12f ) };

		auto closest_t = max_dist;

		std::int32_t stack[ 128 ]{};
		std::int32_t sp{ 0 };
		stack[ 0 ] = 0;

		while ( sp >= 0 )
		{
			const auto& node = this->m_nodes[ stack[ sp-- ] ];
			if ( !node.bounds.intersects_ray( origin, inv_dir, closest_t ) )
			{
				continue;
			}

			if ( node.left == -1 )
			{
				for ( std::int32_t i = node.tri_start; i < node.tri_start + node.tri_count; ++i )
				{
					const auto ti = this->m_indices[ i ];
					if ( ti == exclude_tri )
					{
						continue;
					}

					const auto& tri = this->m_triangles[ ti ];

					const auto e1x = tri.v1.x - tri.v0.x, e1y = tri.v1.y - tri.v0.y, e1z = tri.v1.z - tri.v0.z;
					const auto e2x = tri.v2.x - tri.v0.x, e2y = tri.v2.y - tri.v0.y, e2z = tri.v2.z - tri.v0.z;

					const auto hx = dir[ 1 ] * e2z - dir[ 2 ] * e2y;
					const auto hy = dir[ 2 ] * e2x - dir[ 0 ] * e2z;
					const auto hz = dir[ 0 ] * e2y - dir[ 1 ] * e2x;
					const auto a = e1x * hx + e1y * hy + e1z * hz;

					if ( a > -1e-8f && a < 1e-8f )
					{
						continue;
					}

					const auto f = 1.0f / a;
					const auto sx = origin[ 0 ] - tri.v0.x, sy = origin[ 1 ] - tri.v0.y, sz = origin[ 2 ] - tri.v0.z;
					const auto u = f * ( sx * hx + sy * hy + sz * hz );

					if ( u < 0.0f || u > 1.0f )
					{
						continue;
					}

					const auto qx = sy * e1z - sz * e1y, qy = sz * e1x - sx * e1z, qz = sx * e1y - sy * e1x;
					const auto v = f * ( dir[ 0 ] * qx + dir[ 1 ] * qy + dir[ 2 ] * qz );

					if ( v < 0.0f || u + v > 1.0f )
					{
						continue;
					}

					const auto t = f * ( e2x * qx + e2y * qy + e2z * qz );

					if ( t > 1e-5f && t < closest_t )
					{
						closest_t = t;
						result.hit = true;
						result.fraction = t / max_dist;
						result.distance = t;
						result.triangle_index = ti;
						result.surface = tri.surface;
						result.end_pos = Vector3( origin[ 0 ] + dir[ 0 ] * t, origin[ 1 ] + dir[ 1 ] * t, origin[ 2 ] + dir[ 2 ] * t );

						const auto nx = e1y * e2z - e1z * e2y;
						const auto ny = e1z * e2x - e1x * e2z;
						const auto nz = e1x * e2y - e1y * e2x;
						const auto nl = std::sqrt( nx * nx + ny * ny + nz * nz );

						if ( nl > 1e-8f )
						{
							const auto inv_nl = 1.0f / nl;
							result.normal = Vector3( nx * inv_nl, ny * inv_nl, nz * inv_nl );
						}
					}
				}
			}
			else if ( sp + 2 < 127 )
			{
				stack[ ++sp ] = node.right;
				stack[ ++sp ] = node.left;
			}
		}

		return result;
	}

	std::vector<bvh::hit_entry> bvh::trace_ray_all( const Vector3& start, const Vector3& end ) const
	{
		std::shared_lock lock( this->m_mutex );
		std::vector<hit_entry> hits;

		if ( this->m_nodes.empty( ) )
		{
			return hits;
		}

		const auto dx = end.x - start.x;
		const auto dy = end.y - start.y;
		const auto dz = end.z - start.z;
		const auto max_dist = std::sqrt( dx * dx + dy * dy + dz * dz );

		if ( max_dist < 1e-8f )
		{
			return hits;
		}

		const auto inv_dist = 1.0f / max_dist;
		const float dir[ 3 ]{ dx * inv_dist, dy * inv_dist, dz * inv_dist };
		const float origin[ 3 ]{ start.x, start.y, start.z };
		const float inv_dir[ 3 ]{ std::abs( dir[ 0 ] ) > 1e-8f ? 1.0f / dir[ 0 ] : ( dir[ 0 ] >= 0 ? 1e12f : -1e12f ), std::abs( dir[ 1 ] ) > 1e-8f ? 1.0f / dir[ 1 ] : ( dir[ 1 ] >= 0 ? 1e12f : -1e12f ), std::abs( dir[ 2 ] ) > 1e-8f ? 1.0f / dir[ 2 ] : ( dir[ 2 ] >= 0 ? 1e12f : -1e12f ) };

		std::int32_t stack[ 128 ]{};
		std::int32_t sp{ 0 };
		stack[ 0 ] = 0;

		while ( sp >= 0 )
		{
			const auto& node = this->m_nodes[ stack[ sp-- ] ];
			if ( !node.bounds.intersects_ray( origin, inv_dir, max_dist ) )
			{
				continue;
			}

			if ( node.left == -1 )
			{
				for ( std::int32_t i = node.tri_start; i < node.tri_start + node.tri_count; ++i )
				{
					const auto ti = this->m_indices[ i ];
					const auto& tri = this->m_triangles[ ti ];

					const auto e1x = tri.v1.x - tri.v0.x, e1y = tri.v1.y - tri.v0.y, e1z = tri.v1.z - tri.v0.z;
					const auto e2x = tri.v2.x - tri.v0.x, e2y = tri.v2.y - tri.v0.y, e2z = tri.v2.z - tri.v0.z;

					const auto hx = dir[ 1 ] * e2z - dir[ 2 ] * e2y;
					const auto hy = dir[ 2 ] * e2x - dir[ 0 ] * e2z;
					const auto hz = dir[ 0 ] * e2y - dir[ 1 ] * e2x;
					const auto a = e1x * hx + e1y * hy + e1z * hz;

					if ( a > -1e-8f && a < 1e-8f )
					{
						continue;
					}

					const auto f = 1.0f / a;
					const auto sx = origin[ 0 ] - tri.v0.x, sy = origin[ 1 ] - tri.v0.y, sz = origin[ 2 ] - tri.v0.z;
					const auto u = f * ( sx * hx + sy * hy + sz * hz );

					if ( u < 0.0f || u > 1.0f )
					{
						continue;
					}

					const auto qx = sy * e1z - sz * e1y, qy = sz * e1x - sx * e1z, qz = sx * e1y - sy * e1x;
					const auto v = f * ( dir[ 0 ] * qx + dir[ 1 ] * qy + dir[ 2 ] * qz );

					if ( v < 0.0f || u + v > 1.0f )
					{
						continue;
					}

					const auto t = f * ( e2x * qx + e2y * qy + e2z * qz );

					if ( t > 1e-5f && t < max_dist )
					{
						auto nx = e1y * e2z - e1z * e2y;
						auto ny = e1z * e2x - e1x * e2z;
						auto nz = e1x * e2y - e1y * e2x;
						const auto nl = std::sqrt( nx * nx + ny * ny + nz * nz );

						if ( nl > 1e-8f )
						{
							const auto inv_nl = 1.0f / nl;
							nx *= inv_nl;
							ny *= inv_nl;
							nz *= inv_nl;
						}

						const auto ndot = nx * dir[ 0 ] + ny * dir[ 1 ] + nz * dir[ 2 ];

						hit_entry hit{};
						hit.distance = t;
						hit.fraction = t / max_dist;
						hit.position = Vector3( origin[ 0 ] + dir[ 0 ] * t, origin[ 1 ] + dir[ 1 ] * t, origin[ 2 ] + dir[ 2 ] * t );
						hit.normal = Vector3( nx, ny, nz );
						hit.surface = tri.surface;
						hit.triangle_index = ti;
						hit.is_enter = ( ndot < 0.0f );

						hits.push_back( hit );
					}
				}
			}
			else if ( sp + 2 < 127 )
			{
				stack[ ++sp ] = node.right;
				stack[ ++sp ] = node.left;
			}
		}

		std::sort( hits.begin( ), hits.end( ), [ ]( const hit_entry& a, const hit_entry& b ) { return a.distance < b.distance; } );

		return hits;
	}

	std::vector<bvh::penetration_segment> bvh::build_segments( const std::vector<hit_entry>& hits, float ray_length ) const
	{
		std::vector<penetration_segment> segments;

		if ( hits.empty( ) )
		{
			return segments;
		}

		auto sorted = hits;

		for ( std::size_t i = 1; i < sorted.size( ); ++i )
		{
			auto& prev = sorted[ i - 1 ];
			auto& curr = sorted[ i ];

			if ( !curr.is_enter && prev.is_enter && ( curr.fraction - prev.fraction ) * ray_length <= ( 1.0f / 512.0f ) )
			{
				std::swap( prev, curr );
			}
		}

		auto was_exit{ true };
		auto seg_enter_idx{ -1 };
		auto seg_enter_fraction{ 0.0f };

		for ( std::size_t i = 0; i < sorted.size( ); ++i )
		{
			const auto& hit = sorted[ i ];
			const bool is_exit = !hit.is_enter;

			if ( is_exit != was_exit )
			{
				was_exit = is_exit;

				if ( !is_exit )
				{
					if ( seg_enter_idx >= 0 && i > 0 )
					{
						const auto& exit_hit = sorted[ i - 1 ];

						penetration_segment seg{};
						seg.enter_fraction = sorted[ seg_enter_idx ].fraction;
						seg.exit_fraction = exit_hit.fraction;
						seg.enter_distance = sorted[ seg_enter_idx ].distance;
						seg.exit_distance = exit_hit.distance;
						seg.enter_pos = sorted[ seg_enter_idx ].position;
						seg.exit_pos = exit_hit.position;
						seg.enter_surface = sorted[ seg_enter_idx ].surface;
						seg.exit_surface = exit_hit.surface;
						seg.thickness = exit_hit.distance - sorted[ seg_enter_idx ].distance;
						seg.min_pen_mod = sorted[ seg_enter_idx ].surface.penetration;

						if ( seg.thickness > 0.0f )
						{
							segments.push_back( seg );
						}
					}

					seg_enter_idx = static_cast< int >( i );
					seg_enter_fraction = hit.fraction;
				}
			}
		}

		if ( seg_enter_idx >= 0 )
		{
			const auto& enter_hit = sorted[ seg_enter_idx ];
			const auto& last_hit = sorted.back( );

			penetration_segment seg{};
			seg.enter_fraction = enter_hit.fraction;
			seg.exit_fraction = last_hit.fraction;
			seg.enter_distance = enter_hit.distance;
			seg.exit_distance = last_hit.distance;
			seg.enter_pos = enter_hit.position;
			seg.exit_pos = last_hit.position;
			seg.enter_surface = enter_hit.surface;
			seg.exit_surface = last_hit.surface;
			seg.thickness = last_hit.distance - enter_hit.distance;

			if ( seg.thickness < 1.0f )
			{
				seg.thickness = 1.0f;
			}

			seg.min_pen_mod = enter_hit.surface.penetration;

			segments.push_back( seg );
		}

		if ( segments.empty( ) && !sorted.empty( ) )
		{
			for ( std::size_t i = 0; i + 1 < sorted.size( ); i += 2 )
			{
				penetration_segment seg{};
				seg.enter_fraction = sorted[ i ].fraction;
				seg.exit_fraction = sorted[ i + 1 ].fraction;
				seg.enter_distance = sorted[ i ].distance;
				seg.exit_distance = sorted[ i + 1 ].distance;
				seg.enter_pos = sorted[ i ].position;
				seg.exit_pos = sorted[ i + 1 ].position;
				seg.enter_surface = sorted[ i ].surface;
				seg.exit_surface = sorted[ i + 1 ].surface;
				seg.thickness = sorted[ i + 1 ].distance - sorted[ i ].distance;

				if ( seg.thickness < 1.0f )
				{
					seg.thickness = 1.0f;
				}

				seg.min_pen_mod = sorted[ i ].surface.penetration;
				segments.push_back( seg );
			}

			if ( sorted.size( ) % 2 == 1 )
			{
				const auto& h = sorted.back( );
				penetration_segment seg{};
				seg.enter_fraction = h.fraction;
				seg.exit_fraction = h.fraction;
				seg.enter_distance = h.distance;
				seg.exit_distance = h.distance + 1.0f;
				seg.enter_pos = h.position;
				seg.exit_pos = h.position;
				seg.enter_surface = h.surface;
				seg.exit_surface = h.surface;
				seg.thickness = 1.0f;
				seg.min_pen_mod = h.surface.penetration;
				segments.push_back( seg );
			}
		}

		return segments;
	}

	const std::vector<bvh::triangle>& bvh::triangles( ) const
	{
		return this->m_triangles;
	}

	std::size_t bvh::count( ) const
	{
		std::shared_lock lock( this->m_mutex );
		return this->m_triangles.size( );
	}

	bool bvh::valid( ) const
	{
		std::shared_lock lock( this->m_mutex );
		return !this->m_triangles.empty( ) && !this->m_nodes.empty( );
	}

	void bvh::aabb::expand( const Vector3& p )
	{
		if ( p.x < this->mins[ 0 ] )
		{
			this->mins[ 0 ] = p.x;
		}

		if ( p.y < this->mins[ 1 ] )
		{
			this->mins[ 1 ] = p.y;
		}

		if ( p.z < this->mins[ 2 ] )
		{
			this->mins[ 2 ] = p.z;
		}

		if ( p.x > this->maxs[ 0 ] )
		{
			this->maxs[ 0 ] = p.x;
		}

		if ( p.y > this->maxs[ 1 ] )
		{
			this->maxs[ 1 ] = p.y;
		}

		if ( p.z > this->maxs[ 2 ] )
		{
			this->maxs[ 2 ] = p.z;
		}
	}

	void bvh::aabb::expand( const aabb& o )
	{
		for ( int i = 0; i < 3; ++i )
		{
			if ( o.mins[ i ] < this->mins[ i ] )
			{
				this->mins[ i ] = o.mins[ i ];
			}

			if ( o.maxs[ i ] > this->maxs[ i ] )
			{
				this->maxs[ i ] = o.maxs[ i ];
			}
		}
	}

	int bvh::aabb::longest_axis( ) const
	{
		const auto ex = this->maxs[ 0 ] - this->mins[ 0 ];
		const auto ey = this->maxs[ 1 ] - this->mins[ 1 ];
		const auto ez = this->maxs[ 2 ] - this->mins[ 2 ];

		if ( ex >= ey && ex >= ez )
		{
			return 0;
		}

		if ( ey >= ez )
		{
			return 1;
		}

		return 2;
	}

	bool bvh::aabb::intersects_ray( const float origin[ 3 ], const float inv_dir[ 3 ], float max_t ) const
	{
		auto tmin{ 0.0f };
		auto tmax = max_t;

		for ( int i = 0; i < 3; ++i )
		{
			auto t0 = ( this->mins[ i ] - origin[ i ] ) * inv_dir[ i ];
			auto t1 = ( this->maxs[ i ] - origin[ i ] ) * inv_dir[ i ];

			if ( inv_dir[ i ] < 0.0f )
			{
				const auto tmp = t0;
				t0 = t1;
				t1 = tmp;
			}

			if ( t0 > tmin )
			{
				tmin = t0;
			}

			if ( t1 < tmax )
			{
				tmax = t1;
			}

			if ( tmax < tmin )
			{
				return false;
			}
		}

		return true;
	}

	void bvh::rebuild_accel_unlocked( )
	{
		this->m_nodes.clear( );
		this->m_indices.clear( );
		this->m_tri_bounds.clear( );
		this->m_centroids.clear( );

		const auto tri_count = static_cast< std::int32_t >( this->m_triangles.size( ) );
		if ( tri_count == 0 )
		{
			return;
		}

		this->m_indices.resize( tri_count );
		this->m_tri_bounds.resize( tri_count );
		this->m_centroids.resize( static_cast< std::size_t >( tri_count ) * 3 );

		for ( std::int32_t i = 0; i < tri_count; ++i )
		{
			this->m_indices[ i ] = i;

			aabb bb{};
			bb.expand( this->m_triangles[ i ].v0 );
			bb.expand( this->m_triangles[ i ].v1 );
			bb.expand( this->m_triangles[ i ].v2 );
			this->m_tri_bounds[ i ] = bb;

			const auto ci = static_cast< std::size_t >( i ) * 3;
			this->m_centroids[ ci ] = ( bb.mins[ 0 ] + bb.maxs[ 0 ] ) * 0.5f;
			this->m_centroids[ ci + 1 ] = ( bb.mins[ 1 ] + bb.maxs[ 1 ] ) * 0.5f;
			this->m_centroids[ ci + 2 ] = ( bb.mins[ 2 ] + bb.maxs[ 2 ] ) * 0.5f;
		}

		this->m_nodes.reserve( static_cast< std::size_t >( tri_count ) * 2 );
		this->build_recursive( 0, tri_count, 0 );
	}

	std::int32_t bvh::build_recursive( std::int32_t start, std::int32_t end, std::int32_t depth )
	{
		const auto node_idx = static_cast< std::int32_t >( this->m_nodes.size( ) );
		this->m_nodes.push_back( {} );

		auto& node = this->m_nodes[ node_idx ];
		const auto count = end - start;

		for ( std::int32_t i = start; i < end; ++i )
		{
			node.bounds.expand( this->m_tri_bounds[ this->m_indices[ i ] ] );
		}

		if ( count <= k_max_leaf_tris || depth >= k_max_depth )
		{
			node.tri_start = start;
			node.tri_count = count;
			return node_idx;
		}

		aabb centroid_bounds{};
		for ( std::int32_t i = start; i < end; ++i )
		{
			const auto ci = static_cast< std::size_t >( this->m_indices[ i ] ) * 3;
			centroid_bounds.expand( Vector3( this->m_centroids[ ci ], this->m_centroids[ ci + 1 ], this->m_centroids[ ci + 2 ] ) );
		}

		const auto axis = centroid_bounds.longest_axis( );
		const auto mid = ( centroid_bounds.mins[ axis ] + centroid_bounds.maxs[ axis ] ) * 0.5f;

		auto partition_point = std::partition( this->m_indices.begin( ) + start, this->m_indices.begin( ) + end, [ & ]( std::int32_t idx ) { return this->m_centroids[ static_cast< std::size_t >( idx ) * 3 + axis ] < mid; } );
		auto split = static_cast< std::int32_t >( partition_point - this->m_indices.begin( ) );

		if ( split == start || split == end )
		{
			split = start + count / 2;
		}

		node.left = this->build_recursive( start, split, depth + 1 );
		this->m_nodes[ node_idx ].right = this->build_recursive( split, end, depth + 1 );

		return node_idx;
	}

	void EnsureWorldBvhLoadThread( ) noexcept
	{
		TickWorldBvhParse( );
	}

	bool TryLoadWorldMeshNow( ) noexcept
	{
		try
		{
			return tri_loader::TickFileLoader( );
		}
		catch ( ... )
		{
			return false;
		}
	}

	void TickWorldBvhParse( )
	{
		if ( g_bvh_thread_started.exchange( true ) )
			return;
		
		HANDLE th = CreateThread( nullptr, 0,
		    []( LPVOID ) -> DWORD {
			    BvhParseThreadFn( );
			    return 0;
		    },
		    nullptr, 0, nullptr );
		if ( th )
			CloseHandle( th );
	}

	uintptr_t DbgClientModule( ) { return g_dbg_client_module.load( ); }
	uintptr_t DbgVphysModule( ) { return g_dbg_vphys_module.load( ); }
	uintptr_t DbgPatternTrace( ) { return g_dbg_pattern_trace.load( ); }
	uintptr_t DbgPatternSurf( ) { return g_dbg_pattern_surface.load( ); }
	uintptr_t DbgVphys2World( ) { return g_dbg_vphys2_world.load( ); }
	int DbgBodyCount( ) { return g_dbg_body_count.load( ); }
	uint32_t DbgWorldVtableFound( ) { return g_dbg_world_vtable_found.load( ); }
	uint32_t DbgParseAttempts( ) { return g_bvh_parse_attempts.load( ); }
	uint32_t DbgParseSuccess( ) { return g_bvh_parse_successes.load( ); }
	int DbgLastExtracted( ) { return g_dbg_last_extracted.load( ); }

} 
