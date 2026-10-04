#pragma once

#if defined(_MSC_VER) && _MSC_VER >= 1200
#   pragma once
#endif

#include <array>
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <zstd.h>

#include "Meshopt/Meshoptimizer.h"

namespace source2 {

struct Vec3  { float x, y, z; };
struct Vec4  { float x, y, z, w; };

struct Mat3x4 {
    float mat[3][4]{};
    static Mat3x4 identity() {
        Mat3x4 m{};
        m.mat[0][0] = m.mat[1][1] = m.mat[2][2] = 1.f;
        return m;
    }
};

namespace detail {

inline bool lz4_decompress(const uint8_t* __restrict src, size_t src_size,
                            uint8_t*       __restrict dst, size_t dst_size) {
    size_t si = 0, di = 0;
    while (di < dst_size) {
        if (si >= src_size) return false;
        const uint8_t token = src[si++];

        size_t lit_len = token >> 4;
        if (lit_len == 15) {
            uint8_t extra;
            do {
                if (si >= src_size) return false;
                extra    = src[si++];
                lit_len += extra;
            } while (extra == 255);
        }
        if (si + lit_len > src_size)  return false;
        if (di + lit_len > dst_size)  return false;
        std::memcpy(dst + di, src + si, lit_len);
        si += lit_len;
        di += lit_len;

        if (di == dst_size) break;   

        if (si + 2 > src_size) return false;
        const uint16_t moffset = static_cast<uint16_t>(src[si] | (src[si + 1] << 8));
        si += 2;
        if (moffset == 0) return false;

        size_t match_len = (token & 0xF) + 4;  
        if ((token & 0xF) == 15) {
            uint8_t extra;
            do {
                if (si >= src_size) return false;
                extra      = src[si++];
                match_len += extra;
            } while (extra == 255);
        }

        if (di < moffset) return false;         
        if (di + match_len > dst_size) return false;

        const size_t match_start = di - moffset;
        
        for (size_t i = 0; i < match_len; ++i)
            dst[di++] = dst[match_start + i];
    }
    return true;
}

template<typename T>
inline T rd(const uint8_t* p) {
    T v; std::memcpy(&v, p, sizeof(T)); return v;
}

inline bool read_cstr(const uint8_t* buf, size_t buf_size,
                      size_t& pos, std::string& out) {
    out.clear();
    while (pos < buf_size) {
        const char c = static_cast<char>(buf[pos++]);
        if (c == '\0') return true;
        out.push_back(c);
    }
    return false;
}

} 

struct ResBlock {
    char     type[5]{};   
    uint32_t offset = 0;  
    uint32_t size   = 0;
};

struct ResHeader {
    uint32_t file_size      = 0;
    uint16_t header_version = 0;
    uint16_t version        = 0;
    std::vector<ResBlock> blocks;
};

inline std::optional<ResHeader> parse_res_header(const uint8_t* data, size_t size) {
    if (size < 14) return std::nullopt;

    ResHeader hdr;
    hdr.file_size      = detail::rd<uint32_t>(data + 0);
    hdr.header_version = detail::rd<uint16_t>(data + 4);
    hdr.version        = detail::rd<uint16_t>(data + 6);

    if (hdr.header_version != 12) return std::nullopt;  

    const uint32_t block_offset_field = detail::rd<uint32_t>(data + 8);
    const uint32_t block_count        = detail::rd<uint32_t>(data + 12);

    const size_t blocks_start = static_cast<size_t>(8) + block_offset_field;
    const size_t blocks_end   = blocks_start + block_count * 12;
    if (blocks_end > size) return std::nullopt;

    hdr.blocks.reserve(block_count);
    for (uint32_t i = 0; i < block_count; ++i) {
        size_t b = blocks_start + i * 12;

        ResBlock blk{};
        std::memcpy(blk.type, data + b, 4);
        blk.type[4] = '\0';

        const uint32_t rel_off = detail::rd<uint32_t>(data + b + 4);
        blk.offset = static_cast<uint32_t>(b + 4 + rel_off);
        blk.size   = detail::rd<uint32_t>(data + b + 8);

        hdr.blocks.push_back(blk);
    }

    return hdr;
}

inline const ResBlock* find_block(const ResHeader& h, const char* type) {
    for (const auto& b : h.blocks)
        if (std::strncmp(b.type, type, 4) == 0) return &b;
    return nullptr;
}

enum class DxgiFormat : uint32_t {
    UNKNOWN                  = 0,
    R32G32B32A32_FLOAT       = 2,
    R32G32B32_FLOAT          = 6,
    R16G16B16A16_FLOAT       = 10,
    R16G16B16A16_UINT        = 12,
    R32G32_FLOAT             = 16,
    R10G10B10A2_UNORM        = 24,
    R8G8B8A8_UNORM           = 28,
    R8G8B8A8_UINT            = 30,
    R16G16_FLOAT             = 34,
    R16G16_UNORM             = 35,
    R16G16_UINT              = 36,
    R16G16_SNORM             = 37,
    R32_FLOAT                = 41,
    R32_UINT                 = 42,
    R8G8_UNORM               = 49,
    R16_FLOAT                = 54,
    R16_UINT                 = 57,
    R8_UNORM                 = 61,
    R8_UINT                  = 62,
};

struct VertexAttribute {
    std::string  semantic_name;
    uint32_t     semantic_index  = 0;
    DxgiFormat   format          = DxgiFormat::UNKNOWN;
    uint32_t     byte_offset     = 0;   
    uint32_t     slot            = 0;
    uint32_t     slot_type       = 0;
    uint32_t     step_rate       = 0;
};

struct VertexBuffer {
    uint32_t                   vertex_count = 0;
    uint32_t                   vertex_size  = 0;  
    std::vector<VertexAttribute> attributes;
    std::vector<uint8_t>       data;               

    static bool semantic_equals(const std::string& lhs, const char* rhs) {
        if (!rhs) return false;
        if (lhs.size() != std::strlen(rhs)) return false;
        for (size_t i = 0; i < lhs.size(); ++i) {
            const unsigned char a = static_cast<unsigned char>(lhs[i]);
            const unsigned char b = static_cast<unsigned char>(rhs[i]);
            if (std::tolower(a) != std::tolower(b)) return false;
        }
        return true;
    }

