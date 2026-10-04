#pragma once

#if defined(_MSC_VER) && _MSC_VER >= 1200
#   pragma once
#endif

#ifndef WIN32_LEAN_AND_MEAN
#   define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#   define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <sstream>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "../../Game/expectional_winio.hpp"

namespace vpk {

struct RawDirEntry {
    uint32_t crc32;
    uint16_t preload_bytes;
    uint16_t archive_index;   
    uint32_t entry_offset;
    uint32_t entry_length;
    
};
static_assert(sizeof(RawDirEntry) == 16, "RawDirEntry size mismatch");

struct VPKHeaderV2 {
    uint32_t signature;                
    uint32_t version;                  
    uint32_t tree_size;
    uint32_t file_data_section_size;
    uint32_t archive_md5_section_size;
    uint32_t other_md5_section_size;
    uint32_t signature_section_size;
};
static_assert(sizeof(VPKHeaderV2) == 28, "VPKHeaderV2 size mismatch");

static constexpr uint32_t VPK_SIGNATURE = 0x55AA1234u;
static constexpr uint32_t VPK_VERSION   = 2u;
static constexpr uint16_t VPK_EMBEDDED  = 0x7FFF;

struct DirEntry {
    uint32_t             crc32         = 0;
    uint16_t             archive_index = 0;
    uint32_t             entry_offset  = 0;
    uint32_t             entry_length  = 0;
    std::vector<uint8_t> preload_data;           
};

namespace detail {

inline bool read_whole_file_win32(const std::string& path, std::vector<uint8_t>& out) {
    return ExpectionalWinIO::ReadAllBytesPathA(path, out);
}

inline bool read_whole_file_win32(const std::wstring& path, std::vector<uint8_t>& out) {
    return ExpectionalWinIO::ReadAllBytesWide(path, out);
}

inline bool read_text_file_utf8(const std::string& path, std::string& out) {
    std::vector<uint8_t> bytes;
    if (!ExpectionalWinIO::ReadAllBytesPathA(path, bytes))
        return false;
    out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

inline bool read_cstring(const uint8_t* buf, size_t buf_size,
                         size_t& pos, std::string& out) {
    out.clear();
    while (pos < buf_size) {
        char c = static_cast<char>(buf[pos++]);
        if (c == '\0') return true;
        out.push_back(c);
    }
    return false;  
}

template<typename T>
inline T read_le(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

} 

class VPKDir {
public:
    VPKDir() = default;

    bool open(const std::string& dir_vpk_path) {
        opened_      = false;
        dir_path_    = dir_vpk_path;
        archive_base_ = derive_archive_base(dir_vpk_path);
        entries_.clear();
        embedded_data_.clear();

        std::vector<uint8_t> file_bytes;
        if (!detail::read_whole_file_win32(dir_vpk_path, file_bytes))
            return false;

        const size_t file_size = file_bytes.size();
        if (file_size < sizeof(VPKHeaderV2)) return false;

        VPKHeaderV2 hdr{};
        std::memcpy(&hdr, file_bytes.data(), sizeof(hdr));
        if (hdr.signature != VPK_SIGNATURE) return false;
        if (hdr.version != VPK_VERSION)     return false;

        size_t offset = sizeof(VPKHeaderV2);
        if (offset + hdr.tree_size > file_size) return false;

        std::vector<uint8_t> tree(file_bytes.begin() + offset,
                                  file_bytes.begin() + offset + hdr.tree_size);
        offset += hdr.tree_size;

        if (hdr.file_data_section_size > 0) {
            if (offset + hdr.file_data_section_size > file_size) return false;
            embedded_data_.assign(file_bytes.begin() + offset,
                                  file_bytes.begin() + offset + hdr.file_data_section_size);
        }

        opened_ = parse_tree(tree);
        if (!opened_) entries_.clear();
        return opened_;
    }

    bool open_from_bytes(const std::vector<uint8_t>& vpk_data) {
        opened_      = false;
        dir_path_    = "<in-memory>";
        archive_base_ = "";  
        entries_.clear();
        embedded_data_.clear();

        if (vpk_data.size() < sizeof(VPKHeaderV2)) return false;

        VPKHeaderV2 hdr{};
        std::memcpy(&hdr, vpk_data.data(), sizeof(hdr));
        if (hdr.signature != VPK_SIGNATURE) return false;
        if (hdr.version != VPK_VERSION)     return false;

        size_t offset = sizeof(VPKHeaderV2);
        if (offset + hdr.tree_size > vpk_data.size()) return false;

        std::vector<uint8_t> tree(vpk_data.begin() + offset,
                                   vpk_data.begin() + offset + hdr.tree_size);
        offset += hdr.tree_size;

        if (hdr.file_data_section_size > 0) {
            if (offset + hdr.file_data_section_size > vpk_data.size()) return false;
            embedded_data_.assign(vpk_data.begin() + offset,
                                  vpk_data.begin() + offset + hdr.file_data_section_size);
        }

        opened_ = parse_tree(tree);
        if (!opened_) entries_.clear();  
        return opened_;
    }

    std::optional<std::vector<uint8_t>> read_file(const std::string& full_path) const {
        
        std::string key = full_path;
        for (char& c : key) if (c == '\\') c = '/';
        
        for (char& c : key)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');

        auto it = entries_.find(key);
        if (it == entries_.end()) return std::nullopt;
        const DirEntry& e = it->second;

        const size_t total_size = e.preload_data.size() + e.entry_length;
        std::vector<uint8_t> out;
        out.reserve(total_size);
        out.insert(out.end(), e.preload_data.begin(), e.preload_data.end());

        if (e.entry_length == 0) return out;

        if (e.archive_index == VPK_EMBEDDED) {
            
            if (e.entry_offset + e.entry_length > embedded_data_.size())
                return std::nullopt;
            out.insert(out.end(),
                       embedded_data_.data() + e.entry_offset,
                       embedded_data_.data() + e.entry_offset + e.entry_length);
        } else {
            
            char idx_buf[8];
            std::snprintf(idx_buf, sizeof(idx_buf), "%03d", (int)e.archive_index);
            std::string archive_path = archive_base_ + idx_buf + ".vpk";

            std::vector<uint8_t> archive_bytes;
            if (!detail::read_whole_file_win32(archive_path, archive_bytes))
                return std::nullopt;
            if (e.entry_offset + e.entry_length > archive_bytes.size())
                return std::nullopt;

            const size_t old_size = out.size();
            out.resize(old_size + e.entry_length);
            std::memcpy(out.data() + old_size,
                        archive_bytes.data() + e.entry_offset,
                        e.entry_length);
        }

        return out;
    }

    bool is_open() const { return opened_; }

    size_t file_count() const { return entries_.size(); }

    std::optional<std::string> find_by_filename(const std::string& filename) const {
        std::string needle = filename;
        for (char& c : needle)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        for (const auto& [key, _] : entries_) {
            if (key.size() >= needle.size() &&
                key.compare(key.size() - needle.size(), needle.size(), needle) == 0)
                return key;
        }
        return std::nullopt;
    }

    std::vector<std::string> enumerate_matching(const std::string& substring,
                                                size_t limit = 20) const {
        std::string needle = substring;
        for (char& c : needle)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        std::vector<std::string> result;
        result.reserve(limit);
        for (const auto& [key, _] : entries_) {
            if (key.find(needle) != std::string::npos) {
                result.push_back(key);
                if (result.size() >= limit) break;
            }
        }
        return result;
    }

    std::vector<std::string> list_files(const std::string& prefix = {},
                                        const std::string& suffix = {}) const {
        std::string norm_prefix = prefix;
        std::string norm_suffix = suffix;
        for (char& c : norm_prefix) {
            if (c == '\\') c = '/';
            else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        for (char& c : norm_suffix) {
            if (c == '\\') c = '/';
            else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }

        std::vector<std::string> result;
        result.reserve(entries_.size());
        for (const auto& [key, _] : entries_) {
            if (!norm_prefix.empty() && key.rfind(norm_prefix, 0) != 0)
                continue;
            if (!norm_suffix.empty()) {
                if (key.size() < norm_suffix.size())
                    continue;
                if (key.compare(key.size() - norm_suffix.size(), norm_suffix.size(), norm_suffix) != 0)
                    continue;
            }
            result.push_back(key);
        }
        std::sort(result.begin(), result.end());
        return result;
    }

    std::vector<std::string> enumerate_paths(const std::string& prefix = {},
                                            const std::string& suffix = {}) const {
        std::string prefix_key = prefix;
        std::string suffix_key = suffix;
        for (char& c : prefix_key) {
            if (c == '\\') c = '/';
            else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        for (char& c : suffix_key) {
            if (c == '\\') c = '/';
            else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }

        std::vector<std::string> result;
        result.reserve(entries_.size());
        for (const auto& [key, _] : entries_) {
            if (!prefix_key.empty() && key.rfind(prefix_key, 0) != 0)
                continue;
            if (!suffix_key.empty()) {
                if (key.size() < suffix_key.size())
                    continue;
                if (key.compare(key.size() - suffix_key.size(), suffix_key.size(), suffix_key) != 0)
                    continue;
            }
            result.push_back(key);
        }
        std::sort(result.begin(), result.end());
        return result;
    }

private:
    std::string dir_path_;
    std::string archive_base_;   
    std::unordered_map<std::string, DirEntry> entries_;
    std::vector<uint8_t> embedded_data_;
    bool opened_ = false;

    static std::string derive_archive_base(const std::string& path) {
        const auto ext = path.rfind(".vpk");
        if (ext == std::string::npos)
            return path;
        /** Valve: pak01_dir.vpk / 3232466864_dir.vpk -> stem + '_' */
        if (ext >= 4 && path.compare(ext - 4, 4, "_dir") == 0)
            return path.substr(0, ext - 3);
        /** Chunk dosyasi: foo_000.vpk -> foo_ */
        if (ext >= 4 && path[ext - 4] == '_') {
            bool digits = true;
            for (size_t i = ext - 3; i < ext; ++i) {
                if (!std::isdigit(static_cast<unsigned char>(path[i]))) {
                    digits = false;
                    break;
                }
            }
            if (digits)
                return path.substr(0, ext - 3);
        }
        /** de_dust2.vpk gibi tek parca / map vpk: tam stem + '_' (eski kod de_dust_ yapıyordu). */
        return path.substr(0, ext) + "_";
    }

    bool parse_tree(const std::vector<uint8_t>& tree) {
        const uint8_t* buf  = tree.data();
        const size_t   size = tree.size();
        size_t pos = 0;

        std::string ext, path, filename;

        while (pos < size) {
            if (!detail::read_cstring(buf, size, pos, ext)) return false;
            if (ext.empty()) break;  

            while (pos < size) {
                if (!detail::read_cstring(buf, size, pos, path)) return false;
                if (path.empty()) break;  

                while (pos < size) {
                    if (!detail::read_cstring(buf, size, pos, filename)) return false;
                    if (filename.empty()) break;  

                    if (pos + sizeof(RawDirEntry) + 2 > size) return false;
                    RawDirEntry raw{};
                    std::memcpy(&raw, buf + pos, sizeof(RawDirEntry));
                    pos += sizeof(RawDirEntry);

                    uint16_t terminator = 0;
                    std::memcpy(&terminator, buf + pos, 2);
                    pos += 2;
                    if (terminator != 0xFFFF) return false;

                    DirEntry entry;
                    entry.crc32         = raw.crc32;
                    entry.archive_index = raw.archive_index;
                    entry.entry_offset  = raw.entry_offset;
                    entry.entry_length  = raw.entry_length;

                    if (raw.preload_bytes > 0) {
                        if (pos + raw.preload_bytes > size) return false;
                        entry.preload_data.assign(
                            buf + pos,
                            buf + pos + raw.preload_bytes);
                        pos += raw.preload_bytes;
                    }

                    std::string key;
                    key.reserve(path.size() + 1 + filename.size() + 1 + ext.size());
                    if (path != " ") { key += path; key += '/'; }
                    key += filename;
                    key += '.';
                    key += ext;
                    
                    for (char& c : key)
                        if (c >= 'A' && c <= 'Z')
                            c = static_cast<char>(c - 'A' + 'a');

                    entries_.emplace(std::move(key), std::move(entry));
                }
            }
        }

        return true;
    }
};

inline std::vector<std::string> cs2_default_vpk_paths() {
    std::vector<std::string> paths;
    paths.reserve(16);

    auto push_unique = [&](std::string p) {
        for (char& c : p) if (c == '\\') c = '/';
        while (p.size() > 1 && p.back() == '/') p.pop_back();
        for (const auto& existing : paths)
            if (existing == p) return;
        paths.push_back(std::move(p));
    };

    static constexpr const char* k_cs2_rel =
        "/steamapps/common/Counter-Strike Global Offensive/game/csgo/pak01_dir.vpk";

#ifdef _WIN32
    
    auto reg_query = [](HKEY root, const char* subkey, const char* value) -> std::string {
        HKEY hkey = nullptr;
        if (RegOpenKeyExA(root, subkey, 0, KEY_READ | KEY_WOW64_32KEY, &hkey) != ERROR_SUCCESS)
            return {};
        char buf[1024] = {};
        DWORD size = sizeof(buf);
        bool ok = (RegQueryValueExA(hkey, value, nullptr, nullptr,
                                    reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS);
        RegCloseKey(hkey);
        return ok ? std::string(buf) : std::string{};
    };

    std::vector<std::string> steam_roots;
    steam_roots.reserve(8);

    auto push_steam_root = [&](std::string base) {
        if (base.empty()) return;
        for (char& c : base) if (c == '\\') c = '/';
        while (base.size() > 1 && base.back() == '/') base.pop_back();
        for (const auto& s : steam_roots)
            if (s == base) return;
        steam_roots.push_back(std::move(base));
    };

    push_steam_root(reg_query(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath"));
    
    push_steam_root(reg_query(HKEY_CURRENT_USER,
        "SOFTWARE\\Valve\\Steam", "SteamPath"));

    auto parse_library_folders = [&](const std::string& steam_root) {
        const std::string vdf_path = steam_root + "/steamapps/libraryfolders.vdf";
        std::string vdf_text;
        if (!detail::read_text_file_utf8(vdf_path, vdf_text)) return;
        std::istringstream vdf(vdf_text);

        std::string line;
        while (std::getline(vdf, line)) {
            
            auto key_pos = line.find("\"path\"");
            if (key_pos == std::string::npos) continue;

            auto val_start = line.find('"', key_pos + 6);
            if (val_start == std::string::npos) continue;
            val_start++;
            auto val_end = line.find('"', val_start);
            if (val_end == std::string::npos) continue;

            std::string lib_path = line.substr(val_start, val_end - val_start);
            
            std::string unescaped;
            unescaped.reserve(lib_path.size());
            for (size_t i = 0; i < lib_path.size(); ++i) {
                if (lib_path[i] == '\\' && i + 1 < lib_path.size() && lib_path[i + 1] == '\\') {
                    unescaped += '/';
                    ++i; 
                } else if (lib_path[i] == '\\') {
                    unescaped += '/';
                } else {
                    unescaped += lib_path[i];
                }
            }
            push_steam_root(std::move(unescaped));
        }
    };

    for (const auto& root : steam_roots) {
        parse_library_folders(root);
    }
    
    const size_t initial_count = steam_roots.size();
    for (size_t i = 0; i < initial_count; ++i) {
        parse_library_folders(steam_roots[i]);
    }

    for (const auto& root : steam_roots) {
        push_unique(root + k_cs2_rel);
    }

    for (char drive = 'C'; drive <= 'Z'; ++drive) {
        char root_path[] = { drive, ':', '\\', '\0' };
        if (GetDriveTypeA(root_path) != DRIVE_FIXED) continue;

        std::string d(1, drive);
        d += ':';
        const std::string steam_dirs[] = {
            d + "/Program Files (x86)/Steam",
            d + "/Program Files/Steam",
            d + "/Steam",
            d + "/SteamLibrary",
        };
        for (const auto& sd : steam_dirs) {
            push_unique(sd + k_cs2_rel);
        }
    }
#endif

    return paths;
}

inline std::vector<std::string> find_workshop_map_vpks(const std::string& map_name) {
    std::vector<std::string> paths;
    paths.reserve(4);

    auto normalize_path = [](std::string p) {
        for (char& c : p) if (c == '\\') c = '/';
        while (p.size() > 1 && p.back() == '/') p.pop_back();
        return p;
    };

    std::vector<std::string> steam_roots;
    steam_roots.reserve(8);

#ifdef _WIN32
    auto reg_query = [](HKEY root, const char* subkey, const char* value) -> std::string {
        HKEY hkey = nullptr;
        if (RegOpenKeyExA(root, subkey, 0, KEY_READ | KEY_WOW64_32KEY, &hkey) != ERROR_SUCCESS)
            return {};
        char buf[1024] = {};
        DWORD size = sizeof(buf);
        bool ok = (RegQueryValueExA(hkey, value, nullptr, nullptr,
                                    reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS);
        RegCloseKey(hkey);
        return ok ? std::string(buf) : std::string{};
    };

    auto push_steam_root = [&](std::string base) {
        if (base.empty()) return;
        base = normalize_path(base);
        for (const auto& s : steam_roots)
            if (s == base) return;
        steam_roots.push_back(std::move(base));
    };

    push_steam_root(reg_query(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath"));
    
    push_steam_root(reg_query(HKEY_CURRENT_USER,
        "SOFTWARE\\Valve\\Steam", "SteamPath"));

    auto parse_library_folders = [&](const std::string& steam_root) {
        const std::string vdf_path = steam_root + "/steamapps/libraryfolders.vdf";
        std::string vdf_text;
        if (!detail::read_text_file_utf8(vdf_path, vdf_text)) return;
        std::istringstream vdf(vdf_text);

        std::string line;
        while (std::getline(vdf, line)) {
            auto key_pos = line.find("\"path\"");
            if (key_pos == std::string::npos) continue;

            auto val_start = line.find('"', key_pos + 6);
            if (val_start == std::string::npos) continue;
            val_start++;
            auto val_end = line.find('"', val_start);
            if (val_end == std::string::npos) continue;

            std::string lib_path = line.substr(val_start, val_end - val_start);
            std::string unescaped;
            unescaped.reserve(lib_path.size());
            for (size_t i = 0; i < lib_path.size(); ++i) {
                if (lib_path[i] == '\\' && i + 1 < lib_path.size() && lib_path[i + 1] == '\\') {
                    unescaped += '/';
                    ++i;
                } else if (lib_path[i] == '\\') {
                    unescaped += '/';
                } else {
                    unescaped += lib_path[i];
                }
            }
            push_steam_root(std::move(unescaped));
        }
    };

    for (const auto& root : steam_roots) {
        parse_library_folders(root);
    }
    const size_t initial_count = steam_roots.size();
    for (size_t i = 0; i < initial_count; ++i) {
        parse_library_folders(steam_roots[i]);
    }

    for (char drive = 'C'; drive <= 'Z'; ++drive) {
        char root_path[] = { drive, ':', '\\', '\0' };
        if (GetDriveTypeA(root_path) != DRIVE_FIXED) continue;

        std::string d(1, drive);
        d += ':';
        const std::string steam_dirs[] = {
            d + "/Program Files (x86)/Steam",
            d + "/Program Files/Steam",
            d + "/Steam",
            d + "/SteamLibrary",
        };
        for (const auto& sd : steam_dirs) {
            push_steam_root(normalize_path(sd));
        }
    }

    for (const auto& root : steam_roots) {
        const std::string workshop_dir = root + "/steamapps/workshop/content/730";
        
        WIN32_FIND_DATAA ffd;
        HANDLE find_handle = FindFirstFileA((workshop_dir + "/*").c_str(), &ffd);
        if (find_handle == INVALID_HANDLE_VALUE) continue;

        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (ffd.cFileName[0] == '.') continue;  

            std::string map_id = ffd.cFileName;
            const std::string map_id_dir = workshop_dir + "/" + map_id;

            WIN32_FIND_DATAA ffd2;
            HANDLE find_handle2 = FindFirstFileA((map_id_dir + "/maps/*").c_str(), &ffd2);
            if (find_handle2 == INVALID_HANDLE_VALUE) continue;

            do {
                if (ffd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                
                std::string filename = ffd2.cFileName;
                
                if (filename.size() > 4) {
                    std::string ext = filename.substr(filename.size() - 4);
                    for (char& c : ext) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
                    if (ext == ".vpk") {
                        std::string base = filename.substr(0, filename.size() - 4);
                        for (char& c : base) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
                        
                        std::string target = map_name;
                        for (char& c : target) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );

                        /** `de_dust2.vpk` ile oyun adi `de_dust2_2017` (workshop) eslesir. */
                        const auto workshop_vpk_matches = [](const std::string& vpk_base_lower,
                                                             const std::string& want_lower) -> bool {
                            if (vpk_base_lower == want_lower)
                                return true;
                            if (want_lower.size() > vpk_base_lower.size() + 1
                                && want_lower.compare(0, vpk_base_lower.size(), vpk_base_lower) == 0
                                && want_lower[vpk_base_lower.size()] == '_')
                                return true;
                            if (vpk_base_lower.size() > want_lower.size() + 1
                                && vpk_base_lower.compare(0, want_lower.size(), want_lower) == 0
                                && vpk_base_lower[want_lower.size()] == '_')
                                return true;
                            return false;
                        };
                        if (workshop_vpk_matches(base, target)) {
                            paths.push_back(normalize_path(map_id_dir + "/maps/" + filename));
                        }
                    }
                }
            } while (FindNextFileA(find_handle2, &ffd2));
            FindClose(find_handle2);

        } while (FindNextFileA(find_handle, &ffd));
        FindClose(find_handle);
    }

#endif

    return paths;
}

inline std::vector<std::string> find_workshop_map_container_vpks() {
    std::vector<std::string> paths;
    paths.reserve(16);

    auto normalize_path = [](std::string p) {
        for (char& c : p) if (c == '\\') c = '/';
        while (p.size() > 1 && p.back() == '/') p.pop_back();
        return p;
    };

#ifdef _WIN32
    auto reg_query = [](HKEY root, const char* subkey, const char* value) -> std::string {
        HKEY hkey = nullptr;
        if (RegOpenKeyExA(root, subkey, 0, KEY_READ | KEY_WOW64_32KEY, &hkey) != ERROR_SUCCESS)
            return {};
        char buf[1024] = {};
        DWORD size = sizeof(buf);
        bool ok = (RegQueryValueExA(hkey, value, nullptr, nullptr,
                                    reinterpret_cast<LPBYTE>(buf), &size) == ERROR_SUCCESS);
        RegCloseKey(hkey);
        return ok ? std::string(buf) : std::string{};
    };

    std::vector<std::string> steam_roots;

    auto push_steam_root = [&](std::string base) {
        if (base.empty()) return;
        base = normalize_path(base);
        for (const auto& s : steam_roots)
            if (s == base) return;
        steam_roots.push_back(std::move(base));
    };

    push_steam_root(reg_query(HKEY_LOCAL_MACHINE,
        "SOFTWARE\\WOW6432Node\\Valve\\Steam", "InstallPath"));
    push_steam_root(reg_query(HKEY_CURRENT_USER,
        "SOFTWARE\\Valve\\Steam", "SteamPath"));

    auto parse_library_folders = [&](const std::string& steam_root) {
        const std::string vdf_path = steam_root + "/steamapps/libraryfolders.vdf";
        std::string vdf_text;
        if (!detail::read_text_file_utf8(vdf_path, vdf_text)) return;
        std::istringstream vdf(vdf_text);

        std::string line;
        while (std::getline(vdf, line)) {
            auto key_pos = line.find("\"path\"");
            if (key_pos == std::string::npos) continue;
            auto val_start = line.find('"', key_pos + 6);
            if (val_start == std::string::npos) continue;
            val_start++;
            auto val_end = line.find('"', val_start);
            if (val_end == std::string::npos) continue;
            std::string lib_path = line.substr(val_start, val_end - val_start);
            std::string unescaped;
            unescaped.reserve(lib_path.size());
            for (size_t i = 0; i < lib_path.size(); ++i) {
                if (lib_path[i] == '\\' && i + 1 < lib_path.size() && lib_path[i + 1] == '\\') {
                    unescaped += '/'; ++i;
                } else if (lib_path[i] == '\\') {
                    unescaped += '/';
                } else {
                    unescaped += lib_path[i];
                }
            }
            push_steam_root(std::move(unescaped));
        }
    };

    for (const auto& root : steam_roots) {
        parse_library_folders(root);
    }

    for (char drive = 'C'; drive <= 'Z'; ++drive) {
        char root_path[] = { drive, ':', '\\', '\0' };
        if (GetDriveTypeA(root_path) != DRIVE_FIXED) continue;
        std::string d(1, drive);
        d += ':';
        const std::string steam_dirs[] = {
            d + "/Program Files (x86)/Steam",
            d + "/Program Files/Steam",
            d + "/Steam",
            d + "/SteamLibrary",
        };
        for (const auto& sd : steam_dirs) {
            push_steam_root(normalize_path(sd));
        }
    }

    for (const auto& root : steam_roots) {
        const std::string workshop_dir = root + "/steamapps/workshop/content/730";
        
        WIN32_FIND_DATAA ffd;
        HANDLE find_handle = FindFirstFileA((workshop_dir + "/*").c_str(), &ffd);
        if (find_handle == INVALID_HANDLE_VALUE) continue;

        do {
            if (!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
            if (ffd.cFileName[0] == '.') continue;

            std::string map_id = ffd.cFileName;
            const std::string map_id_dir = workshop_dir + "/" + map_id;

            WIN32_FIND_DATAA ffd2;
            HANDLE find_handle2 = FindFirstFileA((map_id_dir + "/*").c_str(), &ffd2);
            if (find_handle2 == INVALID_HANDLE_VALUE) continue;

            do {
                if (ffd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
                
                std::string filename = ffd2.cFileName;
                if (filename.size() > 4) {
                    std::string ext = filename.substr(filename.size() - 4);
                    for (char& c : ext) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
                    if (ext == ".vpk") {
                        /** Workshop konteyneri herhangi bir adda olabilir (`<addonid>.vpk`, `<map>_dir.vpk`, `<map>_000.vpk`). */
                        std::string fn_lower = filename;
                        for (char& c : fn_lower) c = static_cast<char>( std::tolower( static_cast<unsigned char>( c ) ) );
                        /** `<map>_001.vpk`..`_999.vpk` chunk dosyalari hep ana `_dir.vpk` ile birlikte gelir; sadece `_dir`/`_000`/plain adlari yeter. */
                        const bool is_chunk = (fn_lower.size() > 8) &&
                            std::isdigit(static_cast<unsigned char>(fn_lower[fn_lower.size() - 5])) &&
                            std::isdigit(static_cast<unsigned char>(fn_lower[fn_lower.size() - 6])) &&
                            std::isdigit(static_cast<unsigned char>(fn_lower[fn_lower.size() - 7])) &&
                            fn_lower[fn_lower.size() - 8] == '_' &&
                            fn_lower.find("_000.vpk") == std::string::npos;
                        if (!is_chunk)
                            paths.push_back(normalize_path(map_id_dir + "/" + filename));
                    }
                }
            } while (FindNextFileA(find_handle2, &ffd2));
            FindClose(find_handle2);

        } while (FindNextFileA(find_handle, &ffd));
        FindClose(find_handle);
    }

#endif

    return paths;
}

}
