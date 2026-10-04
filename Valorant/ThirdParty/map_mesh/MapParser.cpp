#include "MapParser.hpp"

#include "../vpk-parser/VPK.hpp"
#include "Source2.hpp"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace map_parser {

static PhysFilterFlags s_phys_filter{};
void            set_phys_filter(const PhysFilterFlags& f) { s_phys_filter = f; }
PhysFilterFlags get_phys_filter() { return s_phys_filter; }

struct hedge_t {
    std::uint8_t next;
    std::uint8_t twin;
    std::uint8_t vert;
    std::uint8_t face;
};

static std::string normalize_map_name(std::string map_name) {
    if (map_name.empty()) return {};
    for (char& c : map_name) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    if (const auto slash = map_name.find_last_of('/'); slash != std::string::npos)
        map_name = map_name.substr(slash + 1);
    if (map_name.size() > 4 && map_name.compare(map_name.size() - 4, 4, ".vpk") == 0)
        map_name.resize(map_name.size() - 4);
    if (map_name.size() > 4 && map_name.compare(map_name.size() - 4, 4, ".bsp") == 0)
        map_name.resize(map_name.size() - 4);
    if (map_name == "<empty>") return {};
    return map_name;
}

/** `de_dust2_2017` -> `de_dust2_2017`, `de_dust2` (VPK dosya adi / ic `maps/<stem>/`). */
static void append_map_stem_variants(const std::string& map_name, std::vector<std::string>& out) {
    out.clear();
    if (map_name.empty())
        return;
    out.push_back(map_name);
    std::string cur = map_name;
    for (;;) {
        const size_t u = cur.rfind('_');
        if (u == std::string::npos || u < 3u)
            break;
        const std::string tail = cur.substr(u + 1);
        bool all_digit = !tail.empty();
        for (unsigned char ch : tail) {
            if (!std::isdigit(ch)) {
                all_digit = false;
                break;
            }
        }
        if (!all_digit)
            break;
        cur.resize(u);
        bool dup = false;
        for (const std::string& e : out)
            if (e == cur) {
                dup = true;
                break;
            }
        if (!dup)
            out.push_back(cur);
    }
    const auto push_new = [&out](const std::string& s) {
        if (s.empty())
            return;
        for (const std::string& e : out)
            if (e == s)
                return;
        out.push_back(s);
    };
    if (map_name.size() > 3u) {
        if (map_name.compare(0, 3, "mg_") == 0)
            push_new(map_name.substr(3));
        if (map_name.compare(0, 3, "gd_") == 0)
            push_new(map_name.substr(3));
    }
}

static std::string normalize_resource_path(std::string path,
                                           const char* src_ext,
                                           const char* cmp_ext) {
    for (char& c : path) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    const std::string se = src_ext;
    const std::string ce = cmp_ext;
    if (path.size() >= ce.size() &&
        path.compare(path.size() - ce.size(), ce.size(), ce) == 0)
        return path;
    if (path.size() >= se.size() &&
        path.compare(path.size() - se.size(), se.size(), se) == 0) {
        path += "_c";
        return path;
    }
    return path + ce;
}

static std::optional<std::vector<uint8_t>>
read_vpk_with_fallback(const vpk::VPKDir& dir, const std::string& path) {
    if (auto bytes = dir.read_file(path)) return bytes;
    const auto slash = path.rfind('/');
    const std::string filename = (slash != std::string::npos) ? path.substr(slash + 1) : path;
    if (const auto alt = dir.find_by_filename(filename))
        return dir.read_file(*alt);
    return std::nullopt;
}

static std::optional<std::vector<uint8_t>>
read_resource_global(const vpk::VPKDir& map_vpk, const std::string& path) {
    if (auto bytes = read_vpk_with_fallback(map_vpk, path)) return bytes;

    static std::vector<vpk::VPKDir> global_vpks;
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        for (const auto& pak_path : vpk::cs2_default_vpk_paths()) {
            vpk::VPKDir d;
            if (d.open(pak_path)) global_vpks.push_back(std::move(d));
        }
    }
    for (const auto& d : global_vpks) {
        if (!d.is_open()) continue;
        if (auto bytes = read_vpk_with_fallback(d, path)) return bytes;
    }
    return std::nullopt;
}

static void str_to_lower_ascii(std::string& s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
}

static std::string vpk_entry_stem_lower(const std::string& vpk_entry_path) {
    const auto slash = vpk_entry_path.find_last_of('/');
    std::string base = (slash == std::string::npos) ? vpk_entry_path : vpk_entry_path.substr(slash + 1);
    const auto dot = base.rfind('.');
    if (dot != std::string::npos)
        base.resize(dot);
    str_to_lower_ascii(base);
    return base;
}