    const VertexAttribute* find_attr(const char* name, uint32_t semantic_idx = 0) const {
        for (const auto& a : attributes)
            if (semantic_equals(a.semantic_name, name) && a.semantic_index == semantic_idx) return &a;
        return nullptr;
    }
};

struct IndexBuffer {
    uint32_t             index_count = 0;
    uint32_t             index_size  = 0;   
    std::vector<uint8_t> data;
};

struct VBIB {
    std::vector<VertexBuffer> vbs;
    std::vector<IndexBuffer>  ibs;
};

inline VBIB parse_vbib(const uint8_t* blk_data, size_t blk_size) {
    VBIB vbib;
    if (blk_size < 4) return vbib;

    size_t pos = 0;

    const uint32_t vb_count = detail::rd<uint32_t>(blk_data + pos); pos += 4;
    vbib.vbs.reserve(vb_count);

    for (uint32_t vi = 0; vi < vb_count; ++vi) {
        if (pos + 12 > blk_size) break;

        VertexBuffer vb;
        vb.vertex_count = detail::rd<uint32_t>(blk_data + pos + 0);
        vb.vertex_size  = detail::rd<uint32_t>(blk_data + pos + 4);
        const uint32_t attr_count = detail::rd<uint32_t>(blk_data + pos + 8);
        pos += 12;

        vb.attributes.reserve(attr_count);
        for (uint32_t ai = 0; ai < attr_count; ++ai) {
            if (pos + 36 > blk_size) break;
            VertexAttribute attr;

            char name_buf[29]{};
            std::memcpy(name_buf, blk_data + pos, 28); pos += 28;
            attr.semantic_name  = name_buf;
            attr.semantic_index = detail::rd<uint32_t>(blk_data + pos + 0);
            attr.format         = static_cast<DxgiFormat>(detail::rd<uint32_t>(blk_data + pos + 4));
            attr.byte_offset    = detail::rd<uint32_t>(blk_data + pos + 8);
            attr.slot           = detail::rd<uint32_t>(blk_data + pos + 12);
            attr.slot_type      = detail::rd<uint32_t>(blk_data + pos + 16);
            attr.step_rate      = detail::rd<uint32_t>(blk_data + pos + 20);
            pos += 24;

            vb.attributes.push_back(std::move(attr));
        }

        if (pos + 4 > blk_size) break;
        const uint32_t data_len = detail::rd<uint32_t>(blk_data + pos); pos += 4;
        if (pos + data_len > blk_size) break;
        vb.data.assign(blk_data + pos, blk_data + pos + data_len);
        pos += data_len;

        pos = (pos + 3) & ~size_t(3);

        vbib.vbs.push_back(std::move(vb));
    }

    if (pos + 4 > blk_size) return vbib;
    const uint32_t ib_count = detail::rd<uint32_t>(blk_data + pos); pos += 4;
    vbib.ibs.reserve(ib_count);

    for (uint32_t ii = 0; ii < ib_count; ++ii) {
        if (pos + 8 > blk_size) break;

        IndexBuffer ib;
        ib.index_count = detail::rd<uint32_t>(blk_data + pos + 0);
        ib.index_size  = detail::rd<uint32_t>(blk_data + pos + 4);
        pos += 8;

        if (pos + 4 > blk_size) break;
        const uint32_t unk_count = detail::rd<uint32_t>(blk_data + pos); pos += 4;
        pos += unk_count * 4;

        if (pos + 4 > blk_size) break;
        const uint32_t data_len = detail::rd<uint32_t>(blk_data + pos); pos += 4;
        if (pos + data_len > blk_size) break;
        ib.data.assign(blk_data + pos, blk_data + pos + data_len);
        pos += data_len;

        pos = (pos + 3) & ~size_t(3);

        vbib.ibs.push_back(std::move(ib));
    }

    return vbib;
}

inline VBIB parse_mbuf(const uint8_t* blk_data, size_t blk_size) {
    VBIB vbib;
    if (blk_size < 16) return vbib;

    const uint32_t vb_desc_rel = detail::rd<uint32_t>(blk_data + 0);
    const uint32_t vb_count    = detail::rd<uint32_t>(blk_data + 4);
    const uint32_t ib_desc_rel = detail::rd<uint32_t>(blk_data + 8);
    const uint32_t ib_count    = detail::rd<uint32_t>(blk_data + 12);

    const size_t vb_desc_abs = static_cast<size_t>(0)  + vb_desc_rel;
    const size_t ib_desc_abs = static_cast<size_t>(8)  + ib_desc_rel;

    static constexpr size_t MBUF_ATTR_SIZE = 56; 
    vbib.vbs.reserve(vb_count);
    for (uint32_t vi = 0; vi < vb_count; ++vi) {
        const size_t dp = vb_desc_abs + static_cast<size_t>(vi) * 24;
        if (dp + 24 > blk_size) break;

        VertexBuffer vb;
        vb.vertex_count = detail::rd<uint32_t>(blk_data + dp + 0);
        vb.vertex_size  = detail::rd<uint32_t>(blk_data + dp + 4);
        const int32_t  attr_rel   = detail::rd<int32_t>(blk_data  + dp + 8);
        const uint32_t attr_count = detail::rd<uint32_t>(blk_data + dp + 12);
        const int32_t  data_rel   = detail::rd<int32_t>(blk_data  + dp + 16);
        const uint32_t data_size  = detail::rd<uint32_t>(blk_data + dp + 20);

        if (vb.vertex_count == 0 || vb.vertex_size == 0) continue;

        const size_t attr_abs = static_cast<size_t>(dp + 8) + attr_rel;
        vb.attributes.reserve(attr_count);
        for (uint32_t ai = 0; ai < attr_count; ++ai) {
            const size_t ap = attr_abs + static_cast<size_t>(ai) * MBUF_ATTR_SIZE;
            if (ap + MBUF_ATTR_SIZE > blk_size) break;
            VertexAttribute a{};
            char name_buf[29]{};
            std::memcpy(name_buf, blk_data + ap, 28);
            a.semantic_name  = name_buf;
            a.semantic_index = detail::rd<uint32_t>(blk_data + ap + 28);
            
            a.format      = static_cast<DxgiFormat>(detail::rd<uint32_t>(blk_data + ap + 36));
            a.byte_offset = detail::rd<uint32_t>(blk_data + ap + 40);
            a.slot        = detail::rd<uint32_t>(blk_data + ap + 44);
            a.slot_type   = detail::rd<uint32_t>(blk_data + ap + 48);
            a.step_rate   = detail::rd<uint32_t>(blk_data + ap + 52);
            vb.attributes.push_back(std::move(a));
        }

        const size_t data_abs = static_cast<size_t>(dp + 16) + data_rel;
        if (data_abs + data_size > blk_size) continue;

        const size_t raw_size = static_cast<size_t>(vb.vertex_count) * vb.vertex_size;
        vb.data.resize(raw_size);

        if (data_size == raw_size) {
            
            std::memcpy(vb.data.data(), blk_data + data_abs, raw_size);
        } else {
            
            if (meshopt_decodeVertexBuffer(vb.data.data(), vb.vertex_count,
                                           vb.vertex_size, blk_data + data_abs,
                                           data_size) != 0) {
                vb.data.clear();
                continue;
            }
        }

        vbib.vbs.push_back(std::move(vb));
    }

    vbib.ibs.reserve(ib_count);
    for (uint32_t ii = 0; ii < ib_count; ++ii) {
        const size_t dp = ib_desc_abs + static_cast<size_t>(ii) * 24;
        if (dp + 24 > blk_size) break;

        IndexBuffer ib;
        ib.index_count = detail::rd<uint32_t>(blk_data + dp + 0);
        ib.index_size  = detail::rd<uint32_t>(blk_data + dp + 4);
        
        const int32_t  data_rel  = detail::rd<int32_t>(blk_data  + dp + 16);
        const uint32_t data_size = detail::rd<uint32_t>(blk_data + dp + 20);

        if (ib.index_count == 0 || (ib.index_size != 2 && ib.index_size != 4))
            continue;

        const size_t data_abs = static_cast<size_t>(dp + 16) + data_rel;
        if (data_abs + data_size > blk_size) continue;

        const size_t raw_size = static_cast<size_t>(ib.index_count) * ib.index_size;
        ib.data.resize(raw_size);

        if (data_size == raw_size) {
            std::memcpy(ib.data.data(), blk_data + data_abs, raw_size);
        } else {
            
            if (meshopt_decodeIndexBuffer(ib.data.data(), ib.index_count,
                                          ib.index_size, blk_data + data_abs,
                                          data_size) != 0) {
                if (meshopt_decodeIndexSequence(ib.data.data(), ib.index_count,
                                                ib.index_size, blk_data + data_abs,
                                                data_size) != 0) {
                    ib.data.clear();
                    continue;
                }
            }
        }

        vbib.ibs.push_back(std::move(ib));
    }

    return vbib;
}

namespace kv3 {

static constexpr std::array<uint8_t, 16> GUID_BINARY = {
    0x00, 0x05, 0x86, 0x1B, 0xD8, 0xF7, 0xC1, 0x40,
    0xAD, 0x82, 0x75, 0xA4, 0x82, 0x67, 0xE7, 0x14
};
static constexpr std::array<uint8_t, 16> GUID_BINARY_LZ4 = {
    0x8A, 0x34, 0x47, 0x68, 0xA1, 0x63, 0x5C, 0x4F,
    0xA1, 0x97, 0x53, 0x80, 0x6F, 0xD9, 0xB1, 0x19
};
static constexpr std::array<uint8_t, 16> GUID_BINARY_BC = {
    0x46, 0x1A, 0x79, 0x95, 0xBC, 0x95, 0x6C, 0x4F,
    0xA7, 0x0B, 0x05, 0xBC, 0xA1, 0xB7, 0xDF, 0xD2
};

static constexpr uint32_t KV3_MAGIC_VKV3 = 0x03564B56u; 
static constexpr uint32_t KV3_MAGIC_KV35 = 0x4B563305u; 

enum class KVType : uint8_t {
    Null        = 1,
    Boolean     = 2,
    Int64       = 3,
    UInt64      = 4,
    Double      = 5,
    String      = 6,
    BinaryBlob  = 7,
    Array       = 8,
    Object      = 9,
    ArrayTyped  = 10,
    Int32       = 11,
    UInt32      = 12,
    Boolean_true  = 13,
    Boolean_false = 14,
    Int64_zero    = 15,
    Int64_one     = 16,
    Double_zero   = 17,
    Double_one    = 18,
    Float         = 19,
    Int16         = 20,
    UInt16        = 21,
    Unknown22     = 22,
    Int32_as_byte = 23,
    Array_type_byte_length = 24,
    Array_type_auxiliary   = 25,
};

struct KVValue;
using KVObject = std::vector<std::pair<std::string, KVValue>>;
using KVArray  = std::vector<KVValue>;

struct KVValue {
    KVType type = KVType::Null;
    std::variant<
        std::monostate,          
        bool,                    
        int64_t,                 
        uint64_t,                
        double,                  
        std::string,             
        std::vector<uint8_t>,    
        KVObject,                
        KVArray                  
    > data;

    bool         is_null()   const { return type == KVType::Null; }
    bool         is_string() const { return type == KVType::String; }
    bool         is_object() const { return type == KVType::Object; }
    bool         is_array()  const { return type == KVType::Array || type == KVType::ArrayTyped; }

    const std::string&  as_string() const { return std::get<std::string>(data); }
    int64_t             as_int()    const {
        if (auto* v = std::get_if<int64_t>(&data))  return *v;
        if (auto* v = std::get_if<uint64_t>(&data)) return static_cast<int64_t>(*v);
        return 0;
    }
    double              as_float()  const {
        if (auto* v = std::get_if<double>(&data))   return *v;
        if (auto* v = std::get_if<int64_t>(&data))  return static_cast<double>(*v);
        return 0.0;
    }
    const KVObject&     as_object() const { return std::get<KVObject>(data); }
    const KVArray&      as_array()  const { return std::get<KVArray>(data); }

    const KVValue* get(const char* key) const {
        if (!is_object()) return nullptr;
        for (const auto& [k, v] : as_object())
            if (k == key) return &v;
        return nullptr;
    }
    const KVValue* get(size_t idx) const {
        if (is_array()) {
            const auto& a = as_array();
            return idx < a.size() ? &a[idx] : nullptr;
        }
        if (is_object()) {
            const auto& o = as_object();
            return idx < o.size() ? &o[idx].second : nullptr;
        }
        return nullptr;
    }
    
    const KVValue* get(int idx) const {
        return get(static_cast<size_t>(idx < 0 ? 0 : idx));
    }

    size_t size() const {
        if (is_array())  return as_array().size();
        if (is_object()) return as_object().size();
        return 0;
    }
};

struct BufferCursor {
    const uint8_t* bytes1 = nullptr; size_t size1 = 0; size_t off1 = 0;
    const uint8_t* bytes2 = nullptr; size_t size2 = 0; size_t off2 = 0;
    const uint8_t* bytes4 = nullptr; size_t size4 = 0; size_t off4 = 0;
    const uint8_t* bytes8 = nullptr; size_t size8 = 0; size_t off8 = 0;
};

struct KVReader {
    int version = 0;
    BufferCursor main_buf{};
    BufferCursor aux_buf{};
    const uint8_t* types = nullptr;
    size_t types_size = 0;
    size_t types_off = 0;
    const uint8_t* object_lengths = nullptr;
    size_t object_lengths_size = 0;
    size_t object_lengths_off = 0;
    const uint8_t* blob_lengths = nullptr;
    size_t blob_lengths_size = 0;
    size_t blob_lengths_off = 0;
    const uint8_t* blobs = nullptr;
    size_t blobs_size = 0;
    size_t blobs_off = 0;
    std::vector<std::string> strings;

    static size_t align_to(size_t v, size_t a) {
        return (v + (a - 1)) & ~(a - 1);
    }

    bool read_u8_from(uint8_t& out, const uint8_t* base, size_t size, size_t& off) {
        if (off + 1 > size) return false;
        out = base[off++];
        return true;
    }
    bool read_u16_from(uint16_t& out, const uint8_t* base, size_t size, size_t& off) {
        if (off + 2 > size) return false;
        out = detail::rd<uint16_t>(base + off);
        off += 2;
        return true;
    }
    bool read_i32_from(int32_t& out, const uint8_t* base, size_t size, size_t& off) {
        if (off + 4 > size) return false;
        out = detail::rd<int32_t>(base + off);
        off += 4;
        return true;
    }
    bool read_u32_from(uint32_t& out, const uint8_t* base, size_t size, size_t& off) {
        if (off + 4 > size) return false;
        out = detail::rd<uint32_t>(base + off);
        off += 4;
        return true;
    }
    bool read_i64_from(int64_t& out, const uint8_t* base, size_t size, size_t& off) {
        if (off + 8 > size) return false;
        out = detail::rd<int64_t>(base + off);
        off += 8;
        return true;
    }
    bool read_u64_from(uint64_t& out, const uint8_t* base, size_t size, size_t& off) {
        if (off + 8 > size) return false;
        out = detail::rd<uint64_t>(base + off);
        off += 8;
        return true;
    }
    bool read_f32_from(float& out, const uint8_t* base, size_t size, size_t& off) {
        if (off + 4 > size) return false;
        out = detail::rd<float>(base + off);
        off += 4;
        return true;
    }
    bool read_f64_from(double& out, const uint8_t* base, size_t size, size_t& off) {
        if (off + 8 > size) return false;
        out = detail::rd<double>(base + off);
        off += 8;
        return true;
    }

    bool read_main_u8(uint8_t& out)   { return read_u8_from(out, main_buf.bytes1, main_buf.size1, main_buf.off1); }
    bool read_main_u16(uint16_t& out) { return read_u16_from(out, main_buf.bytes2, main_buf.size2, main_buf.off2); }
    bool read_main_i32(int32_t& out)  { return read_i32_from(out, main_buf.bytes4, main_buf.size4, main_buf.off4); }
    bool read_main_u32(uint32_t& out) { return read_u32_from(out, main_buf.bytes4, main_buf.size4, main_buf.off4); }
    bool read_main_i64(int64_t& out)  { return read_i64_from(out, main_buf.bytes8, main_buf.size8, main_buf.off8); }
    bool read_main_u64(uint64_t& out) { return read_u64_from(out, main_buf.bytes8, main_buf.size8, main_buf.off8); }
    bool read_main_f32(float& out)    { return read_f32_from(out, main_buf.bytes4, main_buf.size4, main_buf.off4); }
    bool read_main_f64(double& out)   { return read_f64_from(out, main_buf.bytes8, main_buf.size8, main_buf.off8); }

