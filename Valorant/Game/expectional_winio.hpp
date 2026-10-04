#pragma once

#include <Windows.h>
#include <ShlObj.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ExpectionalWinIO {

inline std::wstring Utf8ToWide(const std::string& u8)
{
	if (u8.empty())
		return {};
	const int n = MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, nullptr, 0);
	if (n <= 1)
		return {};
	std::wstring w(static_cast<size_t>(n - 1), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, u8.c_str(), -1, w.data(), n);
	return w;
}

inline std::string WideToUtf8(const std::wstring& w)
{
	if (w.empty())
		return {};
	const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
	if (n <= 1)
		return {};
	std::string u8(static_cast<size_t>(n - 1), '\0');
	WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, u8.data(), n, nullptr, nullptr);
	return u8;
}

inline bool DirExistsWide(const std::wstring& path)
{
	const DWORD attr = GetFileAttributesW(path.c_str());
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool FileExistsWide(const std::wstring& path)
{
	const DWORD attr = GetFileAttributesW(path.c_str());
	return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

inline bool EnsureDirectoryWide(const std::wstring& dir)
{
	if (dir.empty())
		return false;
	if (DirExistsWide(dir))
		return true;
	const DWORD r = SHCreateDirectoryExW(nullptr, dir.c_str(), nullptr);
	return r == ERROR_SUCCESS || r == ERROR_ALREADY_EXISTS;
}

inline bool DeleteFileIfExistsWide(const std::wstring& path)
{
	if (!FileExistsWide(path))
		return true;
	return DeleteFileW(path.c_str()) != FALSE;
}

inline std::wstring AnsiPathToWide(const std::string& pathA)
{
	if (pathA.empty())
		return {};
	const int n = MultiByteToWideChar(CP_ACP, 0, pathA.c_str(), -1, nullptr, 0);
	if (n <= 1)
		return {};
	std::wstring w(static_cast<size_t>(n - 1), L'\0');
	MultiByteToWideChar(CP_ACP, 0, pathA.c_str(), -1, w.data(), n);
	for (wchar_t& c : w) {
		if (c == L'/')
			c = L'\\';
	}
	return w;
}

inline bool ReadAllBytesWide(const std::wstring& path, std::vector<uint8_t>& out,
    uint64_t maxBytes = 512ull * 1024 * 1024)
{
	out.clear();
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
	    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	LARGE_INTEGER li{};
	if (!GetFileSizeEx(h, &li) || li.QuadPart < 0 || static_cast<uint64_t>(li.QuadPart) > maxBytes) {
		CloseHandle(h);
		return false;
	}
	out.resize(static_cast<size_t>(li.QuadPart));
	DWORD rd = 0;
	const BOOL ok = ReadFile(h, out.data(), static_cast<DWORD>(out.size()), &rd, nullptr);
	CloseHandle(h);
	if (!ok) {
		out.clear();
		return false;
	}
	out.resize(rd);
	return rd > 0;
}

inline bool ReadAllBytesUtf8(const std::string& pathUtf8, std::vector<uint8_t>& out)
{
	std::wstring w = Utf8ToWide(pathUtf8);
	for (wchar_t& c : w) {
		if (c == L'/')
			c = L'\\';
	}
	return ReadAllBytesWide(w, out);
}

/** FindFirstFileA / registry yolları — CP_ACP, slash normalize. VPK + Steam dosyalari icin. */
inline bool ReadAllBytesPathA(const std::string& pathA, std::vector<uint8_t>& out)
{
	return ReadAllBytesWide(AnsiPathToWide(pathA), out);
}

inline uint64_t FileSizeWide(const std::wstring& path)
{
	const DWORD attr = GetFileAttributesW(path.c_str());
	if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY))
		return 0;
	HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
	    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return 0;
	LARGE_INTEGER li{};
	const BOOL ok = GetFileSizeEx(h, &li);
	CloseHandle(h);
	if (!ok || li.QuadPart < 0)
		return 0;
	return static_cast<uint64_t>(li.QuadPart);
}

inline bool WriteAllBytesWide(const std::wstring& path, const void* data, DWORD size)
{
	const size_t slash = path.find_last_of(L"\\/");
	if (slash != std::wstring::npos)
		(void)EnsureDirectoryWide(path.substr(0, slash));
	HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
	    nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (h == INVALID_HANDLE_VALUE)
		return false;
	DWORD wr = 0;
	const BOOL ok = WriteFile(h, data, size, &wr, nullptr) && wr == size;
	CloseHandle(h);
	return ok != FALSE;
}

using FileVisitFn = std::function<void(const std::wstring& filePath)>;

inline void ForEachFileWide(const std::wstring& root, bool recursive,
    const wchar_t* extLower, const FileVisitFn& visit)
{
	if (root.empty() || !visit)
		return;
	std::vector<std::wstring> stack;
	stack.push_back(root);
	while (!stack.empty()) {
		const std::wstring dir = std::move(stack.back());
		stack.pop_back();
		const std::wstring pattern = dir + L"\\*";
		WIN32_FIND_DATAW fd{};
		HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
		if (hFind == INVALID_HANDLE_VALUE)
			continue;
		do {
			if (fd.cFileName[0] == L'.' &&
			    (fd.cFileName[1] == L'\0' ||
			     (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0')))
				continue;
			const std::wstring full = dir + L"\\" + fd.cFileName;
			if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				if (recursive)
					stack.push_back(full);
				continue;
			}
			if (extLower && extLower[0]) {
				const wchar_t* dot = wcsrchr(fd.cFileName, L'.');
				if (!dot)
					continue;
				if (_wcsicmp(dot, extLower) != 0)
					continue;
			}
			visit(full);
		} while (FindNextFileW(hFind, &fd));
		FindClose(hFind);
	}
}

inline void ForEachSubdirWide(const std::wstring& root, const FileVisitFn& visitDir)
{
	if (root.empty() || !visitDir || !DirExistsWide(root))
		return;
	const std::wstring pattern = root + L"\\*";
	WIN32_FIND_DATAW fd{};
	HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
	if (hFind == INVALID_HANDLE_VALUE)
		return;
	do {
		if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			continue;
		if (fd.cFileName[0] == L'.' &&
		    (fd.cFileName[1] == L'\0' ||
		     (fd.cFileName[1] == L'.' && fd.cFileName[2] == L'\0')))
			continue;
		visitDir(root + L"\\" + fd.cFileName);
	} while (FindNextFileW(hFind, &fd));
	FindClose(hFind);
}

}  // namespace ExpectionalWinIO