/** Radar `TryWorkshopRadarVpkOpen` ile ayni eslesme: tam ad, alt dize, `de_dust2` <-> `de_dust2_2017`. */
static bool map_key_matches_vpk_entry(const std::vector<std::string>& keys, const std::string& entry_path) {
    const std::string base = vpk_entry_stem_lower(entry_path);
    if (base.empty())
        return false;
    std::string flo = entry_path;
    str_to_lower_ascii(flo);
    for (const std::string& raw : keys) {
        std::string k = raw;
        str_to_lower_ascii(k);
        if (k.empty())
            continue;
        if (base == k || flo.find(k) != std::string::npos)
            return true;
        if (k.size() > base.size() + 1 && k.compare(0, base.size(), base) == 0 && k[base.size()] == '_')
            return true;
        if (base.size() > k.size() + 1 && base.compare(0, k.size(), k) == 0 && base[k.size()] == '_')
            return true;
    }
    return false;
}

static const std::vector<std::string>& workshop_container_cache() {
    static std::vector<std::string> cache;
    static bool built = false;
    if (!built) {
        cache = vpk::find_workshop_map_container_vpks();
        built = true;
    }
    return cache;
}

static bool try_open_nested_bytes(vpk::VPKDir& out_vpk, const std::vector<uint8_t>& bytes,
                                  const std::string& opened_label, const std::string& nested_path,
                                  std::string& opened_path, std::string& interior_map_name) {
    if (bytes.empty() || !out_vpk.open_from_bytes(bytes))
        return false;
    opened_path = opened_label;
    interior_map_name = vpk_entry_stem_lower(nested_path);
    return true;
}

static bool container_has_world_physics(vpk::VPKDir& dir, const std::string& interior_stem) {
    const std::string phys = "maps/" + interior_stem + "/world_physics.vmdl_c";
    const auto bytes = dir.read_file(phys);
    return bytes && !bytes->empty();
}

static bool open_map_vpk(vpk::VPKDir& out_vpk,
                         const std::string& map_name,
                         std::string& opened_path,
                         std::string& interior_map_name) {
    interior_map_name.clear();
    std::vector<std::string> names;
    append_map_stem_variants(map_name, names);

    for (const std::string& mn : names) {
        for (const auto& pak_path : vpk::cs2_default_vpk_paths()) {
            const auto slash = pak_path.find_last_of('/');
            if (slash == std::string::npos) continue;
            const auto base_dir = pak_path.substr(0, slash);
            const auto candidate = base_dir + "/maps/" + mn + ".vpk";
            if (out_vpk.open(candidate)) {
                opened_path = candidate;
                interior_map_name = mn;
                return true;
            }
        }
    }

    for (const auto& container_path : workshop_container_cache()) {
        vpk::VPKDir container_vpk;
        if (!container_vpk.open(container_path))
            continue;

        for (const std::string& mn : names) {
            const std::string nested_vpk_path = "maps/" + mn + ".vpk";
            if (auto nested = container_vpk.read_file(nested_vpk_path)) {
                if (try_open_nested_bytes(out_vpk, *nested, container_path + ":" + nested_vpk_path,
                                          nested_vpk_path, opened_path, interior_map_name))
                    return true;
            }
        }

        std::vector<std::string> inner_map_vpks;
        for (const auto& mf : container_vpk.list_files("maps/", ".vpk")) {
            inner_map_vpks.push_back(mf);
            if (!map_key_matches_vpk_entry(names, mf))
                continue;
            const auto nested = container_vpk.read_file(mf);
            if (!nested || nested->empty())
                continue;
            if (try_open_nested_bytes(out_vpk, *nested, container_path + ":" + mf, mf, opened_path,
                                      interior_map_name))
                return true;
        }

        /** Tek-harita workshop paketi: icinde yalnizca bir `maps/*.vpk` varsa onu kullan. */
        if (inner_map_vpks.size() == 1) {
            const std::string& mf = inner_map_vpks.front();
            if (auto nested = container_vpk.read_file(mf)) {
                if (try_open_nested_bytes(out_vpk, *nested, container_path + ":" + mf, mf, opened_path,
                                          interior_map_name))
                    return true;
            }
        }

        /** Bazi workshop paketleri dogrudan konteyner VPK — ic ice `maps/<map>.vpk` yok. */
        for (const std::string& mn : names) {
            std::string stem = mn;
            str_to_lower_ascii(stem);
            if (!container_has_world_physics(container_vpk, stem))
                continue;
            if (out_vpk.open(container_path)) {
                opened_path = container_path;
                interior_map_name = stem;
                return true;
            }
        }
    }

    for (const std::string& mn : names) {
        for (const auto& wp : vpk::find_workshop_map_vpks(mn)) {
            if (!out_vpk.open(wp))
                continue;
            opened_path = wp;
            interior_map_name = vpk_entry_stem_lower(wp);
            if (interior_map_name.empty())
                interior_map_name = mn;
            return true;
        }
    }

    return false;
}