    bool read_type(KVType& out) {
        if (types_off >= types_size) return false;
        uint8_t databyte = types[types_off++];

        if (version >= 3) {
            if ((databyte & 0x80u) != 0) {
                databyte &= 0x3Fu;
                if (types_off >= types_size) return false;
                ++types_off; 
            }
        } else {
            if ((databyte & 0x80u) != 0) {
                databyte &= 0x7Fu;
                if (types_off >= types_size) return false;
                ++types_off; 
            }
        }

        out = static_cast<KVType>(databyte);
        return true;
    }

    bool read_value_with_type(KVType type, KVValue& out, int depth = 0) {
        if (depth > 128) return false;
        out.type = type;

        switch (type) {
        case KVType::Null:
            out.data = std::monostate{};
            return true;
        case KVType::Boolean_true:
            out.data = true;
            return true;
        case KVType::Boolean_false:
            out.data = false;
            return true;
        case KVType::Int64_zero:
            out.data = static_cast<int64_t>(0);
            return true;
        case KVType::Int64_one:
            out.data = static_cast<int64_t>(1);
            return true;
        case KVType::Double_zero:
            out.data = 0.0;
            return true;
        case KVType::Double_one:
            out.data = 1.0;
            return true;
        case KVType::Boolean: {
            uint8_t b = 0;
            if (!read_main_u8(b)) return false;
            out.data = (b != 0);
            return true;
        }
        case KVType::Int32_as_byte:
        case KVType::Unknown22: {
            uint8_t v = 0;
            if (!read_main_u8(v)) return false;
            out.data = static_cast<int64_t>(v);
            return true;
        }
        case KVType::Int16: {
            uint16_t u = 0;
            if (!read_main_u16(u)) return false;
            out.data = static_cast<int64_t>(static_cast<int16_t>(u));
            return true;
        }
        case KVType::UInt16: {
            uint16_t u = 0;
            if (!read_main_u16(u)) return false;
            out.data = static_cast<uint64_t>(u);
            return true;
        }
        case KVType::Int32: {
            int32_t v = 0;
            if (!read_main_i32(v)) return false;
            out.data = static_cast<int64_t>(v);
            return true;
        }
        case KVType::UInt32: {
            uint32_t v = 0;
            if (!read_main_u32(v)) return false;
            out.data = static_cast<uint64_t>(v);
            return true;
        }
        case KVType::Float: {
            float f = 0.0f;
            if (!read_main_f32(f)) return false;
            out.data = static_cast<double>(f);
            return true;
        }
        case KVType::Int64: {
            int64_t v = 0;
            if (!read_main_i64(v)) return false;
            out.data = v;
            return true;
        }
        case KVType::UInt64: {
            uint64_t v = 0;
            if (!read_main_u64(v)) return false;
            out.data = v;
            return true;
        }
        case KVType::Double: {
            double d = 0.0;
            if (!read_main_f64(d)) return false;
            out.data = d;
            return true;
        }
        case KVType::String: {
            int32_t sid = -1;
            if (!read_main_i32(sid)) return false;
            if (sid >= 0 && static_cast<size_t>(sid) < strings.size()) out.data = strings[static_cast<size_t>(sid)];
            else out.data = std::string{};
            return true;
        }
        case KVType::BinaryBlob: {
            int32_t len = 0;
            if (version < 2) {
                if (!read_main_i32(len) || len < 0) return false;
                if (main_buf.off1 + static_cast<size_t>(len) > main_buf.size1) return false;
                std::vector<uint8_t> blob(main_buf.bytes1 + main_buf.off1, main_buf.bytes1 + main_buf.off1 + static_cast<size_t>(len));
                main_buf.off1 += static_cast<size_t>(len);
                out.data = std::move(blob);
                return true;
            }
            if (blob_lengths_off + 4 > blob_lengths_size) return false;
            len = detail::rd<int32_t>(blob_lengths + blob_lengths_off);
            blob_lengths_off += 4;
            if (len < 0 || blobs_off + static_cast<size_t>(len) > blobs_size) return false;
            std::vector<uint8_t> blob(blobs + blobs_off, blobs + blobs_off + static_cast<size_t>(len));
            blobs_off += static_cast<size_t>(len);
            out.data = std::move(blob);
            return true;
        }
        case KVType::Array: {
            int32_t count = 0;
            if (!read_main_i32(count) || count < 0) return false;
            KVArray arr;
            arr.reserve(static_cast<size_t>(count));
            for (int32_t i = 0; i < count; ++i) {
                KVType t;
                if (!read_type(t)) return false;
                KVValue elem;
                if (!read_value_with_type(t, elem, depth + 1)) return false;
                arr.push_back(std::move(elem));
            }
            out.data = std::move(arr);
            return true;
        }
        case KVType::ArrayTyped:
        case KVType::Array_type_byte_length:
        case KVType::Array_type_auxiliary: {
            int32_t count = 0;
            if (type == KVType::Array_type_byte_length || type == KVType::Array_type_auxiliary) {
                uint8_t c = 0;
                if (!read_main_u8(c)) return false;
                count = c;
            } else {
                if (!read_main_i32(count) || count < 0) return false;
            }

            KVType sub_type;
            if (!read_type(sub_type)) return false;

            KVArray arr;
            arr.reserve(static_cast<size_t>(count));

            if (type == KVType::Array_type_auxiliary) {
                std::swap(main_buf, aux_buf);
            }
            for (int32_t i = 0; i < count; ++i) {
                KVValue elem;
                if (!read_value_with_type(sub_type, elem, depth + 1)) {
                    if (type == KVType::Array_type_auxiliary) std::swap(main_buf, aux_buf);
                    return false;
                }
                arr.push_back(std::move(elem));
            }
            if (type == KVType::Array_type_auxiliary) {
                std::swap(main_buf, aux_buf);
            }
            out.type = KVType::ArrayTyped;
            out.data = std::move(arr);
            return true;
        }
        case KVType::Object: {
            int32_t object_length = 0;
            if (version >= 5) {
                if (object_lengths_off + 4 > object_lengths_size) return false;
                object_length = detail::rd<int32_t>(object_lengths + object_lengths_off);
                object_lengths_off += 4;
            } else {
                if (!read_main_i32(object_length)) return false;
            }
            if (object_length < 0) return false;

            KVObject obj;
            obj.reserve(static_cast<size_t>(object_length));
            for (int32_t i = 0; i < object_length; ++i) {
                int32_t sid = -1;
                if (!read_main_i32(sid)) return false;
                std::string key;
                if (sid >= 0 && static_cast<size_t>(sid) < strings.size()) key = strings[static_cast<size_t>(sid)];

                KVType t;
                if (!read_type(t)) return false;
                KVValue v;
                if (!read_value_with_type(t, v, depth + 1)) return false;
                obj.emplace_back(std::move(key), std::move(v));
            }
            out.data = std::move(obj);
            return true;
        }
        default:
            return false;
        }
    }
};

inline std::optional<KVValue> parse_binary(const uint8_t* data, size_t size) {
    if (!data || size < 24) return std::nullopt;

    const uint32_t magic = detail::rd<uint32_t>(data + 0);
    if (magic == KV3_MAGIC_VKV3) {
        return std::nullopt; 
    }

    if ((magic & 0xFFFFFF00u) != 0x4B563300u) return std::nullopt;
    const int version = static_cast<int>(magic & 0xFFu);
    if (version < 1 || version > 5) return std::nullopt;

    size_t pos = 4;
    if (pos + 16 + 4 > size) return std::nullopt;
    pos += 16; 

    const uint32_t compression_method = detail::rd<uint32_t>(data + pos); pos += 4;

    uint16_t compression_dict_id = 0;
    uint16_t compression_frame_size = 0;
    int32_t count_bytes1 = 0;
    int32_t count_bytes4 = 0;
    int32_t count_bytes8 = 0;
    int32_t count_types = 0;
    int32_t count_objects = 0;
    int32_t count_arrays = 0;
    int32_t size_uncompressed_total = 0;
    int32_t size_compressed_total = 0;
    int32_t count_blocks = 0;
    int32_t size_binary_blobs = 0;
    int32_t count_bytes2 = 0;
    int32_t size_block_compressed_sizes = 0;

    auto need = [&](size_t n) { return pos + n <= size; };

    if (version == 1) {
        if (!need(16)) return std::nullopt;
        count_bytes1 = detail::rd<int32_t>(data + pos); pos += 4;
        count_bytes4 = detail::rd<int32_t>(data + pos); pos += 4;
        count_bytes8 = detail::rd<int32_t>(data + pos); pos += 4;
        size_uncompressed_total = detail::rd<int32_t>(data + pos); pos += 4;
        size_compressed_total = static_cast<int32_t>(size - pos);
    } else {
        if (!need(40)) return std::nullopt;
        compression_dict_id = detail::rd<uint16_t>(data + pos); pos += 2;
        compression_frame_size = detail::rd<uint16_t>(data + pos); pos += 2;
        count_bytes1 = detail::rd<int32_t>(data + pos); pos += 4;
        count_bytes4 = detail::rd<int32_t>(data + pos); pos += 4;
        count_bytes8 = detail::rd<int32_t>(data + pos); pos += 4;
        count_types = detail::rd<int32_t>(data + pos); pos += 4;
        count_objects = static_cast<int32_t>(detail::rd<uint16_t>(data + pos)); pos += 2;
        count_arrays = static_cast<int32_t>(detail::rd<uint16_t>(data + pos)); pos += 2;
        size_uncompressed_total = detail::rd<int32_t>(data + pos); pos += 4;
        size_compressed_total = detail::rd<int32_t>(data + pos); pos += 4;
        count_blocks = detail::rd<int32_t>(data + pos); pos += 4;
        size_binary_blobs = detail::rd<int32_t>(data + pos); pos += 4;
    }

    if (version >= 4) {
        if (!need(8)) return std::nullopt;
        count_bytes2 = detail::rd<int32_t>(data + pos); pos += 4;
        size_block_compressed_sizes = detail::rd<int32_t>(data + pos); pos += 4;
    }

    int32_t size_uncompressed_buffer1 = 0;
    int32_t size_compressed_buffer1 = 0;
    int32_t size_uncompressed_buffer2 = 0;
    int32_t size_compressed_buffer2 = 0;
    int32_t count_bytes1_buffer2 = 0;
    int32_t count_bytes2_buffer2 = 0;
    int32_t count_bytes4_buffer2 = 0;
    int32_t count_bytes8_buffer2 = 0;
    int32_t count_objects_buffer2 = 0;

    if (version >= 5) {
        if (!need(48)) return std::nullopt;
        size_uncompressed_buffer1 = detail::rd<int32_t>(data + pos); pos += 4;
        size_compressed_buffer1 = detail::rd<int32_t>(data + pos); pos += 4;
        size_uncompressed_buffer2 = detail::rd<int32_t>(data + pos); pos += 4;
        size_compressed_buffer2 = detail::rd<int32_t>(data + pos); pos += 4;
        count_bytes1_buffer2 = detail::rd<int32_t>(data + pos); pos += 4;
        count_bytes2_buffer2 = detail::rd<int32_t>(data + pos); pos += 4;
        count_bytes4_buffer2 = detail::rd<int32_t>(data + pos); pos += 4;
        count_bytes8_buffer2 = detail::rd<int32_t>(data + pos); pos += 4;
        pos += 4; 
        count_objects_buffer2 = detail::rd<int32_t>(data + pos); pos += 4;
        pos += 8; 
        if (size_uncompressed_total != size_uncompressed_buffer1 + size_uncompressed_buffer2) return std::nullopt;
    } else {
        size_uncompressed_buffer1 = size_uncompressed_total;
        size_compressed_buffer1 = size_compressed_total;
    }

    std::vector<uint32_t> block_compressed_sizes;
    size_t block_size_index = 0;
    if (count_blocks > 0 && size_block_compressed_sizes > 0) {
        if (!need(static_cast<size_t>(size_block_compressed_sizes))) {
            return std::nullopt;
        }

        const size_t table_bytes = static_cast<size_t>(size_block_compressed_sizes);
        const uint8_t* tbl = data + pos;

        const size_t expected_count = static_cast<size_t>(count_blocks);
        if (table_bytes == expected_count * 4) {
            block_compressed_sizes.reserve(expected_count);
            for (size_t i = 0; i < expected_count; ++i) {
                block_compressed_sizes.push_back(detail::rd<uint32_t>(tbl + i * 4));
            }
        } else if (table_bytes == expected_count * 2) {
            block_compressed_sizes.reserve(expected_count);
            for (size_t i = 0; i < expected_count; ++i) {
                block_compressed_sizes.push_back(static_cast<uint32_t>(detail::rd<uint16_t>(tbl + i * 2)));
            }
        } else {
            return std::nullopt;
        }

        pos += table_bytes;
    }

    auto decode_block = [&](int32_t uncompressed_size, int32_t compressed_size, std::vector<uint8_t>& out) -> bool {
        if (uncompressed_size < 0 || compressed_size < 0) return false;
        out.resize(static_cast<size_t>(uncompressed_size));

        auto decode_zstd_frames = [&](const uint8_t* src, size_t src_size, uint8_t* dst, size_t dst_size) -> bool {
            size_t src_off = 0;
            size_t dst_off = 0;

            while (src_off < src_size && dst_off < dst_size) {
                const size_t frame_size = ZSTD_findFrameCompressedSize(src + src_off, src_size - src_off);
                if (ZSTD_isError(frame_size) || frame_size == 0 || src_off + frame_size > src_size) {
                    return false;
                }

                const size_t wrote = ZSTD_decompress(
                    dst + dst_off,
                    dst_size - dst_off,
                    src + src_off,
                    frame_size);

                if (ZSTD_isError(wrote)) {
                    return false;
                }

                src_off += frame_size;
                dst_off += wrote;
            }

            return (src_off == src_size) && (dst_off == dst_size);
        };

        auto decode_zstd_frames_prefix = [&](const uint8_t* src, size_t src_size, uint8_t* dst, size_t dst_size, size_t& consumed) -> bool {
            size_t src_off = 0;
            size_t dst_off = 0;

            while (src_off < src_size && dst_off < dst_size) {
                const size_t frame_size = ZSTD_findFrameCompressedSize(src + src_off, src_size - src_off);
                if (ZSTD_isError(frame_size) || frame_size == 0 || src_off + frame_size > src_size) {
                    return false;
                }

                const size_t wrote = ZSTD_decompress(
                    dst + dst_off,
                    dst_size - dst_off,
                    src + src_off,
                    frame_size);

                if (ZSTD_isError(wrote)) {
                    return false;
                }

                src_off += frame_size;
                dst_off += wrote;
            }

            consumed = src_off;
            return dst_off == dst_size;
        };

        if (compression_method == 0) {
            if (pos + static_cast<size_t>(uncompressed_size) > size) return false;
            std::memcpy(out.data(), data + pos, static_cast<size_t>(uncompressed_size));
            pos += static_cast<size_t>(uncompressed_size);
            return true;
        }
        if (compression_method == 1) {
            if (!block_compressed_sizes.empty()) {
                if (compression_dict_id != 0) return false;
                if (version >= 2 && compression_frame_size != 16384) return false;

                size_t out_off = 0;
                while (out_off < out.size()) {
                    if (block_size_index >= block_compressed_sizes.size()) return false;
                    const size_t cs = static_cast<size_t>(block_compressed_sizes[block_size_index++]);
                    if (cs == 0 || pos + cs > size) return false;
                    const size_t chunk_out = std::min(static_cast<size_t>(compression_frame_size), out.size() - out_off);
                    const bool ok = detail::lz4_decompress(
                        data + pos,
                        cs,
                        out.data() + out_off,
                        chunk_out);
                    pos += cs;
                    if (!ok) return false;
                    out_off += chunk_out;
                }
                return true;
            }

            if (pos + static_cast<size_t>(compressed_size) > size) return false;
            if (compression_dict_id != 0) return false;
            if (version >= 2 && compression_frame_size != 16384) return false;
            const bool ok = detail::lz4_decompress(
                data + pos,
                static_cast<size_t>(compressed_size),
                out.data(),
                static_cast<size_t>(uncompressed_size));
            pos += static_cast<size_t>(compressed_size);
            return ok;
        }
        if (compression_method == 2) {
            
            if (compressed_size > 0 && pos + static_cast<size_t>(compressed_size) <= size) {
                if (decode_zstd_frames(
                    data + pos,
                    static_cast<size_t>(compressed_size),
                    out.data(),
                    out.size())) {
                    pos += static_cast<size_t>(compressed_size);
                    return true;
                }
            }

            {
                size_t consumed = 0;
                if (decode_zstd_frames_prefix(
                    data + pos,
                    size - pos,
                    out.data(),
                    out.size(),
                    consumed)) {
                    pos += consumed;
                    return true;
                }
            }

            {
                constexpr uint8_t zstd_magic[4] = {0x28, 0xB5, 0x2F, 0xFD};
                const size_t max_shift = std::min<size_t>(4096, (size > pos) ? (size - pos) : 0);

                for (size_t shift = 1; shift + 4 <= max_shift; ++shift) {
                    const uint8_t* p = data + pos + shift;
                    if (p[0] != zstd_magic[0] || p[1] != zstd_magic[1] ||
                        p[2] != zstd_magic[2] || p[3] != zstd_magic[3]) {
                        continue;
                    }

                    size_t consumed = 0;
                    if (decode_zstd_frames_prefix(
                        p,
                        size - (pos + shift),
                        out.data(),
                        out.size(),
                        consumed)) {
                        pos += shift + consumed;
                        return true;
                    }
                }
            }

            if (!block_compressed_sizes.empty()) {
                size_t out_off = 0;
                const size_t frame = (compression_frame_size > 0)
                    ? static_cast<size_t>(compression_frame_size)
                    : out.size();

                while (out_off < out.size()) {
                    if (block_size_index >= block_compressed_sizes.size()) return false;
                    const size_t cs = static_cast<size_t>(block_compressed_sizes[block_size_index++]);
                    if (cs == 0 || pos + cs > size) return false;

                    const size_t chunk_out = std::min(frame, out.size() - out_off);
                    const size_t got = ZSTD_decompress(
                        out.data() + out_off,
                        chunk_out,
                        data + pos,
                        cs);
                    pos += cs;
                    if (ZSTD_isError(got)) return false;
                    if (got != chunk_out) return false;
                    out_off += chunk_out;
                }
                return true;
            }

            if (pos + static_cast<size_t>(compressed_size) > size) return false;
            const size_t got = ZSTD_decompress(
                out.data(),
                static_cast<size_t>(uncompressed_size),
                data + pos,
                static_cast<size_t>(compressed_size));
            pos += static_cast<size_t>(compressed_size);
            if (ZSTD_isError(got)) return false;
            return got == static_cast<size_t>(uncompressed_size);
        }
        return false;
    };

    const int32_t buf1_alloc = (version < 5 && compression_method == 2 && size_binary_blobs > 0)
        ? size_uncompressed_buffer1 + size_binary_blobs
        : size_uncompressed_buffer1;
    std::vector<uint8_t> buf1;
    std::vector<uint8_t> buf2;
    std::vector<uint8_t> blobs_storage;
    if (!decode_block(buf1_alloc, size_compressed_buffer1, buf1)) {
        return std::nullopt;
    }

    KVReader r{};
    r.version = version;

    auto split_streams = [&](const std::vector<uint8_t>& src,
                             int32_t cb1,
                             int32_t cb2,
                             int32_t cb4,
                             int32_t cb8,
                             bool align8_when_empty,
                             BufferCursor& out,
                             size_t& offset_out) -> bool {
        if (cb1 < 0 || cb2 < 0 || cb4 < 0 || cb8 < 0) return false;
        size_t off = 0;
        if (cb1 > 0) {
            if (off + static_cast<size_t>(cb1) > src.size()) return false;
            out.bytes1 = src.data() + off;
            out.size1 = static_cast<size_t>(cb1);
            off += static_cast<size_t>(cb1);
        }
        if (cb2 > 0) {
            off = KVReader::align_to(off, 2);
            if (off + static_cast<size_t>(cb2) * 2 > src.size()) return false;
            out.bytes2 = src.data() + off;
            out.size2 = static_cast<size_t>(cb2) * 2;
            off += static_cast<size_t>(cb2) * 2;
        }
        if (cb4 > 0) {
            off = KVReader::align_to(off, 4);
            if (off + static_cast<size_t>(cb4) * 4 > src.size()) return false;
            out.bytes4 = src.data() + off;
            out.size4 = static_cast<size_t>(cb4) * 4;
            off += static_cast<size_t>(cb4) * 4;
        }
        if (cb8 > 0) {
            off = KVReader::align_to(off, 8);
            if (off + static_cast<size_t>(cb8) * 8 > src.size()) return false;
            out.bytes8 = src.data() + off;
            out.size8 = static_cast<size_t>(cb8) * 8;
            off += static_cast<size_t>(cb8) * 8;
        } else if (align8_when_empty) {
            off = KVReader::align_to(off, 8);
        }
        offset_out = off;
        return true;
    };

    BufferCursor b1{};
    size_t b1_offset = 0;
    if (!split_streams(buf1, count_bytes1, count_bytes2, count_bytes4, count_bytes8, version < 5, b1, b1_offset)) {
        return std::nullopt;
    }

    if (b1.size4 < 4) return std::nullopt;
    int32_t count_strings = detail::rd<int32_t>(b1.bytes4 + b1.off4);
    b1.off4 += 4;
    if (count_strings < 0) return std::nullopt;

    r.strings.reserve(static_cast<size_t>(count_strings));

    if (version >= 5) {
        r.aux_buf = b1;

        for (int32_t i = 0; i < count_strings; ++i) {
            std::string s;
            if (!detail::read_cstr(r.aux_buf.bytes1, r.aux_buf.size1, r.aux_buf.off1, s)) return std::nullopt;
            r.strings.push_back(std::move(s));
        }

        if (!decode_block(size_uncompressed_buffer2, size_compressed_buffer2, buf2)) {
            return std::nullopt;
        }

        size_t off = 0;
        if (count_objects < 0 || count_objects_buffer2 < 0) return std::nullopt;

        int32_t object_lengths_count = count_objects_buffer2;
        if (object_lengths_count <= 0) {
            object_lengths_count = count_objects;
        }

        size_t obj_len_bytes = static_cast<size_t>(object_lengths_count) * 4;
        if (off + obj_len_bytes > buf2.size()) return std::nullopt;
        r.object_lengths = buf2.data();
        r.object_lengths_size = obj_len_bytes;
        off += obj_len_bytes;

        BufferCursor b2{};
        b2.off1 = b2.off2 = b2.off4 = b2.off8 = 0;
        if (count_bytes1_buffer2 < 0 || count_bytes2_buffer2 < 0 || count_bytes4_buffer2 < 0 || count_bytes8_buffer2 < 0) return std::nullopt;

        if (count_bytes1_buffer2 > 0) {
            if (off + static_cast<size_t>(count_bytes1_buffer2) > buf2.size()) return std::nullopt;
            b2.bytes1 = buf2.data() + off;
            b2.size1 = static_cast<size_t>(count_bytes1_buffer2);
            off += static_cast<size_t>(count_bytes1_buffer2);
        }
        if (count_bytes2_buffer2 > 0) {
            off = KVReader::align_to(off, 2);
            if (off + static_cast<size_t>(count_bytes2_buffer2) * 2 > buf2.size()) return std::nullopt;
            b2.bytes2 = buf2.data() + off;
            b2.size2 = static_cast<size_t>(count_bytes2_buffer2) * 2;
            off += static_cast<size_t>(count_bytes2_buffer2) * 2;
        }
        if (count_bytes4_buffer2 > 0) {
            off = KVReader::align_to(off, 4);
            if (off + static_cast<size_t>(count_bytes4_buffer2) * 4 > buf2.size()) return std::nullopt;
            b2.bytes4 = buf2.data() + off;
            b2.size4 = static_cast<size_t>(count_bytes4_buffer2) * 4;
            off += static_cast<size_t>(count_bytes4_buffer2) * 4;
        }
        if (count_bytes8_buffer2 > 0) {
            off = KVReader::align_to(off, 8);
            if (off + static_cast<size_t>(count_bytes8_buffer2) * 8 > buf2.size()) return std::nullopt;
            b2.bytes8 = buf2.data() + off;
            b2.size8 = static_cast<size_t>(count_bytes8_buffer2) * 8;
            off += static_cast<size_t>(count_bytes8_buffer2) * 8;
        }

        if (count_types < 0) return std::nullopt;
        const size_t types_size_v5 = static_cast<size_t>(count_types);
        if (off + types_size_v5 > buf2.size()) return std::nullopt;

        r.types = buf2.data() + off;
        r.types_size = types_size_v5;
        off += types_size_v5;

        if (count_blocks > 0) {
            
            const size_t blob_len_bytes = static_cast<size_t>(count_blocks) * 4;
            if (off + blob_len_bytes > buf2.size()) return std::nullopt;
            r.blob_lengths = buf2.data() + off;
            r.blob_lengths_size = blob_len_bytes;
            off += blob_len_bytes;
        }

        if (off + 4 > buf2.size()) return std::nullopt;
        {
            const uint32_t trailer = detail::rd<uint32_t>(buf2.data() + off);
            if (trailer != 0xFFEEDD00u) return std::nullopt;
            off += 4;
        }

        r.main_buf = b2;

        if (count_blocks > 0 && size_binary_blobs > 0) {
            const int32_t blobs_compressed = size_compressed_total
                - size_compressed_buffer1 - size_compressed_buffer2;
            if (blobs_compressed > 0 && pos + static_cast<size_t>(blobs_compressed) <= size) {
                if (compression_method == 2) {
                    blobs_storage.resize(static_cast<size_t>(size_binary_blobs));
                    const size_t got = ZSTD_decompress(
                        blobs_storage.data(), blobs_storage.size(),
                        data + pos, static_cast<size_t>(blobs_compressed));
                    pos += static_cast<size_t>(blobs_compressed);
                    if (ZSTD_isError(got) || got != blobs_storage.size()) return std::nullopt;
                    r.blobs = blobs_storage.data();
                    r.blobs_size = blobs_storage.size();
                } else if (compression_method == 0 && static_cast<size_t>(size_binary_blobs) <= static_cast<size_t>(blobs_compressed)) {
                    r.blobs = data + pos;
                    r.blobs_size = static_cast<size_t>(size_binary_blobs);
                    pos += static_cast<size_t>(size_binary_blobs);
                }
            }
            
            if (pos + 4 <= size) {
                const uint32_t ft = detail::rd<uint32_t>(data + pos);
                if (ft == 0xFFEEDD00u) pos += 4;
            }
        }
    } else {
        size_t strings_off = b1_offset;
        for (int32_t i = 0; i < count_strings; ++i) {
            std::string s;
            if (!detail::read_cstr(buf1.data(), buf1.size(), strings_off, s)) return std::nullopt;
            r.strings.push_back(std::move(s));
        }

        if (version == 1) {
            if (size_uncompressed_total < 0) return std::nullopt;
            if (static_cast<size_t>(size_uncompressed_total) < strings_off + 4) return std::nullopt;
            r.types_size = static_cast<size_t>(size_uncompressed_total) - strings_off - 4;
        } else {
            if (count_types < 0) return std::nullopt;
            if (count_types < static_cast<int32_t>(strings_off - b1_offset)) return std::nullopt;
            r.types_size = static_cast<size_t>(count_types) - (strings_off - b1_offset);
        }

        if (strings_off + r.types_size > buf1.size()) return std::nullopt;
        r.types = buf1.data() + strings_off;

        const size_t off_after_types = strings_off + r.types_size;

        if (count_blocks > 0) {
            
            const size_t blob_len_bytes = static_cast<size_t>(count_blocks) * 4;
            if (off_after_types + blob_len_bytes + 4 > buf1.size()) return std::nullopt;
            r.blob_lengths = buf1.data() + off_after_types;
            r.blob_lengths_size = blob_len_bytes;

            const uint32_t trailer = detail::rd<uint32_t>(buf1.data() + off_after_types + blob_len_bytes);
            if (trailer != 0xFFEEDD00u) return std::nullopt;

            if (size_binary_blobs > 0 && compression_method == 2) {
                const size_t blob_start = static_cast<size_t>(size_uncompressed_buffer1);
                if (blob_start + static_cast<size_t>(size_binary_blobs) <= buf1.size()) {
                    r.blobs = buf1.data() + blob_start;
                    r.blobs_size = static_cast<size_t>(size_binary_blobs);
                }
            }

            if (pos + 4 <= size) {
                const uint32_t ft = detail::rd<uint32_t>(data + pos);
                if (ft == 0xFFEEDD00u) pos += 4;
            }
        } else {
            if (off_after_types + 4 > buf1.size()) return std::nullopt;
            const uint32_t trailer = detail::rd<uint32_t>(buf1.data() + off_after_types);
            if (trailer != 0xFFEEDD00u) return std::nullopt;
        }

        r.main_buf = b1;
    }

    if (compression_method == 1 && compression_frame_size != 16384 && version >= 2) {
        return std::nullopt;
    }
    KVType root_type;
    if (!r.read_type(root_type)) return std::nullopt;
    KVValue root;
    if (!r.read_value_with_type(root_type, root)) return std::nullopt;
    return root;
}

} 

struct Bone {
    std::string name;
    int         parent = -1;     
    Vec3        pos_parent{};    
    Vec4        rot_parent{};    
};

struct ModelData {
    std::vector<VertexBuffer>  vertex_buffers;
    std::vector<IndexBuffer>   index_buffers;

