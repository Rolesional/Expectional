#pragma once
#include <string>
#include <vector>

std::string ExpectionalConfigDirUtf8();

std::string ExpectionalLineupsDirUtf8();

std::wstring ExpectionalLineupsDirWide();

void ExpectionalEnsureLineupsDirAndReadme();

void ExpectionalShellOpenUtf8Path(const char* utf8_path);

void ExpectionalShellOpenLineupsFolder();

void ExpectionalShellOpenLineupsReadme();

std::vector<std::string> ExpectionalConfigList();

bool ExpectionalConfigSave(const char* name_no_ext);
bool ExpectionalConfigLoad(const char* name_no_ext);
bool ExpectionalConfigDelete(const char* name_no_ext);

bool ExpectionalSaveActiveConfig();