static bool resolve_model_geometry_from_refs(vpk::VPKDir& map_vpk,
                                              source2::ModelData& md) {
    if (md.has_geometry()) return true;
    for (const auto& ref : md.mesh_resources) {
        const std::string mesh_path = normalize_resource_path(ref, ".vmesh", ".vmesh_c");
        auto bytes = read_resource_global(map_vpk, mesh_path);
        if (!bytes) continue;
        auto vbib_opt = source2::parse_vmesh_c(bytes->data(), bytes->size());
        if (!vbib_opt || vbib_opt->vbs.empty() || vbib_opt->ibs.empty()) continue;
        md.vertex_buffers = std::move(vbib_opt->vbs);
        md.index_buffers  = std::move(vbib_opt->ibs);
        md.geometry_source = 3;
        return true;
    }
    return false;
}

static bool append_model_triangles(const source2::ModelData& md,
                                   std::vector<Triangle>& out) {
    static constexpr size_t k_max_tris = 2000000;
    if (out.size() >= k_max_tris) return false;

    const auto part_count = std::min(md.vertex_buffers.size(), md.index_buffers.size());
    if (part_count == 0) return false;

    const auto before = out.size();

    for (size_t part = 0; part < part_count; ++part) {
        const auto& vb = md.vertex_buffers[part];
        const auto& ib = md.index_buffers[part];
        const auto* pos_attr = vb.find_attr("POSITION", 0);
        if (!pos_attr || vb.vertex_count == 0 || ib.index_count < 3) continue;
        if (ib.index_size != 2 && ib.index_size != 4) continue;

        const auto index_count = static_cast<size_t>(ib.index_count);
        const auto required_ib = index_count * static_cast<size_t>(ib.index_size);
        if (ib.data.size() < required_ib) continue;

        for (size_t i = 0; i + 2 < index_count; i += 3) {
            uint32_t i0{}, i1{}, i2{};
            if (ib.index_size == 2) {
                i0 = source2::detail::rd<uint16_t>(ib.data.data() + i * 2);
                i1 = source2::detail::rd<uint16_t>(ib.data.data() + (i + 1) * 2);
                i2 = source2::detail::rd<uint16_t>(ib.data.data() + (i + 2) * 2);
            } else {
                i0 = source2::detail::rd<uint32_t>(ib.data.data() + i * 4);
                i1 = source2::detail::rd<uint32_t>(ib.data.data() + (i + 1) * 4);
                i2 = source2::detail::rd<uint32_t>(ib.data.data() + (i + 2) * 4);
            }
            if (i0 >= vb.vertex_count || i1 >= vb.vertex_count || i2 >= vb.vertex_count)
                continue;

            const auto p0 = source2::read_attr_vec3(vb, i0, *pos_attr);
            const auto p1 = source2::read_attr_vec3(vb, i1, *pos_attr);
            const auto p2 = source2::read_attr_vec3(vb, i2, *pos_attr);

            out.push_back({{p0.x, p0.y, p0.z},
                           {p1.x, p1.y, p1.z},
                           {p2.x, p2.y, p2.z}});

            if (out.size() >= k_max_tris) return false;
        }
    }
    return out.size() > before;
}