    std::vector<Bone>          skeleton;        

    std::vector<Mat3x4>        inv_bind_poses;

    std::vector<int>           remapping_table;

    std::vector<int>           remapping_table_starts;

    std::vector<std::string>   mesh_resources;

    struct MeshGroupInfo {
        std::string              group_name;
        std::vector<std::string> meshes; 
    };
    std::vector<MeshGroupInfo> mesh_group_info;

    std::vector<std::vector<int>> per_vb_remap;

    std::vector<size_t> embedded_mesh_vb_counts;

    int geometry_source = 0;

    bool has_skeleton() const { return !skeleton.empty(); }
    bool has_geometry() const { return !vertex_buffers.empty() && !index_buffers.empty(); }
    bool valid()        const { return has_geometry(); }
};

namespace detail {

inline Mat3x4 mat_from_pos_quat(const Vec3& p, const Vec4& q) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    const float x2 = x+x, y2 = y+y, z2 = z+z;
    const float xx = x*x2, xy = x*y2, xz = x*z2;
    const float yy = y*y2, yz = y*z2, zz = z*z2;
    const float wx = w*x2, wy = w*y2, wz = w*z2;
    Mat3x4 m{};
    m.mat[0][0]=1.f-(yy+zz); m.mat[0][1]=xy-wz;      m.mat[0][2]=xz+wy; m.mat[0][3]=p.x;
    m.mat[1][0]=xy+wz;       m.mat[1][1]=1.f-(xx+zz); m.mat[1][2]=yz-wx; m.mat[1][3]=p.y;
    m.mat[2][0]=xz-wy;       m.mat[2][1]=yz+wx;       m.mat[2][2]=1.f-(xx+yy); m.mat[2][3]=p.z;
    return m;
}

inline Mat3x4 mat_mul(const Mat3x4& a, const Mat3x4& b) {
    Mat3x4 r{};
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            r.mat[i][j] = a.mat[i][0]*b.mat[0][j]
                        + a.mat[i][1]*b.mat[1][j]
                        + a.mat[i][2]*b.mat[2][j];
        }
        r.mat[i][3] += a.mat[i][3];
    }
    return r;
}

