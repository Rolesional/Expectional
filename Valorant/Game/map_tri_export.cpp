#include "map_tri_export.hpp"

#include "../ThirdParty/map_mesh/MapParser.hpp"
#include "expectional_paths.hpp"
#include "expectional_winio.hpp"

#include <atomic>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

std::mutex g_export_mtx;
std::atomic<bool> g_export_busy{false};

constexpr std::size_t kMinTriangleCount = 256u;
constexpr std::size_t kTriBytesPerTriangle = 36u;

static std::string NormalizeMapStem(std::string map_name) {
	if (map_name.empty())
		return {};
	for (char& c : map_name) {
		if (c == '\\')
			c = '/';
		else if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
	}
	const size_t slash = map_name.find_last_of('/');
	if (slash != std::string::npos && slash + 1 < map_name.size())
		map_name = map_name.substr(slash + 1);
	if (map_name.size() > 4 && map_name.compare(map_name.size() - 4, 4, ".bsp") == 0)
		map_name.resize(map_name.size() - 4);
	if (map_name.size() > 4 && map_name.compare(map_name.size() - 4, 4, ".vpk") == 0)
		map_name.resize(map_name.size() - 4);
	const size_t dot = map_name.rfind('.');
	if (dot != std::string::npos && dot > 0)
		map_name = map_name.substr(0, dot);
	while (!map_name.empty() && (map_name.front() == ' ' || map_name.front() == '\t'))
		map_name.erase(0, 1);
	while (!map_name.empty() && (map_name.back() == ' ' || map_name.back() == '\t'))
		map_name.pop_back();
	if (map_name == "<empty>")
		return {};
	return map_name;
}

static std::wstring LocalExpectionalMapsDirWide() {
	const std::wstring dir = ExpectionalPaths::GlobalMapsDirWide();
	if (dir.empty())
		return {};
	(void)ExpectionalWinIO::EnsureDirectoryWide(dir);
	return dir;
}

static bool WriteAllBytesAtomicWide(const std::wstring& out_path, const std::vector<uint8_t>& data) {
	if (data.empty())
		return false;
	const std::wstring tmp = out_path + L".tmp";
	if (!ExpectionalWinIO::WriteAllBytesWide(tmp, data.data(), static_cast<DWORD>(data.size())))
		return false;
	if (!ExpectionalWinIO::DeleteFileIfExistsWide(out_path))
		return false;
	return MoveFileExW(tmp.c_str(), out_path.c_str(),
	    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
}

static bool WriteTriBinaryWide(const std::wstring& out_path, const std::vector<map_parser::Triangle>& tris) {
	if (tris.size() < kMinTriangleCount)
		return false;
	std::vector<uint8_t> blob;
	blob.reserve(tris.size() * kTriBytesPerTriangle);
	for (const auto& t : tris) {
		const float buf[9] = {t.v0.x, t.v0.y, t.v0.z, t.v1.x, t.v1.y, t.v1.z, t.v2.x, t.v2.y, t.v2.z};
		const auto* p = reinterpret_cast<const uint8_t*>(buf);
		blob.insert(blob.end(), p, p + sizeof(buf));
	}
	return WriteAllBytesAtomicWide(out_path, blob);
}

static void ExportThread(std::string map, std::wstring out_path) {
	struct BusyReset {
		~BusyReset() { g_export_busy.store(false, std::memory_order_release); }
	} reset_busy;

	map_parser::MapMesh mesh = map_parser::load_mesh(map);
	if (!mesh.valid || mesh.triangles.size() < kMinTriangleCount) {
		(void)ExpectionalWinIO::DeleteFileIfExistsWide(out_path);
		return;
	}
	if (!WriteTriBinaryWide(out_path, mesh.triangles))
		(void)ExpectionalWinIO::DeleteFileIfExistsWide(out_path);
}

static bool TriMeshFileValidWide(const std::wstring& path) noexcept {
	const uint64_t sz = ExpectionalWinIO::FileSizeWide(path);
	if (sz < kMinTriangleCount * kTriBytesPerTriangle)
		return false;
	return (sz % kTriBytesPerTriangle) == 0;
}

} 

bool TriMeshFileValid(const std::filesystem::path& path) noexcept {
	try {
		return TriMeshFileValidWide(path.wstring());
	} catch (...) {
		return false;
	}
}

bool RemoveTriMeshFileIfInvalid(const std::filesystem::path& path) noexcept {
	try {
		if (TriMeshFileValid(path))
			return false;
		return ExpectionalWinIO::DeleteFileIfExistsWide(path.wstring());
	} catch (...) {
		return false;
	}
}

bool MapTriExportInProgress() noexcept {
	return g_export_busy.load(std::memory_order_acquire);
}

void MapTriExport_KickIfNeeded(const std::string& map_clean) {
	const std::string map = NormalizeMapStem(map_clean);
	if (map.empty() || map.size() > 200)
		return;
	for (unsigned char c : map) {
		if (c < 32)
			return;
		if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
			return;
	}

	const std::wstring dir = LocalExpectionalMapsDirWide();
	if (dir.empty())
		return;
	const std::wstring out = dir + L"\\" + ExpectionalWinIO::Utf8ToWide(map) + L".tri";

	if (TriMeshFileValidWide(out))
		return;

	(void)ExpectionalWinIO::DeleteFileIfExistsWide(out);

	if (g_export_busy.load(std::memory_order_acquire))
		return;

	std::lock_guard<std::mutex> lk(g_export_mtx);
	if (g_export_busy.load(std::memory_order_acquire))
		return;
	if (TriMeshFileValidWide(out))
		return;

	g_export_busy.store(true, std::memory_order_release);
	std::thread(ExportThread, map, out).detach();
}