static bool append_dmx_hull_triangles(const std::vector<uint8_t>& blob,
                                      std::vector<Triangle>& out) {
    if (blob.size() < 256) return false;

    auto find_token = [&](const char* token, size_t from) -> size_t {
        const size_t n = std::strlen(token);
        if (n == 0 || from >= blob.size() || blob.size() < n) return std::string::npos;
        for (size_t i = from; i + n <= blob.size(); ++i)
            if (std::memcmp(blob.data() + i, token, n) == 0) return i;
        return std::string::npos;
    };

    const size_t pos0_tok = find_token("position$0\0", 0);
    const size_t pos_indices_tok = find_token("position$0Indices\0", 0);
    if (pos0_tok == std::string::npos || pos_indices_tok == std::string::npos ||
        pos_indices_tok <= pos0_tok)
        return false;

    std::size_t vertex_count = 0;
    std::size_t seq_start = std::string::npos;
    for (size_t off = pos_indices_tok; off + 4 * 20 < blob.size(); ++off) {
        uint32_t v0 = source2::detail::rd<uint32_t>(blob.data() + off);
        uint32_t v1 = source2::detail::rd<uint32_t>(blob.data() + off + 4);
        if (v0 != 0 || v1 != 1) continue;
        std::size_t n = 2;
        while (off + (n + 1) * 4 <= blob.size()) {
            auto v = source2::detail::rd<uint32_t>(blob.data() + off + n * 4);
            if (v != n) break;
            ++n;
            if (n > 100000) break;
        }
        if (n >= 8) { vertex_count = n; seq_start = off; break; }
    }
    if (vertex_count < 3 || seq_start == std::string::npos) return false;

    auto plausible = [](float f) {
        return std::isfinite(f) && f > -100000.0f && f < 100000.0f;
    };

    const std::size_t needed_bytes = vertex_count * 3 * sizeof(float);

    std::size_t verts_start = std::string::npos;
    const std::size_t search_begin = pos0_tok;
    const std::size_t search_end = (seq_start > needed_bytes)
                                       ? seq_start - needed_bytes : search_begin;
    for (size_t off = search_begin; off + needed_bytes <= search_end; off += 4) {
        float f0 = source2::detail::rd<float>(blob.data() + off + 0);
        float f1 = source2::detail::rd<float>(blob.data() + off + 4);
        float f2 = source2::detail::rd<float>(blob.data() + off + 8);
        if (!plausible(f0) || !plausible(f1) || !plausible(f2)) continue;
        verts_start = off;
    }
    if (verts_start == std::string::npos || verts_start + needed_bytes > blob.size())
        return false;

    std::vector<Vec3> verts;
    verts.reserve(vertex_count);
    for (std::size_t i = 0; i < vertex_count; ++i) {
        const size_t b = verts_start + i * 12;
        const float x = source2::detail::rd<float>(blob.data() + b + 0);
        const float y = source2::detail::rd<float>(blob.data() + b + 4);
        const float z = source2::detail::rd<float>(blob.data() + b + 8);
        if (!plausible(x) || !plausible(y) || !plausible(z)) return false;
        verts.push_back({x, y, z});
    }

    const size_t faces_tok = find_token("faces\0", pos_indices_tok);
    if (faces_tok == std::string::npos) return false;

    std::vector<int32_t> face_stream;
    for (size_t off = faces_tok; off + 4 <= blob.size(); off += 4) {
        const auto v = source2::detail::rd<int32_t>(blob.data() + off);
        if (v == -1 || (v >= 0 && static_cast<std::size_t>(v) < vertex_count)) {
            face_stream.push_back(v);
            if (face_stream.size() > 2000000) break;
            continue;
        }
        if (!face_stream.empty()) break;
    }
    if (face_stream.size() < 4) return false;

    const auto before = out.size();
    std::vector<int32_t> poly;
    poly.reserve(16);

    auto flush_poly = [&]() {
        if (poly.size() < 3) { poly.clear(); return; }
        const auto i0 = poly[0];
        if (i0 < 0 || static_cast<std::size_t>(i0) >= verts.size()) { poly.clear(); return; }
        for (std::size_t i = 1; i + 1 < poly.size(); ++i) {
            const auto i1 = poly[i];
            const auto i2 = poly[i + 1];
            if (i1 < 0 || i2 < 0) continue;
            if (static_cast<std::size_t>(i1) >= verts.size() ||
                static_cast<std::size_t>(i2) >= verts.size()) continue;
            out.push_back({verts[static_cast<std::size_t>(i0)],
                           verts[static_cast<std::size_t>(i1)],
                           verts[static_cast<std::size_t>(i2)]});
        }
        poly.clear();
    };

    for (const auto idx : face_stream) {
        if (idx == -1) flush_poly();
        else poly.push_back(idx);
    }
    flush_poly();

    return out.size() > before;
}

static bool kv_to_blob_bytes(const source2::kv3::KVValue* v,
                             std::vector<uint8_t>& out) {
    if (!v) return false;
    if (const auto* blob = std::get_if<std::vector<uint8_t>>(&v->data)) {
        out = *blob;
        return !out.empty();
    }
    if (v->is_array()) {
        out.clear();
        out.reserve(v->size());
        for (const auto& elem : v->as_array()) {
            const auto n = elem.as_int();
            if (n < 0 || n > 255) return false;
            out.push_back(static_cast<uint8_t>(n));
        }
        return !out.empty();
    }
    return false;
}

static bool kv_to_float3_array(const source2::kv3::KVValue* v,
                               std::vector<Vec3>& verts) {
    verts.clear();
    if (!v) return false;

    if (const auto* blob = std::get_if<std::vector<uint8_t>>(&v->data)) {
        if (blob->size() < 12 || (blob->size() % 12) != 0) return false;
        verts.reserve(blob->size() / 12);
        for (size_t i = 0; i + 12 <= blob->size(); i += 12) {
            const float x = source2::detail::rd<float>(blob->data() + i + 0);
            const float y = source2::detail::rd<float>(blob->data() + i + 4);
            const float z = source2::detail::rd<float>(blob->data() + i + 8);
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            verts.push_back({x, y, z});
        }
        return !verts.empty();
    }

    if (v->is_array()) {
        const auto& a = v->as_array();
        if (a.size() < 3 || (a.size() % 3) != 0) return false;
        verts.reserve(a.size() / 3);
        for (size_t i = 0; i + 2 < a.size(); i += 3) {
            const float x = static_cast<float>(a[i + 0].as_float());
            const float y = static_cast<float>(a[i + 1].as_float());
            const float z = static_cast<float>(a[i + 2].as_float());
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) return false;
            verts.push_back({x, y, z});
        }
        return !verts.empty();
    }
    return false;
}