inline Mat3x4 mat_invert_rigid(const Mat3x4& m) {
    Mat3x4 inv{};
    
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            inv.mat[i][j] = m.mat[j][i];
    
    inv.mat[0][3] = -(inv.mat[0][0]*m.mat[0][3] + inv.mat[0][1]*m.mat[1][3] + inv.mat[0][2]*m.mat[2][3]);
    inv.mat[1][3] = -(inv.mat[1][0]*m.mat[0][3] + inv.mat[1][1]*m.mat[1][3] + inv.mat[1][2]*m.mat[2][3]);
    inv.mat[2][3] = -(inv.mat[2][0]*m.mat[0][3] + inv.mat[2][1]*m.mat[1][3] + inv.mat[2][2]*m.mat[2][3]);
    return inv;
}

inline void compute_inv_bind_poses(ModelData& md) {
    const size_t n = md.skeleton.size();
    std::vector<Mat3x4> world(n);

    for (size_t i = 0; i < n; ++i) {
        const Bone& b = md.skeleton[i];
        Mat3x4 local = mat_from_pos_quat(b.pos_parent, b.rot_parent);
        if (b.parent >= 0 && static_cast<size_t>(b.parent) < i) {
            world[i] = mat_mul(world[b.parent], local);
        } else {
            world[i] = local;
        }
    }

    md.inv_bind_poses.resize(n);
    for (size_t i = 0; i < n; ++i)
        md.inv_bind_poses[i] = mat_invert_rigid(world[i]);
}

