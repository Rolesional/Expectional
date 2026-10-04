#pragma once

#include <filesystem>
#include <string>

/** VPK (map_parser) ile mesh cikarip `%LOCALAPPDATA%\\Expectional\\maps\\<map>.tri` yazar. */
void MapTriExport_KickIfNeeded(const std::string& map_clean);

/** Arka planda VPK parse devam ediyor mu. */
bool MapTriExportInProgress() noexcept;

/** Gecerli .tri: boyut 36'nin kati ve yeterli ucgen sayisi. */
bool TriMeshFileValid(const std::filesystem::path& path) noexcept;

/** Bozuk / yetersiz .tri dosyasini siler (yeniden export icin). */
bool RemoveTriMeshFileIfInvalid(const std::filesystem::path& path) noexcept;