static bool append_hull_from_phys_data(const std::vector<Vec3>& verts,
                                       const std::vector<uint8_t>& faces,
                                       const std::vector<hedge_t>& edges,
                                       std::vector<Triangle>& out) {
    if (verts.size() < 3 || faces.empty() || edges.empty()) return false;

    const auto before = out.size();
    for (const auto start_he_u8 : faces) {
        const auto start_he = static_cast<size_t>(start_he_u8);
        if (start_he >= edges.size()) continue;

        std::vector<int> poly;
        poly.reserve(16);
        size_t he = start_he;
        int safety = 0;
        while (safety++ < 256) {
            if (he >= edges.size()) break;
            const auto vi = static_cast<int>(edges[he].vert);
            if (vi >= 0 && static_cast<size_t>(vi) < verts.size())
                poly.push_back(vi);
            he = static_cast<size_t>(edges[he].next);
            if (he == start_he) break;
        }
        if (poly.size() < 3) continue;

        const auto i0 = poly[0];
        for (size_t i = 1; i + 1 < poly.size(); ++i) {
            const auto i1 = poly[i];
            const auto i2 = poly[i + 1];
            if (i0 < 0 || i1 < 0 || i2 < 0) continue;
            if (static_cast<size_t>(i0) >= verts.size() ||
                static_cast<size_t>(i1) >= verts.size() ||
                static_cast<size_t>(i2) >= verts.size()) continue;
            out.push_back({verts[static_cast<size_t>(i0)],
                           verts[static_cast<size_t>(i1)],
                           verts[static_cast<size_t>(i2)]});
        }
    }
    return out.size() > before;
}

static bool append_mesh_from_phys_data(const std::vector<Vec3>& verts,
                                       const std::vector<int32_t>& tris,
                                       std::vector<Triangle>& out) {
    if (verts.size() < 3 || tris.size() < 3 || (tris.size() % 3) != 0) return false;

    const auto before = out.size();
    for (size_t i = 0; i + 2 < tris.size(); i += 3) {
        const auto i0 = tris[i + 0];
        const auto i1 = tris[i + 1];
        const auto i2 = tris[i + 2];
        if (i0 < 0 || i1 < 0 || i2 < 0) continue;
        if (static_cast<size_t>(i0) >= verts.size() ||
            static_cast<size_t>(i1) >= verts.size() ||
            static_cast<size_t>(i2) >= verts.size()) continue;
        out.push_back({verts[static_cast<size_t>(i0)],
                       verts[static_cast<size_t>(i1)],
                       verts[static_cast<size_t>(i2)]});
    }
    return out.size() > before;
}