inline void extract_skeleton(const kv3::KVValue& root, ModelData& md) {
    
    const kv3::KVValue* skel_val = root.get("m_modelSkeleton");
    if (!skel_val) return;

    const kv3::KVValue* names_val   = skel_val->get("m_boneName");
    const kv3::KVValue* parents_val = skel_val->get("m_nParent");
    const kv3::KVValue* pos_val     = skel_val->get("m_bonePosParent");
    const kv3::KVValue* rot_val     = skel_val->get("m_boneRotParent");
    const kv3::KVValue* ibp_val     = skel_val->get("m_invBindPose");

    if (!names_val || !names_val->is_array()) return;
    const size_t bone_count = names_val->size();
    md.skeleton.reserve(bone_count);

    for (size_t i = 0; i < bone_count; ++i) {
        Bone b;
        if (const auto* v = names_val->get(i); v && v->is_string())
            b.name = v->as_string();

        if (parents_val && parents_val->is_array())
            if (const auto* v = parents_val->get(i))
                b.parent = static_cast<int>(v->as_int());

        if (pos_val && pos_val->is_array()) {
            if (const auto* v = pos_val->get(i)) {
                if (v->is_array() || v->is_object()) {
                    const auto* x = v->get(0);
                    const auto* y = v->get(1);
                    const auto* z = v->get(2);
                    if (x) b.pos_parent.x = static_cast<float>(x->as_float());
                    if (y) b.pos_parent.y = static_cast<float>(y->as_float());
                    if (z) b.pos_parent.z = static_cast<float>(z->as_float());
                }
            }
        }

        if (rot_val && rot_val->is_array()) {
            if (const auto* v = rot_val->get(i)) {
                if (v->is_array() || v->is_object()) {
                    const auto* x = v->get(0);
                    const auto* y = v->get(1);
                    const auto* z = v->get(2);
                    const auto* w = v->get(3);
                    if (x) b.rot_parent.x = static_cast<float>(x->as_float());
                    if (y) b.rot_parent.y = static_cast<float>(y->as_float());
                    if (z) b.rot_parent.z = static_cast<float>(z->as_float());
                    b.rot_parent.w = 1.f;
                    if (w) b.rot_parent.w = static_cast<float>(w->as_float());
                }
            }
        }

        md.skeleton.push_back(std::move(b));
    }

    if (!md.skeleton.empty())
        compute_inv_bind_poses(md);

    if (md.remapping_table.empty()) {
    const kv3::KVValue* remap_val = root.get("m_remappingTable");
    if (!remap_val)
        remap_val = root.get("m_nRemapping");
    if (remap_val && remap_val->is_array()) {
        md.remapping_table.reserve(remap_val->size());
        for (size_t i = 0; i < remap_val->size(); ++i) {
            if (const auto* v = remap_val->get(i))
                md.remapping_table.push_back(static_cast<int>(v->as_int()));
        }
    }
    }

    if (md.remapping_table_starts.empty()) {
    const kv3::KVValue* starts_val = root.get("m_remappingTableStarts");
    if (starts_val && starts_val->is_array()) {
        md.remapping_table_starts.reserve(starts_val->size());
        for (size_t i = 0; i < starts_val->size(); ++i)
            if (const auto* v = starts_val->get(i))
                md.remapping_table_starts.push_back(static_cast<int>(v->as_int()));
    }
    }
}

inline std::string normalize_mesh_resource_path(std::string path) {
    for (char& c : path) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }

    static const std::string k_vmeshc = ".vmesh_c";
    static const std::string k_vmesh  = ".vmesh";
    if (path.size() >= k_vmeshc.size() &&
        path.compare(path.size() - k_vmeshc.size(), k_vmeshc.size(), k_vmeshc) == 0) {
        return path;
    }
    if (path.size() >= k_vmesh.size() &&
        path.compare(path.size() - k_vmesh.size(), k_vmesh.size(), k_vmesh) == 0) {
        path += "_c";
        return path;
    }
    if (path.find(".vmesh") != std::string::npos) {
        return path;
    }
    return path;
}

inline void collect_mesh_refs_recursive(const kv3::KVValue& value,
                                        std::vector<std::string>& out,
                                        int depth = 0) {
    if (depth > 64) return;

    if (value.is_string()) {
        std::string path = normalize_mesh_resource_path(value.as_string());
        if (path.find(".vmesh") != std::string::npos) {
            if (std::find(out.begin(), out.end(), path) == out.end())
                out.push_back(std::move(path));
        }
        return;
    }

    if (value.is_array()) {
        for (size_t i = 0; i < value.size(); ++i)
            if (const auto* child = value.get(i))
                collect_mesh_refs_recursive(*child, out, depth + 1);
        return;
    }

    if (value.is_object()) {
        for (size_t i = 0; i < value.size(); ++i)
            if (const auto* child = value.get(i))
                collect_mesh_refs_recursive(*child, out, depth + 1);
    }
}

inline void extract_mesh_resources(const kv3::KVValue& root, ModelData& md) {
    collect_mesh_refs_recursive(root, md.mesh_resources);
}

inline void extract_mesh_groups(const kv3::KVValue& root, ModelData& md) {

    std::vector<std::string> group_names;
    for (const char* key : {"m_meshGroups", "m_MeshGroups", "m_meshGroupNames"}) {
        if (const auto* grp = root.get(key); grp && grp->is_array()) {
            for (size_t i = 0; i < grp->size(); ++i)
                if (const auto* v = grp->get(i); v && v->is_string())
                    group_names.push_back(v->as_string());
            if (!group_names.empty()) break;
        }
    }

    const kv3::KVValue* meshes = root.get("m_refMeshes");
    if (!meshes) meshes = root.get("m_meshes");
    if (!meshes) if (const auto* mi = root.get("m_modelInfo")) meshes = mi->get("m_meshes");
    if (!meshes || !meshes->is_array()) return;

    const kv3::KVValue* ref_masks = root.get("m_refMeshGroupMasks");

    std::vector<ModelData::MeshGroupInfo> result;

    for (size_t i = 0; i < meshes->size(); ++i) {
        const auto* entry = meshes->get(i);
        if (!entry) continue;

        std::string mesh_name;
        int mask = -1;

        if (entry->is_string()) {
            
            std::string path = entry->as_string();
            const auto sl = path.rfind('/');
            mesh_name = (sl != std::string::npos) ? path.substr(sl + 1) : path;
            const auto dot = mesh_name.rfind('.');
            if (dot != std::string::npos) mesh_name = mesh_name.substr(0, dot);
            
            if (ref_masks && ref_masks->is_array())
                if (const auto* mv = ref_masks->get(i)) mask = static_cast<int>(mv->as_int());
        } else if (entry->is_object()) {
            if (const auto* v = entry->get("m_name")) mesh_name = v->as_string();
            if (const auto* v = entry->get("m_meshGroupMask")) mask = static_cast<int>(v->as_int());
        }

        if (mesh_name.empty()) continue;

        if (!group_names.empty() && mask >= 0) {
            for (size_t gi = 0; gi < group_names.size(); ++gi) {
                if (mask & (1 << static_cast<int>(gi))) {
                    auto it = std::find_if(result.begin(), result.end(),
                        [&](const ModelData::MeshGroupInfo& g) { return g.group_name == group_names[gi]; });
                    if (it == result.end())
                        result.push_back({group_names[gi], {mesh_name}});
                    else
                        it->meshes.push_back(mesh_name);
                }
            }
        } else {
            
            if (result.empty()) result.push_back({"", {}});
            result[0].meshes.push_back(mesh_name);
        }
    }

    if (!result.empty())
        md.mesh_group_info = std::move(result);
}

inline bool kv_bool(const kv3::KVValue* v, bool fallback = false) {
    if (!v) return fallback;
    if (v->type == kv3::KVType::Boolean_true) return true;
    if (v->type == kv3::KVType::Boolean_false) return false;
    return v->as_int() != 0;
}

inline bool decode_vertex_buffer_from_block(const kv3::KVValue& vb_meta,
                                            const ResHeader& hdr,
                                            const uint8_t* data,
                                            size_t size,
                                            VertexBuffer& out_vb) {
    const auto* block_idx_v = vb_meta.get("m_nBlockIndex");
    const auto* count_v = vb_meta.get("m_nElementCount");
    const auto* stride_v = vb_meta.get("m_nElementSizeInBytes");
    if (!block_idx_v || !count_v || !stride_v) return false;

    const int block_idx = static_cast<int>(block_idx_v->as_int());
    const uint32_t count = static_cast<uint32_t>(count_v->as_int());
    const uint32_t stride = static_cast<uint32_t>(stride_v->as_int());
    if (block_idx < 0 || static_cast<size_t>(block_idx) >= hdr.blocks.size()) return false;
    if (count == 0 || stride == 0) return false;

    const ResBlock& blk = hdr.blocks[static_cast<size_t>(block_idx)];
    if (blk.offset + blk.size > size) return false;

    out_vb.vertex_count = count;
    out_vb.vertex_size = stride;
    out_vb.data.resize(static_cast<size_t>(count) * stride);

    const bool meshopt_compressed = kv_bool(vb_meta.get("m_bMeshoptCompressed"), false);
    if (meshopt_compressed) {
        if (meshopt_decodeVertexBuffer(out_vb.data.data(), count, stride, data + blk.offset, blk.size) != 0) {
            return false;
        }
    } else {
        const size_t need_bytes = static_cast<size_t>(count) * stride;
        if (blk.size < need_bytes) return false;
        std::memcpy(out_vb.data.data(), data + blk.offset, need_bytes);
    }

    const auto* layout = vb_meta.get("m_inputLayoutFields");
    if (layout && layout->is_array()) {
        out_vb.attributes.reserve(layout->size());
        for (size_t i = 0; i < layout->size(); ++i) {
            const auto* f = layout->get(i);
            if (!f || !f->is_object()) continue;

            VertexAttribute a{};
            if (const auto* s = f->get("m_pSemanticName"); s && s->is_string()) a.semantic_name = s->as_string();
            if (const auto* n = f->get("m_nSemanticIndex")) a.semantic_index = static_cast<uint32_t>(n->as_int());
            if (const auto* fmt = f->get("m_Format")) a.format = static_cast<DxgiFormat>(fmt->as_int());
            if (const auto* off = f->get("m_nOffset")) a.byte_offset = static_cast<uint32_t>(off->as_int());
            if (const auto* sl = f->get("m_nSlot")) a.slot = static_cast<uint32_t>(sl->as_int());
            out_vb.attributes.push_back(std::move(a));
        }
    }

    return !out_vb.data.empty();
}

