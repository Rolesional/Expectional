#pragma once

#include <filesystem>
#include <string>

void MapTriExport_KickIfNeeded(const std::string& map_clean);

bool MapTriExportInProgress() noexcept;

bool TriMeshFileValid(const std::filesystem::path& path) noexcept;

bool RemoveTriMeshFileIfInvalid(const std::filesystem::path& path) noexcept;