static bool append_phys_block_triangles(const std::vector<uint8_t>& vmdl_blob,
                                        std::vector<Triangle>& out) {
    auto hdr_opt = source2::parse_res_header(vmdl_blob.data(), vmdl_blob.size());
    if (!hdr_opt) return false;

    const auto* phys_blk = source2::find_block(*hdr_opt, "PHYS");
    if (!phys_blk || phys_blk->offset + phys_blk->size > vmdl_blob.size())
        return false;

    auto kv_opt = source2::kv3::parse_binary(
        vmdl_blob.data() + phys_blk->offset, phys_blk->size);
    if (!kv_opt || !kv_opt->is_object()) return false;

    auto get_first = [](const source2::kv3::KVValue* obj,
                        std::initializer_list<const char*> keys)
        -> const source2::kv3::KVValue* {
        if (!obj || !obj->is_object()) return nullptr;
        for (const auto* k : keys)
            if (const auto* v = obj->get(k)) return v;
        return nullptr;
    };

    std::vector<bool> skip_attr;
    if (const auto* ca = get_first(&*kv_opt,
            {"m_collisionAttributes", "m_CollisionAttributes"}); ca) {
        skip_attr.resize(ca->size(), false);
        for (size_t ai = 0; ai < ca->size(); ++ai) {
            const auto* attr = ca->get(ai);
            if (!attr) continue;
            for (const char* list_key : {
                "m_InteractAsStrings", "m_interactAsStrings",
                "m_InteractExcludeStrings", "m_interactExcludeStrings",
                "m_InteractWithStrings", "m_interactWithStrings"}) {
                const auto* slist = attr->get(list_key);
                if (!slist) continue;
                const size_t n = slist->size();
                for (size_t si = 0; si < n; ++si) {
                    const auto* sv = slist->get(si);
                    if (!sv || !sv->is_string()) continue;
                    const auto& s = sv->as_string();
                    const auto& pf = s_phys_filter;
                    if ((pf.skip_playerclip     && s.find("playerclip")     != std::string::npos) ||
                        (pf.skip_grenadeclip    && s.find("grenadeclip")    != std::string::npos) ||
                        (pf.skip_droneclip      && s.find("droneclip")      != std::string::npos) ||
                        (pf.skip_trigger        && s.find("trigger")        != std::string::npos) ||
                        (pf.skip_sky            && s.find("sky")            != std::string::npos) ||
                        (pf.skip_toolsinvisible && s.find("toolsinvisible") != std::string::npos) ||
                        (pf.skip_clip           && s.find("clip")           != std::string::npos)) {
                        skip_attr[ai] = true;
                    }
                }
            }
        }
    }
    auto should_skip_part = [&](const source2::kv3::KVValue* part) -> bool {
        if (skip_attr.empty()) return false;
        const auto* idx_v = get_first(part,
            {"m_nCollisionAttributeIndex", "m_collisionAttributeIndex"});
        if (!idx_v) return false;
        const auto idx = static_cast<size_t>(idx_v->as_int());
        return idx < skip_attr.size() && skip_attr[idx];
    };

    const auto* parts = get_first(&*kv_opt, {"m_parts", "m_Parts", "parts", "Parts"});
    if (!parts || !parts->is_array()) {
        const auto* phys_obj = get_first(
            &*kv_opt, {"m_physData", "m_PhysData", "m_pData", "m_data"});
        parts = get_first(phys_obj, {"m_parts", "m_Parts", "parts", "Parts"});
    }
    if (!parts || !parts->is_array() || parts->size() == 0) return false;

    std::size_t parsed_hulls = 0;
    std::size_t parsed_meshes = 0;

    for (size_t pi = 0; pi < parts->size(); ++pi) {
        const auto* part = parts->get(pi);
        if (!part || !part->is_object()) continue;

        if (should_skip_part(part)) continue;

        const auto* rn_shape = get_first(part, {"m_rnShape", "m_RnShape", "m_shape"});
        if (!rn_shape || !rn_shape->is_object()) continue;

        if (const auto* hulls = get_first(rn_shape, {"m_hulls", "m_Hulls"});
            hulls && hulls->is_array()) {
            for (size_t hi = 0; hi < hulls->size(); ++hi) {
                const auto* h = hulls->get(hi);
                if (!h || !h->is_object()) continue;

                const auto* hull = get_first(h, {"m_Hull", "m_hull"});
                if (!hull || !hull->is_object()) hull = h;
                if (!hull || !hull->is_object()) continue;

                const auto* vpos = get_first(hull,
                    {"m_VertexPositions", "m_vertexPositions", "m_Vertices", "m_vertices"});
                std::vector<Vec3> verts;
                if (!kv_to_float3_array(vpos, verts)) continue;

                std::vector<uint8_t> faces_raw;
                if (!kv_to_blob_bytes(get_first(hull, {"m_Faces", "m_faces"}), faces_raw))
                    continue;

                std::vector<uint8_t> edges_raw;
                if (!kv_to_blob_bytes(get_first(hull, {"m_Edges", "m_edges"}), edges_raw) ||
                    (edges_raw.size() % 4) != 0)
                    continue;

                std::vector<hedge_t> edges(edges_raw.size() / 4);
                for (size_t ei = 0; ei < edges.size(); ++ei)
                    edges[ei] = {edges_raw[ei * 4 + 0], edges_raw[ei * 4 + 1],
                                 edges_raw[ei * 4 + 2], edges_raw[ei * 4 + 3]};

                if (append_hull_from_phys_data(verts, faces_raw, edges, out))
                    ++parsed_hulls;
            }
        }

        if (const auto* meshes = get_first(rn_shape, {"m_meshes", "m_Meshes"});
            meshes && meshes->is_array()) {
            for (size_t mi = 0; mi < meshes->size(); ++mi) {
                const auto* m = meshes->get(mi);
                if (!m || !m->is_object()) continue;

                const auto* mesh = get_first(m, {"m_Mesh", "m_mesh"});
                if (!mesh || !mesh->is_object()) mesh = m;
                if (!mesh || !mesh->is_object()) continue;

                std::vector<Vec3> verts;
                if (!kv_to_float3_array(get_first(mesh,
                    {"m_Vertices", "m_vertices", "m_VertexPositions", "m_vertexPositions"}),
                    verts)) continue;

                std::vector<int32_t> tris;
                if (const auto* tri_v = get_first(mesh,
                    {"m_Triangles", "m_triangles", "m_Indices", "m_indices"})) {
                    if (const auto* blob = std::get_if<std::vector<uint8_t>>(&tri_v->data)) {
                        if ((blob->size() % 4) == 0) {
                            tris.reserve(blob->size() / 4);
                            for (size_t i = 0; i + 4 <= blob->size(); i += 4)
                                tris.push_back(source2::detail::rd<int32_t>(blob->data() + i));
                        } else if ((blob->size() % 2) == 0) {
                            tris.reserve(blob->size() / 2);
                            for (size_t i = 0; i + 2 <= blob->size(); i += 2)
                                tris.push_back(static_cast<int32_t>(
                                    source2::detail::rd<uint16_t>(blob->data() + i)));
                        }
                    } else if (tri_v->is_array()) {
                        tris.reserve(tri_v->size());
                        for (const auto& t : tri_v->as_array())
                            tris.push_back(static_cast<int32_t>(t.as_int()));
                    }
                }
                if (tris.empty()) continue;

                if (append_mesh_from_phys_data(verts, tris, out))
                    ++parsed_meshes;
            }
        }
    }

    return parsed_hulls > 0 || parsed_meshes > 0;
}