inline bool decode_index_buffer_from_block(const kv3::KVValue& ib_meta,
                                           const ResHeader& hdr,
                                           const uint8_t* data,
                                           size_t size,
                                           IndexBuffer& out_ib) {
    const auto* block_idx_v = ib_meta.get("m_nBlockIndex");
    const auto* count_v = ib_meta.get("m_nElementCount");
    const auto* elem_size_v = ib_meta.get("m_nElementSizeInBytes");
    if (!block_idx_v || !count_v || !elem_size_v) return false;

    const int block_idx = static_cast<int>(block_idx_v->as_int());
    const uint32_t count = static_cast<uint32_t>(count_v->as_int());
    const uint32_t index_size = static_cast<uint32_t>(elem_size_v->as_int());
    if (block_idx < 0 || static_cast<size_t>(block_idx) >= hdr.blocks.size()) return false;
    if (count == 0 || (index_size != 2 && index_size != 4)) return false;

    const ResBlock& blk = hdr.blocks[static_cast<size_t>(block_idx)];
    if (blk.offset + blk.size > size) return false;

    out_ib.index_count = count;
    out_ib.index_size = index_size;
    out_ib.data.resize(static_cast<size_t>(count) * index_size);

    const bool meshopt_compressed = kv_bool(ib_meta.get("m_bMeshoptCompressed"), false);
    const bool meshopt_sequence = kv_bool(ib_meta.get("m_bMeshoptIndexSequence"), false);
    if (meshopt_compressed) {
        int rc = 0;
        if (meshopt_sequence) {
            rc = meshopt_decodeIndexSequence(out_ib.data.data(), count, index_size, data + blk.offset, blk.size);
        } else {
            rc = meshopt_decodeIndexBuffer(out_ib.data.data(), count, index_size, data + blk.offset, blk.size);
        }
        if (rc != 0) return false;
    } else {
        const size_t need_bytes = static_cast<size_t>(count) * index_size;
        if (blk.size < need_bytes) return false;
        std::memcpy(out_ib.data.data(), data + blk.offset, need_bytes);
    }

    return !out_ib.data.empty();
}

inline bool extract_embedded_meshes_from_ctrl(const kv3::KVValue& ctrl_root,
                                              const ResHeader& hdr,
                                              const uint8_t* data,
                                              size_t size,
                                              ModelData& md) {
    const auto* embedded = ctrl_root.get("embedded_meshes");
    if (!embedded || !embedded->is_array()) return false;

    bool any_geometry = false;
    for (size_t ei = 0; ei < embedded->size(); ++ei) {
        const auto* em = embedded->get(ei);
        if (!em || !em->is_object()) continue;

        const size_t remap_section_begin = md.remapping_table.size();

        const kv3::KVValue* mesh_remap = em->get("m_nRemappingTable");
        if (!mesh_remap) mesh_remap = em->get("m_nRemapping");
        if (!mesh_remap) mesh_remap = em->get("m_remappingTable");
        if (mesh_remap && mesh_remap->is_array()) {
            md.remapping_table.reserve(md.remapping_table.size() + mesh_remap->size());
            for (size_t ri = 0; ri < mesh_remap->size(); ++ri) {
                if (const auto* v = mesh_remap->get(ri))
                    md.remapping_table.push_back(static_cast<int>(v->as_int()));
            }
        }

        const bool section_grew = (md.remapping_table.size() > remap_section_begin);

        const auto* vbs = em->get("m_vertexBuffers");
        const auto* ibs = em->get("m_indexBuffers");

        if (!vbs || !vbs->is_array() || !ibs || !ibs->is_array()) {
            const auto* vbib_blk_v = em->get("vbib_block");
            if (!vbib_blk_v) { md.embedded_mesh_vb_counts.push_back(0); continue; }

            const int vbib_idx = static_cast<int>(vbib_blk_v->as_int());
            if (vbib_idx < 0 || static_cast<size_t>(vbib_idx) >= hdr.blocks.size()) {
                md.embedded_mesh_vb_counts.push_back(0);
                continue;
            }

            const ResBlock& mbuf_blk = hdr.blocks[static_cast<size_t>(vbib_idx)];
            if (mbuf_blk.offset + mbuf_blk.size > size) {
                md.embedded_mesh_vb_counts.push_back(0);
                continue;
            }

            VBIB mbuf = parse_mbuf(data + mbuf_blk.offset, mbuf_blk.size);

            size_t vbs_added = 0;
            for (auto& vb : mbuf.vbs) {
                if (vb.data.empty()) continue;
                if (section_grew) {
                    md.remapping_table_starts.push_back(
                        static_cast<int>(remap_section_begin));
                }
                md.vertex_buffers.push_back(std::move(vb));
                any_geometry = true;
                ++vbs_added;
            }
            md.embedded_mesh_vb_counts.push_back(vbs_added);

            for (auto& ib : mbuf.ibs) {
                if (ib.data.empty()) continue;
                md.index_buffers.push_back(std::move(ib));
            }

            if (any_geometry) break;
            continue;
        }

        size_t vbs_added = 0;
        for (size_t vi = 0; vi < vbs->size(); ++vi) {
            const auto* vb_meta = vbs->get(vi);
            if (!vb_meta || !vb_meta->is_object()) continue;
            VertexBuffer vb;
            if (decode_vertex_buffer_from_block(*vb_meta, hdr, data, size, vb)) {
                
                if (section_grew) {
                    md.remapping_table_starts.push_back(
                        static_cast<int>(remap_section_begin));
                }
                md.vertex_buffers.push_back(std::move(vb));
                any_geometry = true;
                ++vbs_added;
            }
        }
        md.embedded_mesh_vb_counts.push_back(vbs_added);

        for (size_t ii = 0; ii < ibs->size(); ++ii) {
            const auto* ib_meta = ibs->get(ii);
            if (!ib_meta || !ib_meta->is_object()) continue;
            IndexBuffer ib;
            if (decode_index_buffer_from_block(*ib_meta, hdr, data, size, ib)) {
                md.index_buffers.push_back(std::move(ib));
            }
        }
    }

    return any_geometry && !md.vertex_buffers.empty() && !md.index_buffers.empty();
}

inline float f16_to_f32(uint16_t h) {
    const uint32_t s = (h >> 15) & 0x1u;
    const uint32_t e = (h >> 10) & 0x1Fu;
    const uint32_t m = h & 0x3FFu;
    uint32_t bits;
    if (e == 0) {
        bits = (s << 31) | (m << 13);  
    } else if (e == 31) {
        bits = (s << 31) | 0x7F800000u | (m << 13);  
    } else {
        bits = (s << 31) | ((e + 112) << 23) | (m << 13);
    }
    float f; std::memcpy(&f, &bits, 4); return f;
}

inline Vec3 decode_normal_r32(uint32_t v) {
    
    auto snorm10 = [](uint32_t bits) -> float {
        int32_t s = static_cast<int32_t>(bits & 0x3FF);
        if (s >= 512) s -= 1024;
        return std::fmaxf(-1.f, s / 511.f);
    };
    Vec3 n;
    n.x = snorm10(v);
    n.y = snorm10(v >> 10);
    n.z = snorm10(v >> 20);
    
    float len = std::sqrtf(n.x*n.x + n.y*n.y + n.z*n.z);
    if (len > 1e-6f) { n.x /= len; n.y /= len; n.z /= len; }
    return n;
}

} 

inline std::string get_data_root_keys(const uint8_t* data, size_t size) {
    auto hdr_opt = parse_res_header(data, size);
    if (!hdr_opt) return std::string("no-header");
    const ResBlock* data_blk = find_block(*hdr_opt, "DATA");
    if (!data_blk || data_blk->offset + data_blk->size > size) return std::string("no-DATA-block");
    auto kv_opt = kv3::parse_binary(data + data_blk->offset, data_blk->size);
    if (!kv_opt) return std::string("kv3-parse-failed");
    if (!kv_opt->is_object() || kv_opt->size() == 0) return std::string("root-not-object");
    std::string keys;
    const size_t n = std::min(kv_opt->size(), (size_t)24);
    for (size_t i = 0; i < n; ++i) {
        if (i) keys += ", ";
        keys += kv_opt->as_object()[i].first;
    }
    return keys;
}

inline void extract_vmesh_c_remap(const uint8_t* data, size_t size, ModelData& md) {
    const bool need_table  = md.remapping_table.empty();
    const bool need_starts = md.remapping_table_starts.empty();
    if (!need_table && !need_starts) return;  
    auto hdr_opt = parse_res_header(data, size);
    if (!hdr_opt) return;
    const ResHeader& hdr = *hdr_opt;

    const ResBlock* data_blk = find_block(hdr, "DATA");
    if (data_blk && data_blk->offset + data_blk->size <= size) {
        auto kv_opt = kv3::parse_binary(data + data_blk->offset, data_blk->size);
        if (kv_opt) {
            if (need_table) {
                const kv3::KVValue* remap_val = kv_opt->get("m_remappingTable");
                if (!remap_val) remap_val = kv_opt->get("m_nRemapping");
                if (remap_val && remap_val->is_array()) {
                    md.remapping_table.reserve(remap_val->size());
                    for (size_t i = 0; i < remap_val->size(); ++i) {
                        if (const auto* v = remap_val->get(i))
                            md.remapping_table.push_back(static_cast<int>(v->as_int()));
                    }
                }
            }

            if (need_starts) {
                const kv3::KVValue* starts_val = kv_opt->get("m_remappingTableStarts");
                if (starts_val && starts_val->is_array()) {
                    md.remapping_table_starts.reserve(starts_val->size());
                    for (size_t i = 0; i < starts_val->size(); ++i) {
                        if (const auto* v = starts_val->get(i))
                            md.remapping_table_starts.push_back(static_cast<int>(v->as_int()));
                    }
                }
            }
        }
    }
}

inline void build_per_vb_remaps(ModelData& md) {
    const size_t n_vbs = md.vertex_buffers.size();
    md.per_vb_remap.clear();
    md.per_vb_remap.resize(n_vbs);

    if (md.remapping_table.empty()) return;  

    struct Section { size_t begin; size_t end; };
    std::vector<Section> sections;

    if (md.remapping_table_starts.empty()) {
        
        sections.push_back({0, md.remapping_table.size()});
        for (size_t i = 0; i < n_vbs; ++i) {
            md.per_vb_remap[i] = md.remapping_table;
        }
        return;
    }

    const size_t table_sz = md.remapping_table.size();

    std::vector<size_t> unique_starts;
    for (int s : md.remapping_table_starts) {
        size_t val = (std::min)(static_cast<size_t>((std::max)(s, 0)), table_sz);
        unique_starts.push_back(val);
    }
    std::sort(unique_starts.begin(), unique_starts.end());
    unique_starts.erase(std::unique(unique_starts.begin(), unique_starts.end()),
                        unique_starts.end());

    for (size_t i = 0; i < unique_starts.size(); ++i) {
        size_t b = unique_starts[i];
        size_t e = (i + 1 < unique_starts.size()) ? unique_starts[i + 1] : table_sz;
        sections.push_back({b, e});
    }

    auto find_section_for_start = [&](size_t start_val) -> size_t {
        for (size_t i = 0; i < sections.size(); ++i) {
            if (sections[i].begin == start_val) return i;
        }
        return sections.empty() ? 0 : sections.size() - 1;
    };

    auto extract_section = [&](size_t si) -> std::vector<int> {
        if (si >= sections.size()) return {};
        const auto& s = sections[si];
        if (s.begin >= s.end) return {};
        return std::vector<int>(
            md.remapping_table.begin() + static_cast<std::ptrdiff_t>(s.begin),
            md.remapping_table.begin() + static_cast<std::ptrdiff_t>(s.end));
    };

    const size_t n_starts = md.remapping_table_starts.size();
    const size_t n_mesh_counts = md.embedded_mesh_vb_counts.size();

    if (n_mesh_counts > 0 && n_starts <= n_mesh_counts) {
        
        size_t vb_cursor = 0;
        for (size_t mesh_ref = 0; mesh_ref < n_mesh_counts; ++mesh_ref) {
            const size_t vb_count = md.embedded_mesh_vb_counts[mesh_ref];

            size_t si = 0;
            if (mesh_ref < n_starts) {
                size_t start_val = (std::min)(
                    static_cast<size_t>((std::max)(md.remapping_table_starts[mesh_ref], 0)),
                    table_sz);
                si = find_section_for_start(start_val);
            } else {
                
                size_t start_val = (std::min)(
                    static_cast<size_t>((std::max)(md.remapping_table_starts.back(), 0)),
                    table_sz);
                si = find_section_for_start(start_val);
            }

            auto remap = extract_section(si);
            for (size_t k = 0; k < vb_count && (vb_cursor + k) < n_vbs; ++k) {
                md.per_vb_remap[vb_cursor + k] = remap;
            }
            vb_cursor += vb_count;
        }

        if (vb_cursor < n_vbs && !sections.empty()) {
            auto last_remap = extract_section(sections.size() - 1);
            for (size_t vi = vb_cursor; vi < n_vbs; ++vi) {
                md.per_vb_remap[vi] = last_remap;
            }
        }
    } else {
        
        size_t last_si = 0;
        for (size_t vi = 0; vi < n_vbs; ++vi) {
            if (vi < n_starts) {
                size_t start_val = (std::min)(
                    static_cast<size_t>((std::max)(md.remapping_table_starts[vi], 0)),
                    table_sz);
                last_si = find_section_for_start(start_val);
            }
            md.per_vb_remap[vi] = extract_section(last_si);
        }
    }
}

