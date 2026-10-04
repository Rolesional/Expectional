#pragma once
#include <Windows.h>
#include <ShlObj.h>
#include <string>

namespace ExpectionalPaths {

inline constexpr wchar_t kUwpNotepadPackage[] = L"Microsoft.WindowsNotepad_8wekyb3d8bbwe";

inline bool UserLocalAppDataWide(std::wstring& out)
{
	wchar_t buf[MAX_PATH]{};
	if (FAILED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr,
	        SHGFP_TYPE_CURRENT, buf)))
		return false;
	out.assign(buf);
	return true;
}

inline std::wstring TrueUserLocalAppDataWide()
{
	wchar_t buf[MAX_PATH]{};
	const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
	if (n > 0 && n < MAX_PATH)
		return std::wstring(buf);
	std::wstring sh;
	if (UserLocalAppDataWide(sh))
		return sh;
	return {};
}

inline std::wstring UwpNotepadConfigDirWide()
{
	const std::wstring lad = TrueUserLocalAppDataWide();
	if (lad.empty())
		return {};
	return lad + L"\\Packages\\" + kUwpNotepadPackage
	     + L"\\LocalCache\\Local\\Expectional\\configs";
}

inline std::wstring GlobalExpectionalRootWide()
{
	const std::wstring lad = TrueUserLocalAppDataWide();
	if (lad.empty())
		return {};
	return lad + L"\\Expectional";
}

inline std::wstring GlobalConfigDirWide()
{
	const std::wstring root = GlobalExpectionalRootWide();
	if (root.empty())
		return {};
	return root + L"\\configs";
}

inline std::wstring GlobalLineupsDirWide()
{
	const std::wstring root = GlobalExpectionalRootWide();
	if (root.empty())
		return {};
	return root + L"\\lineups";
}

inline std::wstring GlobalMapsDirWide()
{
	const std::wstring root = GlobalExpectionalRootWide();
	if (root.empty())
		return {};
	return root + L"\\maps";
}

inline std::wstring UwpNotepadExpectionalRootWide()
{
	const std::wstring lad = TrueUserLocalAppDataWide();
	if (lad.empty())
		return {};
	return lad + L"\\Packages\\" + kUwpNotepadPackage
	     + L"\\LocalCache\\Local\\Expectional";
}

inline std::wstring GlobalImGuiIniPathWide()
{
	const std::wstring root = GlobalExpectionalRootWide();
	if (root.empty())
		return {};
	return root + L"\\imgui.ini";
}

inline std::wstring UwpNotepadImGuiIniPathWide()
{
	const std::wstring root = UwpNotepadExpectionalRootWide();
	if (root.empty())
		return {};
	return root + L"\\imgui.ini";
}

inline std::wstring UserDocumentsLineupsDirWide()
{
	wchar_t profile[MAX_PATH]{};
	const DWORD n = GetEnvironmentVariableW(L"USERPROFILE", profile, MAX_PATH);
	if (n == 0 || n >= MAX_PATH)
		return {};
	return std::wstring(profile) + L"\\Documents\\Expectional\\lineups";
}

}  
