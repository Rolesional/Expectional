#include "grenade_lineup_workshop.hpp"
#include "expectional_winio.hpp"
#include "../ThirdParty/vpk-parser/VPK.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <optional>
#include <regex>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace grenade_lineup_workshop {
namespace {

static std::optional<std::string> ExtractBraceBody(const std::string& text, size_t openBrace) {
	if (openBrace >= text.size() || text[openBrace] != '{')
		return std::nullopt;
	int depth = 0;
	for (size_t i = openBrace; i < text.size(); ++i) {
		if (text[i] == '{')
			++depth;
		else if (text[i] == '}') {
			--depth;
			if (depth == 0)
				return text.substr(openBrace + 1, i - openBrace - 1);
		}
	}
	return std::nullopt;
}

static bool FirstMatchStr(const std::string& body, const std::regex& re, std::string& out) {
	std::smatch m;
	if (!std::regex_search(body, m, re) || m.size() < 2)
		return false;
	out = m[1].str();
	
	while (!out.empty() && (unsigned char)out.front() <= ' ')
		out.erase(0, 1);
	while (!out.empty() && (unsigned char)out.back() <= ' ')
		out.pop_back();
	return true;
}

static bool ParseVec3(const std::string& body, const std::string& key, UE4Structs::Vector3& out) {
	const std::string pat = key + R"(\s*=\s*\[\s*([^\]]+)\s*\])";
	std::smatch m;
	if (!std::regex_search(body, m, std::regex(pat)))
		return false;
	std::string inner = m[1].str();
	std::replace(inner.begin(), inner.end(), ',', ' ');
	std::istringstream iss(inner);
	float x = 0, y = 0, z = 0;
	if (!(iss >> x >> y >> z))
		return false;
	out = UE4Structs::Vector3(x, y, z);
	return true;
}

static std::string UnescapeKv3Text(std::string s) {
	std::string o;
	o.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '\\' && i + 1 < s.size()) {
			const char n = s[i + 1];
			if (n == 'n') {
				o.push_back('\n');
				++i;
			} else if (n == 'r') {
				++i;
			} else if (n == 't') {
				o.push_back('\t');
				++i;
			} else if (n == '"' || n == '\\') {
				o.push_back(n);
				++i;
			} else {
				o.push_back(s[i]);
				o.push_back(n);
				++i;
			}
		} else if (s[i] == '\r') {
			if (i + 1 < s.size() && s[i + 1] == '\n')
				++i;
			o.push_back('\n');
		} else
			o.push_back(s[i]);
	}
	return o;
}

struct NodeParsed {
	std::string subtype;
	std::string id;
	std::string master;
	std::string title;
	std::string desc;
	std::string grenade_type;
	std::string text_h_align;
	bool jump_throw = false;
	bool has_pos = false;
	bool has_ang = false;
	bool has_text_position_offset = false;
	UE4Structs::Vector3 pos{};
	UE4Structs::Vector3 ang{};
	UE4Structs::Vector3 text_position_offset{};
};

