#pragma once
#include <string>
#include <vector>

/** Documents/Expectional/configs — UTF-8 yol (gösterim). */
std::string ExpectionalConfigDirUtf8();
/** Documents/Expectional/lineups — UTF-8 (genis karakterli yollar icin UI). */
std::string ExpectionalLineupsDirUtf8();
/** Windows UTF-16 lineup klasoru (dosya tarama / ShellExecute). */
std::wstring ExpectionalLineupsDirWide();
/** Klasoru + varsayilan readme.txt yoksa yazar. */
void ExpectionalEnsureLineupsDirAndReadme();
/** UTF-8 dosya veya klasor yolunu varsayilan uygulama ile acar (README, Explorer). */
void ExpectionalShellOpenUtf8Path(const char* utf8_path);
/** Lineup klasorunu Explorer'da acar (yoksa olusturur). */
void ExpectionalShellOpenLineupsFolder();
/** readme.txt dosyasini varsayilan editor ile acar. */
void ExpectionalShellOpenLineupsReadme();

/** .cfg uzantisiz dosya adi (ornek: "legit1"). */
std::vector<std::string> ExpectionalConfigList();

bool ExpectionalConfigSave(const char* name_no_ext);
bool ExpectionalConfigLoad(const char* name_no_ext);
bool ExpectionalConfigDelete(const char* name_no_ext);

/** ExpectionalActiveCfgName ile tam config yazimi (autosave). */
bool ExpectionalSaveActiveConfig();