inline std::optional<VBIB> parse_vmesh_c(const uint8_t* data, size_t size) {
    auto hdr_opt = parse_res_header(data, size);
    if (!hdr_opt) return std::nullopt;
    const ResHeader& hdr = *hdr_opt;

    const ResBlock* vbib_blk = find_block(hdr, "VBIB");
    if (!vbib_blk) return std::nullopt;
    if (vbib_blk->offset + vbib_blk->size > size) return std::nullopt;

    VBIB vbib = parse_vbib(data + vbib_blk->offset, vbib_blk->size);
    if (vbib.vbs.empty() || vbib.ibs.empty()) return std::nullopt;
    return vbib;
}

struct SVGDocument {
    std::string xml{};
    float width{0.0f};
    float height{0.0f};
};

inline std::optional<SVGDocument> parse_vsvg_c(const uint8_t* data, size_t size) {
    if (!data || size < 8) {
        return std::nullopt;
    }

    auto find_tag = [&](const char* tag, size_t from) -> size_t {
        const auto tag_len = std::strlen(tag);
        if (tag_len == 0 || size < tag_len || from >= size) {
            return std::string::npos;
        }
        for (size_t i = from; i + tag_len <= size; ++i) {
            if (std::memcmp(data + i, tag, tag_len) == 0) {
                return i;
            }
        }
        return std::string::npos;
    };

    const size_t svg_begin = find_tag("<svg", 0);
    if (svg_begin == std::string::npos) {
        return std::nullopt;
    }

    const size_t svg_end_tag = find_tag("</svg>", svg_begin);
    if (svg_end_tag == std::string::npos) {
        return std::nullopt;
    }

    const size_t svg_end = svg_end_tag + std::strlen("</svg>");
    if (svg_end <= svg_begin || svg_end > size) {
        return std::nullopt;
    }

    SVGDocument out{};
    out.xml.assign(reinterpret_cast<const char*>(data + svg_begin), svg_end - svg_begin);

    auto parse_dimension = [&](const char* key) -> float {
        const auto key_pos = out.xml.find(key);
        if (key_pos == std::string::npos) {
            return 0.0f;
        }
        const auto q1 = out.xml.find('"', key_pos);
        if (q1 == std::string::npos) {
            return 0.0f;
        }
        const auto q2 = out.xml.find('"', q1 + 1);
        if (q2 == std::string::npos || q2 <= q1 + 1) {
            return 0.0f;
        }
        const auto raw = out.xml.substr(q1 + 1, q2 - (q1 + 1));
        std::string filtered{};
        filtered.reserve(raw.size());
        for (char ch : raw) {
            if ((ch >= '0' && ch <= '9') || ch == '.' || ch == '-') {
                filtered.push_back(ch);
            } else if (!filtered.empty()) {
                break;
            }
        }
        if (filtered.empty()) {
            return 0.0f;
        }
        try {
            return std::stof(filtered);
        } catch (...) {
            return 0.0f;
        }
    };

    out.width = parse_dimension("width=");
    out.height = parse_dimension("height=");
    return out;
}

inline std::optional<ModelData> parse_vmdl_c(const uint8_t* data, size_t size) {
    auto hdr_opt = parse_res_header(data, size);
    if (!hdr_opt) return std::nullopt;
    const ResHeader& hdr = *hdr_opt;

    ModelData md;

    if (const ResBlock* vbib_blk = find_block(hdr, "VBIB")) {
        if (vbib_blk->offset + vbib_blk->size > size) return std::nullopt;
        VBIB vbib = parse_vbib(data + vbib_blk->offset, vbib_blk->size);
        md.vertex_buffers = std::move(vbib.vbs);
        md.index_buffers  = std::move(vbib.ibs);
        if (md.has_geometry()) md.geometry_source = 1; 
    }

    if (const ResBlock* ctrl_blk = find_block(hdr, "CTRL")) {
        if (ctrl_blk->offset + ctrl_blk->size > size) return std::nullopt;
        auto ctrl_opt = kv3::parse_binary(data + ctrl_blk->offset, ctrl_blk->size);
        if (ctrl_opt) {
            
            auto vbib_vbs = std::move(md.vertex_buffers);
            auto vbib_ibs = std::move(md.index_buffers);
            md.vertex_buffers.clear();
            md.index_buffers.clear();

            detail::extract_embedded_meshes_from_ctrl(*ctrl_opt, hdr, data, size, md);

            if (md.has_geometry()) {
                
                md.geometry_source = 2; 
            } else {
                
                md.vertex_buffers = std::move(vbib_vbs);
                md.index_buffers  = std::move(vbib_ibs);
            }
        }
    }

    const ResBlock* data_blk = find_block(hdr, "DATA");
    if (data_blk && data_blk->offset + data_blk->size <= size) {
        auto kv_opt = kv3::parse_binary(data + data_blk->offset, data_blk->size);
        if (kv_opt) {
            detail::extract_skeleton(*kv_opt, md);
            detail::extract_mesh_resources(*kv_opt, md);
            detail::extract_mesh_groups(*kv_opt, md);
        }
        
    }

    if (!md.has_geometry() && md.mesh_resources.empty() && !md.has_skeleton())
        return std::nullopt;
    return md;
}

inline Vec3 read_attr_vec3(const VertexBuffer& vb, uint32_t vertex_idx,
                            const VertexAttribute& attr) {
    const uint8_t* p = vb.data.data()
                     + (size_t)vertex_idx * vb.vertex_size
                     + attr.byte_offset;
    Vec3 out{};
    switch (attr.format) {
    case DxgiFormat::R32G32B32_FLOAT:
    case DxgiFormat::R32G32B32A32_FLOAT:
        out.x = detail::rd<float>(p + 0);
        out.y = detail::rd<float>(p + 4);
        out.z = detail::rd<float>(p + 8);
        break;
    case DxgiFormat::R32_UINT:
        
        out = detail::decode_normal_r32(detail::rd<uint32_t>(p));
        break;
    default: break;
    }
    return out;
}

inline std::pair<float,float> read_attr_uv(const VertexBuffer& vb, uint32_t vertex_idx,
                                            const VertexAttribute& attr) {
    const uint8_t* p = vb.data.data()
                     + (size_t)vertex_idx * vb.vertex_size
                     + attr.byte_offset;
    switch (attr.format) {
    case DxgiFormat::R32G32_FLOAT: {
        return { detail::rd<float>(p), detail::rd<float>(p+4) };
    }
    case DxgiFormat::R16G16_FLOAT: {
        return { detail::f16_to_f32(detail::rd<uint16_t>(p)),
                 detail::f16_to_f32(detail::rd<uint16_t>(p+2)) };
    }
    case DxgiFormat::R16G16_UNORM: {
        return { detail::rd<uint16_t>(p) / 65535.f,
                 detail::rd<uint16_t>(p+2) / 65535.f };
    }
    case DxgiFormat::R16G16_SNORM: {
        const int16_t s0 = detail::rd<int16_t>(p);
        const int16_t s1 = detail::rd<int16_t>(p+2);
        return { s0 / 32767.f, s1 / 32767.f };
    }
    default: return {0.f, 0.f};
    }
}

inline std::array<uint16_t,4> read_attr_blend_indices(const VertexBuffer& vb,
                                                        uint32_t vertex_idx,
                                                        const VertexAttribute& attr) {
    const uint8_t* p = vb.data.data()
                     + (size_t)vertex_idx * vb.vertex_size
                     + attr.byte_offset;
    switch (attr.format) {
    case DxgiFormat::R8G8B8A8_UINT:
        return { (uint16_t)p[0], (uint16_t)p[1], (uint16_t)p[2], (uint16_t)p[3] };
    case DxgiFormat::R16G16B16A16_UINT:
        return { detail::rd<uint16_t>(p),   detail::rd<uint16_t>(p+2),
                 detail::rd<uint16_t>(p+4), detail::rd<uint16_t>(p+6) };
    default:
        return {0, 0, 0, 0};
    }
}

inline std::array<uint16_t,4> read_attr_blend_indices(const VertexBuffer& vb,
                                                       uint32_t vertex_idx,
                                                       const VertexAttribute& attr,
                                                       const std::vector<int>* remapping_table) {
    auto indices = read_attr_blend_indices(vb, vertex_idx, attr);
    if (remapping_table && !remapping_table->empty()) {
        for (int i = 0; i < 4; ++i) {
            const size_t idx = static_cast<size_t>(indices[i]);
            if (idx < remapping_table->size())
                indices[i] = static_cast<uint16_t>((*remapping_table)[idx]);
            else
                indices[i] = 0;
        }
    }
    return indices;
}

inline std::array<float,4> read_attr_blend_weights(const VertexBuffer& vb,
                                                    uint32_t vertex_idx,
                                                    const VertexAttribute& attr) {
    const uint8_t* p = vb.data.data()
                     + (size_t)vertex_idx * vb.vertex_size
                     + attr.byte_offset;
    switch (attr.format) {
    case DxgiFormat::R8G8B8A8_UNORM:
        return { p[0]/255.f, p[1]/255.f, p[2]/255.f, p[3]/255.f };
    case DxgiFormat::R16G16B16A16_FLOAT:
        return { detail::f16_to_f32(detail::rd<uint16_t>(p)),
                 detail::f16_to_f32(detail::rd<uint16_t>(p+2)),
                 detail::f16_to_f32(detail::rd<uint16_t>(p+4)),
                 detail::f16_to_f32(detail::rd<uint16_t>(p+6)) };
    default:
        return {1.f, 0.f, 0.f, 0.f};
    }
}

}