static NodeParsed ParseBlock(const std::string& body) {
	NodeParsed p{};
	static const std::regex reSub(R"re(SubType\s*=\s*"([^"]*)")re");
	static const std::regex reId(R"re(Id\s*=\s*"([0-9a-fA-F\-]{36})")re");
	static const std::regex reMaster(R"re(MasterNodeId\s*=\s*"([0-9a-fA-F\-]{36})")re");
	static const std::regex reTitle(
		"Title\\s*=\\s*\\{[\\s\\S]*?Text\\s*=\\s*\"((?:\\\\.|[^\"\\\\])*)\"");
	static const std::regex reDesc(
		"Desc\\s*=\\s*\\{[\\s\\S]*?Text\\s*=\\s*\"((?:\\\\.|[^\"\\\\])*)\"");
	static const std::regex reJt(R"(JumpThrow\s*=\s*(true|false))");
	static const std::regex reGt(R"re(GrenadeType\s*=\s*"([^"]*)")re");
	static const std::regex reTha(R"re(TextHorizontalAlign\s*=\s*"([^"]*)")re");
	FirstMatchStr(body, reSub, p.subtype);
	FirstMatchStr(body, reId, p.id);
	FirstMatchStr(body, reMaster, p.master);
	std::string t, d;
	if (FirstMatchStr(body, reTitle, t))
		p.title = UnescapeKv3Text(std::move(t));
	if (FirstMatchStr(body, reDesc, d))
		p.desc = UnescapeKv3Text(std::move(d));
	std::string jt;
	if (FirstMatchStr(body, reJt, jt)) {
		for (char& c : jt)
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		p.jump_throw = (jt == "true");
	}
	FirstMatchStr(body, reGt, p.grenade_type);
	for (char& c : p.grenade_type)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	FirstMatchStr(body, reTha, p.text_h_align);
	UE4Structs::Vector3 tpo{};
	if (ParseVec3(body, "TextPositionOffset", tpo)) {
		p.text_position_offset = tpo;
		p.has_text_position_offset = true;
	}
	p.has_pos = ParseVec3(body, "Position", p.pos);
	p.has_ang = ParseVec3(body, "Angles", p.ang);
	return p;
}

static std::string ExtractMapName(const std::string& text) {
	static const std::regex re(R"re(MapName\s*=\s*"([^"]*)")re");
	std::smatch m;
	if (!std::regex_search(text, m, re) || m.size() < 2)
		return {};
	std::string s = m[1].str();
	while (!s.empty() && (unsigned char)s.front() <= ' ')
		s.erase(0, 1);
	while (!s.empty() && (unsigned char)s.back() <= ' ')
		s.pop_back();
	return s;
}

static std::string TrimWs(std::string s) {
	while (!s.empty() && (unsigned char)s.front() <= ' ') s.erase(0, 1);
	while (!s.empty() && (unsigned char)s.back() <= ' ') s.pop_back();
	return s;
}

static std::string ExtractPackTitle(const std::string& text) {
	size_t scan_end = text.size();
	{
		static const std::regex firstNode(R"(MapAnnotationNode\d+\s*=\s*\{)");
		std::smatch m;
		if (std::regex_search(text, m, firstNode))
			scan_end = static_cast<size_t>(m.position(0));
	}
	const std::string head = text.substr(0, scan_end);
	const auto try_key = [&head](const std::regex& re) -> std::string {
		std::smatch m;
		if (!std::regex_search(head, m, re) || m.size() < 2) return {};
		std::string val = UnescapeKv3Text(m[1].str());
		return TrimWs(std::move(val));
	};
	static const std::regex reTitleNested(
		"Title\\s*=\\s*\\{[\\s\\S]*?Text\\s*=\\s*\"((?:\\\\.|[^\"\\\\])*)\"");
	static const std::regex reTitleStr(R"re(Title\s*=\s*"((?:\\.|[^"\\])*)")re");
	static const std::regex reName(R"re(\bName\s*=\s*"((?:\\.|[^"\\])*)")re");
	static const std::regex reGroup(R"re(GroupName\s*=\s*"((?:\\.|[^"\\])*)")re");
	std::string t = try_key(reTitleNested);
	if (t.empty()) t = try_key(reTitleStr);
	if (t.empty()) t = try_key(reName);
	if (t.empty()) t = try_key(reGroup);
	
	for (char& c : t) {
		if (c == '\r' || c == '\n' || c == '\t') c = ' ';
	}
	while (t.find("  ") != std::string::npos) {
		size_t p = t.find("  ");
		t.replace(p, 2, " ");
	}
	return TrimWs(std::move(t));
}

static std::string SanitizeName(std::string s) {
	for (char& c : s) {
		if (c == '|')
			c = '_';
	}
	std::string o;
	o.reserve(s.size());
	for (size_t i = 0; i < s.size(); ++i) {
		if (s[i] == '\r') {
			if (i + 1 < s.size() && s[i + 1] == '\n')
				++i;
			o.push_back('\n');
		} else
			o.push_back(s[i]);
	}
	while (!o.empty() && (o.front() == ' ' || o.front() == '\t' || o.front() == '\n'))
		o.erase(0, 1);
	while (!o.empty() && (o.back() == ' ' || o.back() == '\t' || o.back() == '\n'))
		o.pop_back();
	return o.empty() ? std::string("Lineup") : o;
}

static bool IsJtToken(const std::string& s, size_t i) {
	if (i + 1 >= s.size() || s[i] != 'j' || s[i + 1] != 't')
		return false;
	if (i > 0 && std::isalnum(static_cast<unsigned char>(s[i - 1])))
		return false;
	if (i + 2 < s.size() && std::isalnum(static_cast<unsigned char>(s[i + 2])))
		return false;
	return true;
}

static bool JtCommaPattern(const std::string& s) {
	for (size_t i = 0; i + 1 < s.size(); ++i) {
		if (!IsJtToken(s, i))
			continue;
		if (i + 2 < s.size()) {
			const char c = s[i + 2];
			if (c == ',' || c == '.' || c == ')')
				return true;
		}
	}
	return false;
}

static int InferThrowType(std::string blob, bool jumpThrowField) {
	for (char& c : blob) {
		if (c == '\r' || c == '\n' || c == '\t')
			c = ' ';
	}
	std::string d;
	d.reserve(blob.size());
	for (unsigned char uc : blob) {
		if (uc <= 127u)
			d.push_back(static_cast<char>(std::tolower(uc)));
		else
			d.push_back(static_cast<char>(uc));
	}
	std::string comp;
	for (char c : d) {
		if (!std::isspace(static_cast<unsigned char>(c)) && c != '-' && c != '_' && c != '+')
			comp.push_back(c);
	}
	if (comp.find("jumpthrow") != std::string::npos)
		jumpThrowField = true;
	for (size_t i = 0; i < d.size(); ++i) {
		if (IsJtToken(d, i)) {
			jumpThrowField = true;
			break;
		}
	}
	if (!jumpThrowField && JtCommaPattern(d))
		jumpThrowField = true;
	if (d.find("spacebar") != std::string::npos || d.find("space +") != std::string::npos ||
	    d.find("+jump") != std::string::npos || d.find("jump+") != std::string::npos)
		jumpThrowField = true;

	if ((d.find("running") != std::string::npos && d.find("lmb") != std::string::npos) ||
	    (d.find("lmb") != std::string::npos && d.find("running") != std::string::npos))
		return 7;
	if (d.find("run") != std::string::npos && d.find("jump") != std::string::npos)
		return 7;

	const bool duck = d.find("duck") != std::string::npos || d.find("crouch") != std::string::npos ||
	    d.find("ctrl") != std::string::npos;
	const bool jt = jumpThrowField || comp.find("jumpthrow") != std::string::npos ||
	    d.find("jump throw") != std::string::npos || (d.find("jump") != std::string::npos && d.find("throw") != std::string::npos);
	const bool forward = d.find("forward") != std::string::npos || d.find("+w") != std::string::npos ||
	    d.find(" w ") != std::string::npos;
	
	bool wWord = false;
	for (size_t i = 0; i < d.size(); ++i) {
		if (d[i] != 'w')
			continue;
		if (i > 0 && std::isalnum(static_cast<unsigned char>(d[i - 1])))
			continue;
		if (i + 1 < d.size() && std::isalnum(static_cast<unsigned char>(d[i + 1])))
			continue;
		wWord = true;
		break;
	}
	const bool fwd2 = forward || wWord;

	if (duck && fwd2 && jt)
		return 5;
	if (duck && jt)
		return 4;
	if (duck && !jt)
		return 3;
	if (fwd2 && jt)
		return 2;
	if (jt)
		return 1;
	if (fwd2)
		return 6;
	return 0;
}

static int NormalizeNadeKind(const std::string& raw) {
	std::string t = raw;
	for (char& c : t)
		c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	if (t.find("inferno") != std::string::npos || t.find("molotov") != std::string::npos ||
	    t.find("molly") != std::string::npos || t.find("incend") != std::string::npos || t.find("fire") != std::string::npos)
		return 1;
	if (t.find("flash") != std::string::npos)
		return 3;
	if (t.find("decoy") != std::string::npos)
		return 4;
	if (t == "he" || t.find("frag") != std::string::npos || t.find("hegrenade") != std::string::npos)
		return 2;
	if (t.find("smoke") != std::string::npos)
		return 0;
	return 0;
}

static std::string UniqueName(std::string base, std::unordered_map<std::string, int>& counts) {
	const int n = ++counts[base];
	if (n == 1)
		return base;
	char buf[32]{};
	std::snprintf(buf, sizeof buf, "_%d", n);
	return base + buf;
}

static void StripKv3TextHeader(std::string& text)
{
	if (text.rfind("<!--", 0) == 0) {
		const size_t end = text.find("-->");
		if (end != std::string::npos)
			text.erase(0, end + 3);
	}
	while (!text.empty() && (unsigned char)text.front() <= ' ')
		text.erase(0, 1);
}

static bool IsPublishDataBaseNameWide(const std::wstring& name)
{
	if (_wcsicmp(name.c_str(), L"publish_data") == 0)
		return true;
	if (name.size() > 13 && _wcsnicmp(name.c_str(), L"publish_data.", 13) == 0)
		return true;
	return false;
}

static bool ShouldSkipLineupFileNameWide(const std::wstring& fileW)
{
	const size_t slash = fileW.find_last_of(L"\\/");
	const std::wstring name = (slash == std::wstring::npos)
	    ? fileW : fileW.substr(slash + 1);
	return IsPublishDataBaseNameWide(name);
}

static bool ExtIsLineupKv3Wide(const std::wstring& fileW)
{
	const wchar_t* dot = wcsrchr(fileW.c_str(), L'.');
	if (!dot)
		return false;
	return _wcsicmp(dot, L".txt") == 0 || _wcsicmp(dot, L".kv3") == 0;
}

static bool ShouldSkipBinaryExtWide(const std::wstring& fileW)
{
	const wchar_t* dot = wcsrchr(fileW.c_str(), L'.');
	if (!dot)
		return false;
	static const wchar_t* kSkip[] = {
		L".vpk", L".png", L".jpg", L".jpeg", L".gif", L".bmp", L".dds",
		L".wav", L".mp3", L".bsp", L".nav", L".vtf", L".vtex", L".vmdl",
		L".vmesh", L".vmat", L".vcs", L".bin", L".gi", L".exr", L".abc",
	};
	for (const wchar_t* ext : kSkip) {
		if (_wcsicmp(dot, ext) == 0)
			return true;
	}
	return false;
}

struct PublishMeta {
	std::wstring dir_wide;
	std::string title;
	std::wstring source_folder_wide;
};

static void NormalizeDirWide(std::wstring& p)
{
	std::replace(p.begin(), p.end(), L'/', L'\\');
	while (!p.empty() && (p.back() == L'\\' || p.back() == L' '))
		p.pop_back();
}

static std::wstring DirKeyWide(std::wstring p)
{
	NormalizeDirWide(p);
	for (wchar_t& c : p) {
		if (c >= L'A' && c <= L'Z')
			c = static_cast<wchar_t>(c - L'A' + L'a');
	}
	return p;
}

static std::wstring ParentDirWide(const std::wstring& path)
{
	const size_t slash = path.find_last_of(L"\\/");
	if (slash == std::wstring::npos)
		return {};
	return path.substr(0, slash);
}

static std::wstring DirOfFileWide(const std::wstring& fileW)
{
	return ParentDirWide(fileW);
}

static bool ParsePublishDataText(const std::string& text, PublishMeta& out)
{
	static const std::regex reTitle(R"re("title"\s+"((?:\\.|[^"\\])*)")re", std::regex::icase);
	static const std::regex reFolder(R"re("source_folder"\s+"([^"]*)")re", std::regex::icase);
	std::smatch m;
	if (std::regex_search(text, m, reTitle) && m.size() >= 2)
		out.title = UnescapeKv3Text(m[1].str());
	if (std::regex_search(text, m, reFolder) && m.size() >= 2) {
		const std::string folder = m[1].str();
		if (!folder.empty())
			out.source_folder_wide = ExpectionalWinIO::Utf8ToWide(folder);
	}
	return !out.title.empty() || !out.source_folder_wide.empty();
}

static bool IsAllDigitsWide(const std::wstring& s)
{
	if (s.empty())
		return false;
	for (wchar_t c : s) {
		if (c < L'0' || c > L'9')
			return false;
	}
	return true;
}

static std::wstring WorkshopItemDirForPath(const std::wstring& ws730_root, const std::wstring& pathW)
{
	if (pathW.size() <= ws730_root.size())
		return {};
	std::wstring rel = pathW.substr(ws730_root.size());
	if (!rel.empty() && (rel[0] == L'\\' || rel[0] == L'/'))
		rel.erase(0, 1);
	std::wstring best;
	size_t i = 0;
	while (i < rel.size()) {
		if (rel[i] == L'\\' || rel[i] == L'/')
			++i;
		const size_t j = rel.find_first_of(L"\\/", i);
		const std::wstring seg = rel.substr(i, j == std::wstring::npos ? rel.size() - i : j - i);
		if (IsAllDigitsWide(seg))
			best = ws730_root + L"\\" + rel.substr(0, j == std::wstring::npos ? rel.size() : j);
		if (j == std::wstring::npos)
			break;
		i = j;
	}
	if (!best.empty())
		return best;
	const std::wstring dir = DirOfFileWide(pathW);
	return dir.empty() ? ws730_root : dir;
}

static std::wstring StemFromFileWide(const std::wstring& fileW)
{
	const size_t slash = fileW.find_last_of(L"\\/");
	const std::wstring name = (slash == std::wstring::npos)
	    ? fileW : fileW.substr(slash + 1);
	const size_t dot = name.find_last_of(L'.');
	return (dot == std::wstring::npos) ? name : name.substr(0, dot);
}

struct WorkshopScanCtx {
	std::wstring ws730_root;
	std::unordered_map<std::wstring, PublishMeta> publish_by_dir;
};

static void ResolvePackForPath(const WorkshopScanCtx& ctx, const std::wstring& pathW,
                               std::string& pack_id, std::string& pack_title)
{
	std::wstring dir = DirOfFileWide(pathW);
	if (dir.empty())
		dir = pathW;
	NormalizeDirWide(dir);
	while (!dir.empty()) {
		const auto it = ctx.publish_by_dir.find(DirKeyWide(dir));
		if (it != ctx.publish_by_dir.end()) {
			pack_id = ExpectionalWinIO::WideToUtf8(it->second.dir_wide.empty() ? dir : it->second.dir_wide);
			pack_title = it->second.title;
			if (pack_title.empty())
				pack_title = ExpectionalWinIO::WideToUtf8(StemFromFileWide(pathW));
			return;
		}
		if (_wcsicmp(dir.c_str(), ctx.ws730_root.c_str()) == 0)
			break;
		dir = ParentDirWide(dir);
	}
	const std::wstring item_dir = WorkshopItemDirForPath(ctx.ws730_root, pathW);
	pack_id = ExpectionalWinIO::WideToUtf8(item_dir.empty() ? pathW : item_dir);
	pack_title = ExpectionalWinIO::WideToUtf8(StemFromFileWide(pathW));
}

static std::string StemFromVpkEntryPath(const std::string& entryPath)
{
	std::string stem = entryPath;
	const size_t slash = stem.find_last_of('/');
	if (slash != std::string::npos)
		stem = stem.substr(slash + 1);
	const size_t dot = stem.rfind('.');
	if (dot != std::string::npos)
		stem = stem.substr(0, dot);
	return stem;
}

static void ConvertKv3Text(const std::string& text, std::vector<ParsedRow>& out,
                            const std::string& pack_id, const std::string& pack_title_fallback) {
	if (text.find("MapAnnotationNode") == std::string::npos)
		return;
	const std::string mapName = NormalizeWorkshopMapName(ExtractMapName(text));
	const std::string mapEsc = mapName.empty() ? "_" : mapName;
	std::string packTitle = ExtractPackTitle(text);
	if (packTitle.empty())
		packTitle = pack_title_fallback;

	static const std::regex nodeRe(R"(MapAnnotationNode(\d+)\s*=\s*\{)");
	std::unordered_map<int, std::string> bodies;
	size_t scan = 0;
	while (scan < text.size()) {
		std::smatch m;
		if (!std::regex_search(
			    text.begin() + static_cast<std::ptrdiff_t>(scan), text.end(), m, nodeRe))
			break;
		const size_t matchStart = scan + static_cast<size_t>(m.position(0));
		const size_t braceOpen = matchStart + m.length(0) - 1;
		int idx = 0;
		try {
			idx = std::stoi(m[1].str());
		} catch (...) {
			scan = matchStart + (std::max)(size_t{1}, static_cast<size_t>(m.length(0)));
			continue;
		}
		const auto inner = ExtractBraceBody(text, braceOpen);
		if (inner)
			bodies[idx] = *inner;
		scan = matchStart + m.length(0);
	}

	std::unordered_map<int, NodeParsed> parsed;
	for (const auto& kv : bodies)
		parsed[kv.first] = ParseBlock(kv.second);

	std::unordered_map<std::string, std::vector<NodeParsed*>> aimByMaster;
	for (auto& kv : parsed) {
		NodeParsed& np = kv.second;
		if (np.subtype == "aim_target" && !np.master.empty())
			aimByMaster[np.master].push_back(&np);
	}

	std::unordered_map<std::string, int> nameSeen;
	for (auto& kv : parsed) {
		NodeParsed& p = kv.second;
		if (p.subtype != "main" || p.id.empty())
			continue;
		auto it = aimByMaster.find(p.id);
		if (it == aimByMaster.end() || it->second.empty())
			continue;
		const NodeParsed* aim = it->second[0];
		if (!p.has_pos || !aim->has_pos || !aim->has_ang)
			continue;

		std::string rawName = SanitizeName(!p.title.empty() ? p.title : (!aim->title.empty() ? aim->title : "Lineup"));
		const std::string nm = UniqueName(std::move(rawName), nameSeen);

		std::string textBlob;
		textBlob.reserve(256);
		if (!aim->desc.empty())
			textBlob += aim->desc + " ";
		if (!p.desc.empty())
			textBlob += p.desc + " ";
		if (!aim->title.empty())
			textBlob += aim->title + " ";
		if (!p.title.empty())
			textBlob += p.title;

		const int ti = InferThrowType(textBlob, p.jump_throw || aim->jump_throw);
		const int kind = NormalizeNadeKind(p.grenade_type);

		const UE4Structs::Vector3 tpoOff = p.has_text_position_offset
			? p.text_position_offset
			: UE4Structs::Vector3(0.f, 0.f, 60.f);
		std::uint8_t hAlign = 0;
		{
			std::string ha = p.text_h_align;
			for (char& c : ha)
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			if (ha == "left")
				hAlign = 1;
			else if (ha == "right")
				hAlign = 2;
		}

		std::string descOut = aim->desc;
		if (!p.desc.empty()) {
			if (!descOut.empty())
				descOut.push_back('\n');
			descOut += p.desc;
		}
		for (char& c : descOut) {
			if (c == '|')
				c = '_';
		}
		{
			std::string norm;
			norm.reserve(descOut.size());
			for (size_t i = 0; i < descOut.size(); ++i) {
				if (descOut[i] == '\r') {
					if (i + 1 < descOut.size() && descOut[i + 1] == '\n')
						++i;
					norm.push_back('\n');
				} else
					norm.push_back(descOut[i]);
			}
			descOut = std::move(norm);
		}
		while (!descOut.empty() && (descOut.front() == ' ' || descOut.front() == '\t'))
			descOut.erase(0, 1);
		while (!descOut.empty() && (descOut.back() == ' ' || descOut.back() == '\t'))
			descOut.pop_back();

		ParsedRow row{};
		row.map = mapEsc;
		row.name = nm;
		row.desc = descOut;
		row.stand = p.pos;
		row.angles = aim->ang;
		row.target = aim->pos;
		row.label_world = UE4Structs::Vector3(
			p.pos.x + tpoOff.x, p.pos.y + tpoOff.y, p.pos.z + tpoOff.z);
		row.label_h_align = hAlign;
		row.throw_idx = ti;
		row.nade_kind = kind;
		row.source_pack_id = pack_id;
		row.source_pack_title = packTitle;
		out.push_back(std::move(row));
	}
}

static void ProcessLooseLineupFileCtx(const WorkshopScanCtx& ctx, const std::wstring& fileW,
                                      std::vector<ParsedRow>& out, bool requireKnownExt)
{
	if (ShouldSkipLineupFileNameWide(fileW))
		return;
	if (requireKnownExt && !ExtIsLineupKv3Wide(fileW))
		return;
	if (ShouldSkipBinaryExtWide(fileW))
		return;
	std::vector<uint8_t> raw;
	if (!ExpectionalWinIO::ReadAllBytesWide(fileW, raw) || raw.empty() || raw.size() > 32u * 1024u * 1024u)
		return;
	std::string text(reinterpret_cast<const char*>(raw.data()), raw.size());
	if (text.find("MapAnnotationNode") == std::string::npos)
		return;
	StripKv3TextHeader(text);
	std::string pack_id;
	std::string pack_title;
	ResolvePackForPath(ctx, fileW, pack_id, pack_title);
	ConvertKv3Text(text, out, pack_id, pack_title);
}

static void IndexPublishDataFiles(const std::wstring& ws730_root, WorkshopScanCtx& ctx)
{
	ctx.ws730_root = ws730_root;
	NormalizeDirWide(ctx.ws730_root);
	ExpectionalWinIO::ForEachFileWide(ws730_root, true, nullptr, [&](const std::wstring& fileW) {
		const size_t slash = fileW.find_last_of(L"\\/");
		const std::wstring name = (slash == std::wstring::npos)
		    ? fileW : fileW.substr(slash + 1);
		if (!IsPublishDataBaseNameWide(name))
			return;
		std::vector<uint8_t> raw;
		if (!ExpectionalWinIO::ReadAllBytesWide(fileW, raw) || raw.empty())
			return;
		const std::string text(reinterpret_cast<const char*>(raw.data()), raw.size());
		PublishMeta meta{};
		if (!ParsePublishDataText(text, meta))
			return;
		const std::wstring dir = DirOfFileWide(fileW);
		if (dir.empty())
			return;
		meta.dir_wide = dir;
		ctx.publish_by_dir[DirKeyWide(dir)] = std::move(meta);
	});
}

static void ScanLooseLineupsCtx(const WorkshopScanCtx& ctx, const std::wstring& root, bool recursive,
                                std::vector<ParsedRow>& out)
{
	if (root.empty() || !ExpectionalWinIO::DirExistsWide(root))
		return;
	ExpectionalWinIO::ForEachFileWide(root, recursive, L".txt",
	    [&](const std::wstring& fileW) { ProcessLooseLineupFileCtx(ctx, fileW, out, true); });
	ExpectionalWinIO::ForEachFileWide(root, recursive, L".kv3",
	    [&](const std::wstring& fileW) { ProcessLooseLineupFileCtx(ctx, fileW, out, true); });
	ExpectionalWinIO::ForEachFileWide(root, recursive, nullptr,
	    [&](const std::wstring& fileW) {
		    if (ExtIsLineupKv3Wide(fileW))
			    return;
		    ProcessLooseLineupFileCtx(ctx, fileW, out, false);
	    });
}

static bool IsDirVpkFileNameWide(const std::wstring& fileW)
{
	const size_t slash = fileW.find_last_of(L"\\/");
	const std::wstring name = (slash == std::wstring::npos)
	    ? fileW : fileW.substr(slash + 1);
	const size_t n = name.size();
	if (n < 9)
		return false;
	return _wcsicmp(name.c_str() + n - 8, L"_dir.vpk") == 0;
}

static void ProcessVpkLineupEntriesCtx(const WorkshopScanCtx& ctx, vpk::VPKDir& vpkDir,
                                       const std::wstring& vpkPathW, std::vector<ParsedRow>& out)
{
	std::string pack_id;
	std::string pack_title;
	ResolvePackForPath(ctx, vpkPathW, pack_id, pack_title);
	if (pack_title.empty())
		pack_title = ExpectionalWinIO::WideToUtf8(StemFromFileWide(vpkPathW));

	std::unordered_set<std::string> seen_entries;
	const auto ingest = [&](const std::vector<std::string>& entries) {
		for (const std::string& entry : entries) {
			if (entry.find("publish_data") != std::string::npos)
				continue;
			if (!seen_entries.insert(entry).second)
				continue;
			const auto blob = vpkDir.read_file(entry);
			if (!blob || blob->empty())
				continue;
			std::string text(reinterpret_cast<const char*>(blob->data()), blob->size());
			if (text.find("MapAnnotationNode") == std::string::npos)
				continue;
			std::string title_fallback = pack_title;
			if (title_fallback.empty())
				title_fallback = StemFromVpkEntryPath(entry);
			StripKv3TextHeader(text);
			ConvertKv3Text(text, out, pack_id, title_fallback);
		}
	};
	ingest(vpkDir.enumerate_paths("annotations/", ".txt"));
	ingest(vpkDir.enumerate_paths("annotations/", ".kv3"));
	ingest(vpkDir.enumerate_paths("", ".txt"));
	ingest(vpkDir.enumerate_paths("", ".kv3"));
}

static void ScanVpkLineupsCtx(const WorkshopScanCtx& ctx, const std::wstring& root,
                              std::vector<ParsedRow>& out)
{
	if (root.empty() || !ExpectionalWinIO::DirExistsWide(root))
		return;
	ExpectionalWinIO::ForEachFileWide(root, true, L".vpk", [&](const std::wstring& fileW) {
		if (!IsDirVpkFileNameWide(fileW))
			return;
		const std::string vpkPath = ExpectionalWinIO::WideToUtf8(fileW);
		vpk::VPKDir vpkDir;
		if (!vpkDir.open(vpkPath))
			return;
		ProcessVpkLineupEntriesCtx(ctx, vpkDir, fileW, out);
	});
}

} 

static void ProcessLooseLineupFile(const std::wstring& fileW, std::vector<ParsedRow>& out)
{
	WorkshopScanCtx ctx{};
	ProcessLooseLineupFileCtx(ctx, fileW, out, true);
}

static void ScanOneDir(const std::wstring& root, bool recursive, std::vector<ParsedRow>& out) {
	WorkshopScanCtx ctx{};
	ScanLooseLineupsCtx(ctx, root, recursive, out);
}

void AppendWorkshopKv3FromDirectory(const std::wstring& dir_wide, std::vector<ParsedRow>& out) {
	if (dir_wide.empty()) return;
	ScanOneDir(dir_wide, false, out);
}

void AppendWorkshopKv3FromDirectoryRecursive(const std::wstring& dir_wide, std::vector<ParsedRow>& out) {
	if (dir_wide.empty()) return;
	ScanOneDir(dir_wide, true, out);
}

void AppendWorkshopKv3FromAddonVpk(const std::wstring& addon_dir_wide, std::vector<ParsedRow>& out)
{
	if (addon_dir_wide.empty() || !ExpectionalWinIO::DirExistsWide(addon_dir_wide))
		return;
	WorkshopScanCtx ctx{};
	ScanVpkLineupsCtx(ctx, addon_dir_wide, out);
}

void AppendAllFromWorkshopContent730(const std::wstring& ws730_root_wide, std::vector<ParsedRow>& out)
{
	if (ws730_root_wide.empty() || !ExpectionalWinIO::DirExistsWide(ws730_root_wide))
		return;

	WorkshopScanCtx ctx{};
	IndexPublishDataFiles(ws730_root_wide, ctx);

	ScanLooseLineupsCtx(ctx, ws730_root_wide, true, out);
	ScanVpkLineupsCtx(ctx, ws730_root_wide, out);

	std::unordered_set<std::wstring> extra_dirs;
	for (const auto& kv : ctx.publish_by_dir) {
		if (kv.second.source_folder_wide.empty() || kv.second.dir_wide.empty())
			continue;
		std::wstring extra = kv.second.dir_wide + L"\\" + kv.second.source_folder_wide;
		NormalizeDirWide(extra);
		if (!ExpectionalWinIO::DirExistsWide(extra))
			continue;
		if (!extra_dirs.insert(DirKeyWide(extra)).second)
			continue;
		ScanLooseLineupsCtx(ctx, extra, true, out);
		ScanVpkLineupsCtx(ctx, extra, out);
	}
}

} 