static bool append_dmx_hulls(vpk::VPKDir& map_vpk,
                             const std::string& map_name,
                             std::vector<Triangle>& out) {
    int found = 0, parsed = 0, consecutive_miss = 0;

    for (int i = 0; i < 2048; ++i) {
        std::string suffix;
        if (i > 0) suffix = std::to_string(i);

        const std::string probe_paths[] = {
            "maps/" + map_name + "/world_physics_hull" + suffix + ".dmx",
            "maps/" + map_name + "/world_physics_hull_" + suffix + ".dmx",
            "world_physics_hull" + suffix + ".dmx"
        };

        std::optional<std::vector<uint8_t>> dmx_bytes;
        for (const auto& dmx_path : probe_paths) {
            dmx_bytes = read_resource_global(map_vpk, dmx_path);
            if (dmx_bytes) break;
        }

        if (!dmx_bytes) {
            ++consecutive_miss;
            if (found > 0 && consecutive_miss >= 32) break;
            continue;
        }

        consecutive_miss = 0;
        ++found;
        if (append_dmx_hull_triangles(*dmx_bytes, out))
            ++parsed;
    }

    return parsed > 0;
}

static std::vector<std::string> collect_vmesh_refs_from_blob(
    const std::vector<uint8_t>& blob) {
    std::vector<std::string> refs;
    auto is_path_char = [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
               (ch >= '0' && ch <= '9') || ch == '_' || ch == '/' ||
               ch == '\\' || ch == '.' || ch == '-';
    };
    for (size_t i = 0; i + 6 < blob.size(); ++i) {
        if (blob[i] != '.' || blob[i + 1] != 'v' || blob[i + 2] != 'm' ||
            blob[i + 3] != 'e' || blob[i + 4] != 's' || blob[i + 5] != 'h')
            continue;
        size_t ext_end = i + 6;
        if (ext_end + 2 <= blob.size() && blob[ext_end] == '_' && blob[ext_end + 1] == 'c')
            ext_end += 2;
        size_t start = i;
        while (start > 0 && is_path_char(static_cast<char>(blob[start - 1]))) --start;
        if (ext_end <= start) continue;
        std::string ref(reinterpret_cast<const char*>(blob.data() + start), ext_end - start);
        for (char& c : ref) {
            if (c == '\\') c = '/';
            else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        }
        if (ref.find(".vmesh") == std::string::npos) continue;
        if (std::find(refs.begin(), refs.end(), ref) == refs.end())
            refs.push_back(std::move(ref));
    }
    return refs;
}

static bool build_from_world_physics(vpk::VPKDir& map_vpk,
                                     const std::string& map_name,
                                     std::vector<Triangle>& out) {
    const std::string phys_path = "maps/" + map_name + "/world_physics.vmdl_c";
    auto bytes = read_resource_global(map_vpk, phys_path);
    if (!bytes || bytes->empty()) return false;

    auto md_opt = source2::parse_vmdl_c(bytes->data(), bytes->size());
    if (md_opt) {
        if (resolve_model_geometry_from_refs(map_vpk, *md_opt) &&
            append_model_triangles(*md_opt, out))
            return true;
    }

    if (append_phys_block_triangles(*bytes, out))
        return true;

    const auto raw_refs = collect_vmesh_refs_from_blob(*bytes);
    for (const auto& raw_ref : raw_refs) {
        std::string mesh_path = normalize_resource_path(raw_ref, ".vmesh", ".vmesh_c");
        if (mesh_path.find('/') == std::string::npos)
            mesh_path = "maps/" + map_name + "/" + mesh_path;

        auto mesh_bytes = read_resource_global(map_vpk, mesh_path);
        if (!mesh_bytes) continue;

        auto vbib_opt = source2::parse_vmesh_c(mesh_bytes->data(), mesh_bytes->size());
        if (!vbib_opt || vbib_opt->vbs.empty() || vbib_opt->ibs.empty()) continue;

        source2::ModelData fallback_md{};
        fallback_md.vertex_buffers = std::move(vbib_opt->vbs);
        fallback_md.index_buffers  = std::move(vbib_opt->ibs);
        fallback_md.geometry_source = 3;

        if (append_model_triangles(fallback_md, out))
            return true;
    }

    return false;
}

