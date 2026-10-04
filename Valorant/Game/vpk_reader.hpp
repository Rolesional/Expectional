#pragma once
/**
 * Source2 VPK v2 okuyucu + binary KV3 / LZ4 decompressor.
 * CS2 pak01_dir.vpk icinden .vphys_c dosyasini cikartir,
 * Source2 resource header'i atlar, DATA blogu icindeki
 * binary KV3'u (LZ4 sikistirmali veya sikisilmamis) cözer.
 */

#include "globals.hpp"

#include <Windows.h>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <map>
#include <string>
#include <vector>

namespace vpk_reader {

// ===========================================================================
// Minimal LZ4 block decompressor  (block format, NOT frame format)
// ===========================================================================
static bool Lz4BlockDecompress(
	const std::uint8_t* src, std::size_t src_size,
	std::uint8_t*       dst, std::size_t dst_cap,
	std::size_t& out_written )
{
	std::size_t si = 0, di = 0;
	while ( si < src_size )
	{
		const std::uint8_t token = src[ si++ ];

		// --- literals ---
		std::size_t lit_len = token >> 4;
		if ( lit_len == 15 )
		{
			std::uint8_t extra;
			do
			{
				if ( si >= src_size ) goto done;
				extra = src[ si++ ];
				lit_len += extra;
			} while ( extra == 255 );
		}
		if ( di + lit_len > dst_cap || si + lit_len > src_size ) goto done;
		std::memcpy( dst + di, src + si, lit_len );
		di += lit_len;
		si += lit_len;

		if ( si >= src_size ) break; // last sequence: no match part

		// --- match ---
		if ( si + 2 > src_size ) goto done;
		std::uint16_t offset;
		std::memcpy( &offset, src + si, 2 );
		si += 2;
		if ( offset == 0 || offset > di ) goto done;

		std::size_t match_len = static_cast<std::size_t>( token & 0x0F ) + 4;
		if ( match_len == 4 + 15 )
		{
			std::uint8_t extra;
			do
			{
				if ( si >= src_size ) goto done;
				extra = src[ si++ ];
				match_len += extra;
			} while ( extra == 255 );
		}
		if ( di + match_len > dst_cap ) goto done;

		{
			std::size_t ms = di - offset;
			for ( std::size_t k = 0; k < match_len; ++k )
				dst[ di++ ] = dst[ ms + k ];
		}
	}
done:
	out_written = di;
	return di > 0;
}

// ===========================================================================
// Source2 binary KV3  —  binary blob ayiklayici
//
// Desteklenen encoding'ler:
//   ACD982D8-CFD5-8152-43FC-9E75D6CC4A73  (BINARY_BLOCK_LZ4  – blok-basi LZ4)
//   7C2ED647-0E60-4247-B2AD-D7FE86F0FDB4  (BINARY_BLOCK_COMP – tek LZ4 blok)
//   diger                                 (uncompressed denenir)
// ===========================================================================

// Encoding GUID baytlari (LE)
static constexpr std::uint8_t k_enc_lz4[ 16 ] = {
	0xD8, 0x82, 0xD9, 0xAC, 0xD5, 0xCF, 0x52, 0x81,
	0x43, 0xFC, 0x9E, 0x75, 0xD6, 0xCC, 0x4A, 0x73
};
static constexpr std::uint8_t k_enc_comp[ 16 ] = {
	0x47, 0xD6, 0x2E, 0x7C, 0x60, 0x0E, 0x47, 0x42,
	0xB2, 0xAD, 0xD7, 0xFE, 0x86, 0xF0, 0xFD, 0xB4
};

// ----------------------------------------------------------------------------
// Binary bytes bolumunu ayikla; binary_bytes icindeki hedef blob'u don
// (m_Triangles veya m_Vertices icin gerekli ham baytlari BulBlob uzerinden alirsin)
// Bu fonksiyon binary_bytes + ints + doubles'in tamami icin geri donen vektoru doldurur.
// ----------------------------------------------------------------------------
static std::vector<std::uint8_t> ExtractBinaryBytesSection(
	const std::uint8_t* kv3, std::size_t kv3_size )
{
	// binary KV3 magic: 'V','K','V',0x03
	if ( kv3_size < 40 || kv3[ 0 ] != 'V' || kv3[ 1 ] != 'K' ||
		 kv3[ 2 ] != 'V' || kv3[ 3 ] != 0x03 )
		return {};

	const bool is_lz4  = ( std::memcmp( kv3 + 4, k_enc_lz4,  16 ) == 0 );
	const bool is_comp = ( std::memcmp( kv3 + 4, k_enc_comp, 16 ) == 0 );

	std::size_t pos = 36; // magic(4) + enc(16) + fmt(16)

	// --- string table ---
	if ( pos + 4 > kv3_size ) return {};
	std::uint32_t str_count;
	std::memcpy( &str_count, kv3 + pos, 4 ); pos += 4;
	// str_count: null-terminated string'leri atla
	for ( std::uint32_t i = 0; i < str_count; ++i )
	{
		while ( pos < kv3_size && kv3[ pos ] != 0 ) ++pos;
		if ( pos < kv3_size ) ++pos; // null byte
	}

	// --- boyutlar ---
	if ( pos + 12 > kv3_size ) return {};
	std::uint32_t cnt_bin, cnt_int, cnt_dbl;
	std::memcpy( &cnt_bin, kv3 + pos,     4 );
	std::memcpy( &cnt_int, kv3 + pos + 4, 4 );
	std::memcpy( &cnt_dbl, kv3 + pos + 8, 4 );
	pos += 12;

	const std::size_t total_uncomp =
		static_cast<std::size_t>( cnt_bin ) +
		static_cast<std::size_t>( cnt_int ) * 4 +
		static_cast<std::size_t>( cnt_dbl ) * 8;

	if ( total_uncomp == 0 || total_uncomp > 256u * 1024 * 1024 ) return {};

	std::vector<std::uint8_t> out( total_uncomp );

	if ( is_lz4 )
	{
		/** 3 ayri LZ4 blok: [unc_sz][cmp_sz][data] seklinde. */
		std::size_t write_off = 0;
		for ( int blk = 0; blk < 3; ++blk )
		{
			if ( pos + 8 > kv3_size ) return {};
			std::uint32_t unc_sz, cmp_sz;
			std::memcpy( &unc_sz, kv3 + pos,     4 );
			std::memcpy( &cmp_sz, kv3 + pos + 4, 4 );
			pos += 8;
			if ( pos + cmp_sz > kv3_size ) return {};

			if ( write_off + unc_sz > total_uncomp ) return {};
			std::size_t written = 0;
			Lz4BlockDecompress( kv3 + pos, cmp_sz,
				out.data() + write_off, unc_sz, written );
			write_off += unc_sz;
			pos       += cmp_sz;
		}
	}
	else if ( is_comp )
	{
		/** Tum 3 bolum tek LZ4 blok. */
		if ( pos + 8 > kv3_size ) return {};
		std::uint32_t unc_sz, cmp_sz;
		std::memcpy( &unc_sz, kv3 + pos,     4 );
		std::memcpy( &cmp_sz, kv3 + pos + 4, 4 );
		pos += 8;
		if ( pos + cmp_sz > kv3_size || unc_sz > total_uncomp ) return {};
		std::size_t written = 0;
		Lz4BlockDecompress( kv3 + pos, cmp_sz, out.data(), unc_sz, written );
	}
	else
	{
		/** Sikistirmasiz: binary_bytes dogrudan. */
		if ( pos + cnt_bin > kv3_size ) return {};
		std::memcpy( out.data(), kv3 + pos, cnt_bin );
	}

	// binary_bytes bolumunun ilk cnt_bin baytini don
	out.resize( static_cast<std::size_t>( cnt_bin ) );
	return out;
}

// ===========================================================================
// Source2 resource header → blok listesi
// ===========================================================================
struct Src2Block
{
	char         type[ 5 ]{};   // null-terminated, orn. "DATA", "PHYS", "MBUF"
	std::size_t  offset{};      // dosyadaki mutlak ofset
	std::uint32_t size{};
};

static std::vector<Src2Block> FindAllBlocks( const std::uint8_t* data, std::size_t sz )
{
	std::vector<Src2Block> result;
	if ( sz < 8 ) return result;

	std::uint16_t hdr_ver;
	std::memcpy( &hdr_ver, data + 4, 2 );
	if ( hdr_ver != 12 ) return result;

	if ( sz < 12 ) return result;
	std::uint32_t block_count;
	std::memcpy( &block_count, data + 8, 4 );
	if ( block_count == 0 || block_count > 64 ) return result;

	std::size_t entry_base = 12;
	for ( std::uint32_t i = 0; i < block_count; ++i )
	{
		if ( entry_base + 12 > sz ) break;
		std::uint32_t rel_off, blk_sz;
		std::memcpy( &rel_off, data + entry_base + 4, 4 );
		std::memcpy( &blk_sz,  data + entry_base + 8, 4 );
		const std::size_t abs_off = entry_base + 8 + rel_off;

		if ( abs_off < sz && blk_sz > 0 )
		{
			Src2Block blk;
			std::memcpy( blk.type, data + entry_base, 4 );
			blk.type[ 4 ] = '\0';
			blk.offset = abs_off;
			blk.size   = blk_sz;
			result.push_back( blk );
		}
		entry_base += 12;
	}
	return result;
}

// Geriye uyumluluk: sadece DATA blogu
static std::size_t FindDataBlockOffset( const std::uint8_t* data, std::size_t size )
{
	for ( const auto& b : FindAllBlocks( data, size ) )
		if ( std::memcmp( b.type, "DATA", 4 ) == 0 )
			return b.offset;
	return 0;
}

// ===========================================================================
// VPK yapilar  (v1 + v2 destekli)
// ===========================================================================
#pragma pack(push, 1)
struct VpkHeaderV2
{
	std::uint32_t signature;      // 0x55AA1234
	std::uint32_t version;        // 1 veya 2
	std::uint32_t tree_size;
	// v2 ek alanlari:
	std::uint32_t file_data_size;
	std::uint32_t archive_md5_size;
	std::uint32_t other_md5_size;
	std::uint32_t signature_size;
};
struct VpkEntry
{
	std::uint32_t crc32;
	std::uint16_t preload_bytes;
	std::uint16_t archive_index;  // 0x7FFF = bu dosyaya gömülü
	std::uint32_t entry_offset;
	std::uint32_t entry_length;
	std::uint16_t terminator;     // 0xFFFF
};
#pragma pack(pop)

static constexpr std::uint32_t k_vpk_sig   = 0x55AA1234;
static constexpr std::uint16_t k_dir_index = 0x7FFF;

static bool ReadStr( std::ifstream& f, std::string& out )
{
	out.clear();
	char c;
	while ( f.get( c ) )
	{
		if ( c == '\0' ) return true;
		out += c;
	}
	return false;
}

// ===========================================================================
// Genellestirilmis VPK dosya yapisi
// dir_file_path : tam yol (orn. "C:\...\maps\de_dust2.vpk")
//               : pak01_dir.vpk gibi ayri dir dosyasi da olabilir.
// base_name     : dis arsiv isimlendirmesi icin (orn. "pak01" veya "de_dust2")
//               : Bos gecilirse dir dosyasinin adi kullanilir.
// ===========================================================================
struct VpkContext
{
	std::string dir_file;     // dir dosyasinin tam yolu
	std::string base_dir;     // dir dosyasinin bulundugu dizin
	std::string arch_base;    // dis arsiv adinin on-eki (orn. "pak01", "de_dust2")
	std::uint32_t version{};  // 1 veya 2
	std::streampos embedded_start{}; // gömülü verinin baslangici
};

static VpkContext OpenVpk( const std::string& dir_file_path )
{
	VpkContext ctx;
	ctx.dir_file = dir_file_path;

	// base_dir: dizin kismi
	const auto sl = dir_file_path.find_last_of( "\\/" );
	ctx.base_dir  = sl != std::string::npos ? dir_file_path.substr( 0, sl + 1 ) : "";

	// arch_base: dosya adi, uzantisiz, "_dir" suffixsiz
	std::string fname = sl != std::string::npos ? dir_file_path.substr( sl + 1 ) : dir_file_path;
	const auto dot = fname.rfind( '.' );
	if ( dot != std::string::npos ) fname.resize( dot );
	// "_dir" suffixini sil
	if ( fname.size() > 4 && fname.compare( fname.size() - 4, 4, "_dir" ) == 0 )
		fname.resize( fname.size() - 4 );
	ctx.arch_base = fname;

	// Header oku
	std::ifstream f( dir_file_path, std::ios::binary );
	if ( !f.is_open() ) return ctx;

	std::uint32_t sig{}, ver{}, tree_size{};
	f.read( reinterpret_cast<char*>( &sig  ), 4 );
	f.read( reinterpret_cast<char*>( &ver  ), 4 );
	f.read( reinterpret_cast<char*>( &tree_size ), 4 );
	if ( sig != k_vpk_sig ) return ctx;

	ctx.version = ver;
	// v1: header = 12 byte; v2: header = 28 byte
	const std::uint32_t hdr_size = ( ver == 2 ) ? 28u : 12u;
	ctx.embedded_start = static_cast<std::streampos>( hdr_size ) +
		static_cast<std::streampos>( tree_size );

	return ctx;
}

// ===========================================================================
// VPK tree'sini tara
// ===========================================================================
struct VpkFileEntry
{
	std::string ext;
	std::string path;
	std::string filename;
	VpkEntry    entry;
};

static std::vector<VpkFileEntry> ScanVpkTreeFrom(
	const std::string& dir_file_path,
	const std::string& ext_filter = {} )
{
	std::vector<VpkFileEntry> result;

	// Sadece Header'i dogrula
	std::ifstream dir( dir_file_path, std::ios::binary );
	if ( !dir.is_open() ) return result;

	std::uint32_t sig{}, ver{}, tree_size{};
	dir.read( reinterpret_cast<char*>( &sig  ), 4 );
	dir.read( reinterpret_cast<char*>( &ver  ), 4 );
	dir.read( reinterpret_cast<char*>( &tree_size ), 4 );
	if ( sig != k_vpk_sig ) return result;

	// v2 ek alanlari atla
	if ( ver == 2 )
	{
		std::uint32_t tmp[ 4 ]{};
		dir.read( reinterpret_cast<char*>( tmp ), 16 );
	}

	std::string cur_ext, cur_path, cur_file;
	while ( ReadStr( dir, cur_ext ) && !cur_ext.empty() )
	{
		while ( ReadStr( dir, cur_path ) && !cur_path.empty() )
		{
			while ( ReadStr( dir, cur_file ) && !cur_file.empty() )
			{
				VpkEntry entry{};
				dir.read( reinterpret_cast<char*>( &entry ), sizeof( entry ) );
				std::vector<std::uint8_t> preload( entry.preload_bytes );
				if ( entry.preload_bytes )
					dir.read( reinterpret_cast<char*>( preload.data() ),
						entry.preload_bytes );

				if ( ext_filter.empty() || cur_ext == ext_filter )
					result.push_back( { cur_ext, cur_path, cur_file, entry } );
			}
		}
	}
	return result;
}

// pak01_dir.vpk icin tara (eski kod ile uyumluluk)
static std::vector<VpkFileEntry> ScanVpkTree(
	const std::string& game_dir,
	const std::string& ext_filter = {} )
{
	return ScanVpkTreeFrom( game_dir + "pak01_dir.vpk", ext_filter );
}

// ===========================================================================
// Genellestirilmis ayikla — herhangi bir VPK dosyasindan
// ===========================================================================
static std::vector<std::uint8_t> ExtractFromVpkFile(
	const std::string& dir_file_path,
	const std::string& ext,
	const std::string& path,
	const std::string& fname )
{
	const VpkContext ctx = OpenVpk( dir_file_path );
	if ( ctx.version == 0 ) return {};

	std::ifstream dir( dir_file_path, std::ios::binary );
	if ( !dir.is_open() ) return {};

	// Header'i atla
	const std::uint32_t hdr_size = ( ctx.version == 2 ) ? 28u : 12u;
	dir.seekg( hdr_size );

	std::string cur_ext, cur_path, cur_file;
	while ( ReadStr( dir, cur_ext ) && !cur_ext.empty() )
	{
		while ( ReadStr( dir, cur_path ) && !cur_path.empty() )
		{
			while ( ReadStr( dir, cur_file ) && !cur_file.empty() )
			{
				VpkEntry entry{};
				dir.read( reinterpret_cast<char*>( &entry ), sizeof( entry ) );
				std::vector<std::uint8_t> preload( entry.preload_bytes );
				if ( entry.preload_bytes )
					dir.read( reinterpret_cast<char*>( preload.data() ),
						entry.preload_bytes );

				if ( cur_ext != ext || cur_path != path || cur_file != fname )
					continue;

				const std::size_t total = entry.entry_length + entry.preload_bytes;
				if ( total == 0 ) return preload;

				std::vector<std::uint8_t> out( total );
				if ( !preload.empty() )
					std::memcpy( out.data(), preload.data(), preload.size() );

				if ( entry.entry_length > 0 )
				{
					if ( entry.archive_index == k_dir_index )
					{
						// Gomulu: bu dosyada, tree sonrasinda
						dir.seekg( ctx.embedded_start +
							static_cast<std::streampos>( entry.entry_offset ) );
						dir.read( reinterpret_cast<char*>(
							out.data() + entry.preload_bytes ),
							entry.entry_length );
					}
					else
					{
						// Dis arsiv: <base>_NNN.vpk
						char arch_name[ 64 ];
						snprintf( arch_name, sizeof arch_name,
							"%s_%03u.vpk",
							ctx.arch_base.c_str(),
							static_cast<unsigned>( entry.archive_index ) );
						std::ifstream arch(
							ctx.base_dir + arch_name, std::ios::binary );
						if ( !arch.is_open() ) return preload;
						arch.seekg(
							static_cast<std::streamoff>( entry.entry_offset ) );
						arch.read( reinterpret_cast<char*>(
							out.data() + entry.preload_bytes ),
							entry.entry_length );
					}
				}
				return out;
			}
		}
	}
	return {};
}

// Geriye uyumluluk: pak01_dir.vpk'dan ayikla
static std::vector<std::uint8_t> Extract(
	const std::string& game_dir,
	const std::string& ext,
	const std::string& path,
	const std::string& fname )
{
	return ExtractFromVpkFile( game_dir + "pak01_dir.vpk", ext, path, fname );
}

static std::vector<std::uint8_t> ExtractEntry(
	const std::string& game_dir,
	const VpkFileEntry& fe )
{
	return Extract( game_dir, fe.ext, fe.path, fe.filename );
}

// ===========================================================================
// harita adi → kv3 baytlari (text veya binary)
// Tum VPK tree'sini tarayarak harita adini iceren her vphys(_c) girdisini dener.
// out_is_binary = true ise binary KV3 + Source2 resource formatindadir.
// out_found_path: debug icin bulunan VPK yolunu doldurur.
// ===========================================================================
static std::vector<std::uint8_t> FindMapVphys(
	const std::string& game_dir,
	const std::string& map_name,
	bool& out_is_binary,
	std::string& out_found_path )
{
	out_is_binary  = false;
	out_found_path.clear();

	// --- Once sabit adaylarla dene (hizli yol) ---
	const std::vector<std::string> ext_cands  = { "vphys_c", "vphys" };
	const std::vector<std::string> path_cands = {
		"maps",
		"maps/" + map_name,
		"maps\\" + map_name,
		"physics",
		"",          // kok dizin
	};
	const std::vector<std::string> fname_cands = {
		map_name,
		map_name + "_c0",
		map_name + "_c",
		map_name + "_col",
		map_name + "_physics",
		map_name + "_mesh",
		map_name + "_world",
		"world_physics",
		"world",
	};

	for ( const auto& ext : ext_cands )
	{
		for ( const auto& p : path_cands )
		{
			for ( const auto& f : fname_cands )
			{
				auto data = Extract( game_dir, ext, p, f );
				if ( !data.empty() )
				{
					out_is_binary  = ( ext == "vphys_c" );
					out_found_path = p + "/" + f + "." + ext;
					return data;
				}
			}
		}
	}

	// --- Sabit adaylar basarisiz: tum tree'yi tara ---
	// map_name alt stringini path VEYA filename icinde ariyoruz
	const auto all_vphys = ScanVpkTree( game_dir, "vphys_c" );
	const auto all_vphys2= ScanVpkTree( game_dir, "vphys"   );

	// Bulunan tum vphys girdilerini log dosyasina yaz (bir kez)
	{
		static bool logged = false;
		if ( !logged )
		{
			logged = true;
			std::ofstream log( "C:\\vphys_scan.txt" );
			if ( log.is_open() )
			{
				log << "game_dir=" << game_dir << "\n";
				log << "pak01_dir.vpk exists="
					<< ( GetFileAttributesA( ( game_dir + "pak01_dir.vpk" ).c_str() )
						 != INVALID_FILE_ATTRIBUTES ? "YES" : "NO" ) << "\n\n";

				// Dogrulama: ilk 150 benzersiz extension'i say
				const auto all_entries = ScanVpkTree( game_dir );
				std::map<std::string, int> ext_count;
				for ( const auto& e : all_entries ) ext_count[ e.ext ]++;

				log << "=== unique extensions in pak01_dir.vpk (" << all_entries.size()
					<< " total entries) ===\n";
				for ( const auto& kv : ext_count )
					log << "  ." << kv.first << "  (" << kv.second << ")\n";

				log << "\n=== vphys_c entries ===\n";
				for ( const auto& e : all_vphys )
					log << e.path << "/" << e.filename << "." << e.ext
						<< " arch=" << e.entry.archive_index
						<< " sz=" << e.entry.entry_length << "\n";
				if ( all_vphys.empty() ) log << "  (none)\n";

				log << "\n=== vphys entries ===\n";
				for ( const auto& e : all_vphys2 )
					log << e.path << "/" << e.filename << "." << e.ext
						<< " arch=" << e.entry.archive_index
						<< " sz=" << e.entry.entry_length << "\n";
				if ( all_vphys2.empty() ) log << "  (none)\n";

				// Harita-ozel VPK vari mi? (*.vpk tariyoruz)
				log << "\n=== map VPKs in csgo/maps/ (*.vpk) ===\n";
				const std::string maps_dir = game_dir + "maps\\";
				WIN32_FIND_DATAA fd{};
				HANDLE hFind = FindFirstFileA( ( maps_dir + "*.vpk" ).c_str(), &fd );
				if ( hFind != INVALID_HANDLE_VALUE )
				{
					do
					{
						const std::string mvpk = maps_dir + fd.cFileName;
						// Icerideki extension'lari say
						const auto entries = ScanVpkTreeFrom( mvpk );
						std::map<std::string, int> ec;
						for ( const auto& e : entries ) ec[ e.ext ]++;
						log << "  " << fd.cFileName
							<< "  (" << entries.size() << " entries)\n";
						for ( const auto& kv : ec )
							log << "      ." << kv.first << " (" << kv.second << ")\n";
					} while ( FindNextFileA( hFind, &fd ) );
					FindClose( hFind );
				}
				else log << "  (none or FindFirst failed)\n";

				// de_dust2.vpk varsa vwrld_c bloklarini logla
				{
					const std::string dust2 = game_dir + "maps\\de_dust2.vpk";
					log << "\n=== de_dust2.vpk vwrld_c Source2 blocks ===\n";
					const auto dust2_entries = ScanVpkTreeFrom( dust2 );
					for ( const auto& fe : dust2_entries )
					{
						if ( fe.ext != "vwrld_c" ) continue;
						log << "  entry: " << fe.path << "/" << fe.filename << ".vwrld_c"
							<< "  arch=" << fe.entry.archive_index
							<< "  sz=" << fe.entry.entry_length << "\n";

						auto raw = ExtractFromVpkFile( dust2, fe.ext, fe.path, fe.filename );
						if ( raw.empty() ) { log << "  (extraction failed)\n"; continue; }
						log << "  extracted " << raw.size() << " bytes\n";

						const auto blks = FindAllBlocks( raw.data(), raw.size() );
						log << "  blocks:\n";
						for ( const auto& b : blks )
							log << "    [" << b.type << "]  offset="
								<< b.offset << "  size=" << b.size << "\n";
					}
					if ( dust2_entries.empty() ) log << "  (de_dust2.vpk not found or empty)\n";
				}
			}
		}
	}

	// Harita adi alt stringi iceren ilk girdiyi ayikla
	auto TryList = [&]( const std::vector<VpkFileEntry>& lst, bool bin ) ->
		std::vector<std::uint8_t>
	{
		for ( const auto& fe : lst )
		{
			const bool in_path  = fe.path.find( map_name ) != std::string::npos;
			const bool in_file  = fe.filename.find( map_name ) != std::string::npos ||
								  fe.filename.find( "world_physics" ) != std::string::npos;
			if ( !in_path && !in_file ) continue;

			auto data = ExtractEntry( game_dir, fe );
			if ( !data.empty() )
			{
				out_is_binary  = bin;
				out_found_path = fe.path + "/" + fe.filename + "." + fe.ext;
				return data;
			}
		}
		return {};
	};

	{
		auto r = TryList( all_vphys, true );
		if ( !r.empty() ) return r;
		auto r2 = TryList( all_vphys2, false );
		if ( !r2.empty() ) return r2;
	}

	// pak01 tree'sinde bulunamadi; csgo/maps/ klasoründeki harita VPK'larini tara
	{
		const std::string maps_dir = game_dir + "maps\\";

		// Adayi sabit dosyalar
		const std::vector<std::string> map_vpk_cands = {
			maps_dir + map_name + ".vpk",
			maps_dir + map_name + "_dir.vpk",
			maps_dir + map_name + "_c0.vpk",
		};

		// Tercih sirasi: vphys_c > vphys > vwrld_c (physics embeds world mesh)
		auto ExtPriority = []( const std::string& ext ) -> int {
			if ( ext == "vphys_c" ) return 0;
			if ( ext == "vphys"   ) return 1;
			if ( ext == "vwrld_c" ) return 2;
			return 99;
		};

		auto TryMapVpk = [&]( const std::string& vpk_path ) -> std::vector<std::uint8_t>
		{
			if ( GetFileAttributesA( vpk_path.c_str() ) == INVALID_FILE_ATTRIBUTES )
				return {};

			auto entries = ScanVpkTreeFrom( vpk_path );
			// Oncelik sirasi: vphys_c < vphys < vwrld_c
			std::sort( entries.begin(), entries.end(),
				[&]( const VpkFileEntry& a, const VpkFileEntry& b )
				{ return ExtPriority( a.ext ) < ExtPriority( b.ext ); } );

			for ( const auto& fe : entries )
			{
				if ( fe.ext != "vphys_c" && fe.ext != "vphys" && fe.ext != "vwrld_c" )
					continue;
				auto data = ExtractFromVpkFile( vpk_path, fe.ext, fe.path, fe.filename );
				if ( !data.empty() )
				{
					out_is_binary  = true; // hepsi binary
					out_found_path = vpk_path + "::" + fe.path + "/" +
						fe.filename + "." + fe.ext;
					return data;
				}
			}
			return {};
		};

		for ( const auto& cand : map_vpk_cands )
		{
			auto r = TryMapVpk( cand );
			if ( !r.empty() ) return r;
		}

		// Harita adini iceren tum *.vpk dosyalarini tara
		WIN32_FIND_DATAA fd{};
		HANDLE hFind = FindFirstFileA( ( maps_dir + "*.vpk" ).c_str(), &fd );
		if ( hFind != INVALID_HANDLE_VALUE )
		{
			do
			{
				const std::string ffn = fd.cFileName;
				if ( ffn.find( map_name ) == std::string::npos ) continue;
				auto r = TryMapVpk( maps_dir + ffn );
				if ( !r.empty() )
				{
					FindClose( hFind );
					return r;
				}
			} while ( FindNextFileA( hFind, &fd ) );
			FindClose( hFind );
		}
	}

	return {};
}

// ===========================================================================
// CS2 kurulum yolu
// ===========================================================================

// Steam kayit defterinden kurulum dizini
static std::string SteamInstallPathFromRegistry()
{
	HKEY hKey = nullptr;
	if ( RegOpenKeyExA( HKEY_LOCAL_MACHINE,
		"SOFTWARE\\WOW6432Node\\Valve\\Steam",
		0, KEY_READ, &hKey ) != ERROR_SUCCESS )
	{
		RegOpenKeyExA( HKEY_LOCAL_MACHINE,
			"SOFTWARE\\Valve\\Steam",
			0, KEY_READ, &hKey );
	}
	if ( !hKey ) return {};

	char buf[ MAX_PATH ]{};
	DWORD sz = MAX_PATH;
	DWORD type = REG_SZ;
	const LSTATUS r = RegQueryValueExA( hKey, "InstallPath", nullptr, &type,
		reinterpret_cast<LPBYTE>( buf ), &sz );
	RegCloseKey( hKey );
	if ( r != ERROR_SUCCESS ) return {};
	return std::string( buf );
}

static std::string GetCs2GameDir()
{
	auto TryDir = []( const std::string& candidate ) -> bool
	{
		const std::string vpk = candidate + "pak01_dir.vpk";
		return GetFileAttributesA( vpk.c_str() ) != INVALID_FILE_ATTRIBUTES;
	};

	/** 1. Stratéji: islemin yolu */
	if ( processid )
	{
		HANDLE hProc = OpenProcess( PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
			static_cast<DWORD>( processid ) );
		if ( hProc )
		{
			char path[ MAX_PATH * 2 ]{};
			DWORD sz = sizeof( path ) - 1;
			if ( QueryFullProcessImageNameA( hProc, 0, path, &sz ) )
			{
				std::string s = path;
				// cs2.exe: .../game/bin/win64/cs2.exe
				// 3 ust: .../game/   sonra csgo/ ekle
				for ( int up = 0; up < 3; ++up )
				{
					const auto sl = s.find_last_of( "\\/" );
					if ( sl == std::string::npos ) break;
					s.resize( sl );
				}
				const std::string candidate = s + "\\csgo\\";
				if ( TryDir( candidate ) )
				{
					CloseHandle( hProc );
					return candidate;
				}
			}
			CloseHandle( hProc );
		}
	}

	/** 2. Strateji: Steam kayit defteri */
	const std::string steam = SteamInstallPathFromRegistry();
	if ( !steam.empty() )
	{
		const std::string candidate =
			steam + "\\steamapps\\common\\Counter-Strike Global Offensive\\game\\csgo\\";
		if ( TryDir( candidate ) )
			return candidate;
	}

	/** 3. Strateji: bilinen sabit yollar */
	const std::vector<std::string> known = {
		"C:\\Program Files (x86)\\Steam\\steamapps\\common\\"
			"Counter-Strike Global Offensive\\game\\csgo\\",
		"C:\\Program Files\\Steam\\steamapps\\common\\"
			"Counter-Strike Global Offensive\\game\\csgo\\",
		"D:\\Steam\\steamapps\\common\\"
			"Counter-Strike Global Offensive\\game\\csgo\\",
		"E:\\Steam\\steamapps\\common\\"
			"Counter-Strike Global Offensive\\game\\csgo\\",
	};
	for ( const auto& p : known )
		if ( TryDir( p ) ) return p;

	return {};
}

} // namespace vpk_reader