std::string get_debug_info() {
    return "(embedded map_parser: use load_mesh(name))";
}

std::string get_current_map() {
    return {};
}

static std::string s_load_status;
static MapMesh s_loaded_mesh;
std::string get_load_status() { return s_load_status; }

MapMesh load_mesh(const std::string& map_name) {
    MapMesh result;
    const std::string normalized = normalize_map_name(map_name);
    if (normalized.empty()) {
        s_loaded_mesh = {};
        return result;
    }

    vpk::VPKDir map_vpk;
    std::string opened_path;
    std::string interior_map;
    if (!open_map_vpk(map_vpk, normalized, opened_path, interior_map)) {
        s_load_status = "vpk not found for: " + normalized;
        s_loaded_mesh = {};
        return result;
    }

    if (build_from_world_physics(map_vpk, interior_map, result.triangles)) {
        result.valid = true;
        s_load_status = "ok (world_physics tris=" +
                        std::to_string(result.triangles.size()) + ") from " + opened_path;
        s_loaded_mesh = result;
        return result;
    }

    if (append_dmx_hulls(map_vpk, interior_map, result.triangles) &&
        !result.triangles.empty()) {
        result.valid = true;
        s_load_status = "ok (dmx_hull tris=" +
                        std::to_string(result.triangles.size()) + ") from " + opened_path;
        s_loaded_mesh = result;
        return result;
    }

    s_load_status = "no geometry found for: " + normalized;
    s_loaded_mesh = {};
    return result;
}

const MapMesh* get_loaded_mesh() {
    if (!s_loaded_mesh.valid || s_loaded_mesh.triangles.empty()) return nullptr;
    return &s_loaded_mesh;
}

void clear_loaded_mesh() {
    s_loaded_mesh = {};
}

std::vector<WorkshopEntry> list_workshop(const std::string& filter) {
    std::vector<WorkshopEntry> result;
    for (const auto& vpk_path : vpk::find_workshop_map_container_vpks()) {
        vpk::VPKDir dir;
        if (!dir.open(vpk_path)) continue;
        for (const auto& f : dir.list_files("maps/", ".vpk")) {
            const auto slash = f.rfind('/');
            std::string name = (slash != std::string::npos) ? f.substr(slash + 1) : f;
            if (name.size() > 4 && name.compare(name.size() - 4, 4, ".vpk") == 0)
                name = name.substr(0, name.size() - 4);
            if (!filter.empty() && name.find(filter) == std::string::npos) continue;
            result.push_back({name, vpk_path});
        }
    }
    return result;
}

std::vector<Vec2> project_top_down(const MapMesh& mesh,
                                    float canvas_w, float canvas_h) {
    static constexpr std::size_t k_max_segments = 200000;
    if (!mesh.valid || mesh.triangles.empty()) return {};

    float min_x = mesh.triangles[0].v0.x, max_x = min_x;
    float min_y = mesh.triangles[0].v0.y, max_y = min_y;

    auto update_bounds = [&](const Vec3& v) {
        if (!std::isfinite(v.x) || !std::isfinite(v.y)) return;
        min_x = std::min(min_x, v.x); max_x = std::max(max_x, v.x);
        min_y = std::min(min_y, v.y); max_y = std::max(max_y, v.y);
    };
    for (const auto& tri : mesh.triangles) {
        update_bounds(tri.v0);
        update_bounds(tri.v1);
        update_bounds(tri.v2);
    }

    const float rx = max_x - min_x, ry = max_y - min_y;
    if (rx < 1.f || ry < 1.f) return {};

    const float scale = std::min(canvas_w / rx, canvas_h / ry) * 0.95f;

    auto proj = [&](const Vec3& v) -> Vec2 {
        return {(v.x - min_x) * scale, canvas_h - (v.y - min_y) * scale};
    };

    std::vector<Vec2> lines;
    lines.reserve(std::min(mesh.triangles.size() * 6, k_max_segments * 2));

    for (const auto& tri : mesh.triangles) {
        if (lines.size() / 2 >= k_max_segments) break;

        if (!std::isfinite(tri.v0.x) || !std::isfinite(tri.v0.y) ||
            !std::isfinite(tri.v1.x) || !std::isfinite(tri.v1.y) ||
            !std::isfinite(tri.v2.x) || !std::isfinite(tri.v2.y))
            continue;

        const Vec2 pa = proj(tri.v0), pb = proj(tri.v1), pc = proj(tri.v2);

        lines.push_back(pa); lines.push_back(pb);
        lines.push_back(pb); lines.push_back(pc);
        lines.push_back(pc); lines.push_back(pa);
    }

    return lines;
}

}
