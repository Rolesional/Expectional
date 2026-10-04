#include "VtexParser.hpp"

#include "VPK.hpp"

#define STBI_NO_SIMD
#include "stb_image.h"

#include "Zstd.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace vtex {

static std::string s_last_error;

static void set_error(const std::string& msg) { s_last_error = msg; }
static void clear_error() { s_last_error.clear(); }

const std::string& last_error() { return s_last_error; }

struct ResBlock {
    char     type[5]{};
    uint32_t offset = 0;   
    uint32_t size   = 0;
};

struct ResHeader {
    uint32_t file_size = 0;
    std::vector<ResBlock> blocks;
};

static bool parse_res_header(const uint8_t* data, size_t sz, ResHeader& out) {
    if (!data || sz < 16) return false;

    std::memcpy(&out.file_size, data + 0, 4);
    uint32_t block_offset_field, block_count;
    std::memcpy(&block_offset_field, data + 8,  4);
    std::memcpy(&block_count,        data + 12, 4);

    const size_t blocks_start = 8 + block_offset_field;
    const size_t blocks_end   = blocks_start + (size_t)block_count * 12;
    if (blocks_end > sz) return false;

    out.blocks.reserve(block_count);
    for (uint32_t i = 0; i < block_count; ++i) {
        const size_t b = blocks_start + i * 12;
        ResBlock blk{};
        std::memcpy(blk.type, data + b, 4); blk.type[4] = '\0';
        uint32_t rel_off, bsz;
        std::memcpy(&rel_off, data + b + 4, 4);
        std::memcpy(&bsz,     data + b + 8, 4);
        blk.offset = static_cast<uint32_t>(b + 4 + rel_off);
        blk.size   = bsz;
        out.blocks.push_back(blk);
    }
    return !out.blocks.empty();
}

static const ResBlock* find_block(const ResHeader& hdr, const char* type) {
    for (const auto& b : hdr.blocks)
        if (std::strncmp(b.type, type, 4) == 0) return &b;
    return nullptr;
}

static bool lz4_decompress(const uint8_t* src, size_t src_sz,
                             uint8_t* dst, size_t dst_sz) {
    size_t si = 0, di = 0;
    while (di < dst_sz) {
        if (si >= src_sz) return false;
        const uint8_t token = src[si++];
        size_t lit_len = token >> 4;
        if (lit_len == 15) {
            uint8_t ex;
            do { if (si >= src_sz) return false; ex = src[si++]; lit_len += ex; } while (ex == 255);
        }
        if (si + lit_len > src_sz || di + lit_len > dst_sz) return false;
        std::memcpy(dst + di, src + si, lit_len);
        si += lit_len; di += lit_len;
        if (di == dst_sz) break;
        if (si + 2 > src_sz) return false;
        const uint16_t moff = static_cast<uint16_t>(src[si] | (src[si+1] << 8)); si += 2;
        if (moff == 0 || di < moff) return false;
        size_t mlen = (token & 0xF) + 4;
        if ((token & 0xF) == 15) {
            uint8_t ex;
            do { if (si >= src_sz) return false; ex = src[si++]; mlen += ex; } while (ex == 255);
        }
        if (di + mlen > dst_sz) return false;
        const size_t ms = di - moff;
        for (size_t i = 0; i < mlen; ++i) dst[di++] = dst[ms + i];
    }
    return true;
}

enum class VTexFmt : uint32_t {
    UNKNOWN         = 0,
    DXT1            = 1,
    DXT5            = 2,
    I8              = 3,
    RGBA8888        = 4,
    R16             = 5,
    RG1616          = 6,
    RGBA16161616    = 7,
    R16F            = 8,
    RG1616F         = 9,
    RGBA16161616F   = 10,
    R32F            = 11,
    RG3232F         = 12,
    RGB323232F      = 13,
    RGBA32323232F   = 14,
    JPEG_RGBA8888   = 15,
    PNG_RGBA8888    = 16,
    JPEG_DXT5       = 17,
    PNG_DXT5        = 18,
    BC6H            = 19,
    BC7             = 20,
    ATI2N           = 21,
    IA88            = 22,
    ETC2            = 23,
    ETC2_EAC        = 24,
    R11_EAC         = 25,
    RG11_EAC        = 26,
    ATI1N           = 27,
    BGRA8888        = 28,
    WEBP_RGBA8888   = 29,
    WEBP_DXT5       = 30,
};

static const char* vtex_fmt_name(VTexFmt f) {
    switch (f) {
    case VTexFmt::UNKNOWN:        return "UNKNOWN";
    case VTexFmt::DXT1:           return "DXT1 (BC1)";
    case VTexFmt::DXT5:           return "DXT5 (BC3)";
    case VTexFmt::I8:             return "I8";
    case VTexFmt::RGBA8888:       return "RGBA8888";
    case VTexFmt::R16:            return "R16";
    case VTexFmt::RG1616:         return "RG1616";
    case VTexFmt::RGBA16161616:   return "RGBA16161616";
    case VTexFmt::R16F:           return "R16F";
    case VTexFmt::RG1616F:        return "RG1616F";
    case VTexFmt::RGBA16161616F:  return "RGBA16161616F";
    case VTexFmt::R32F:           return "R32F";
    case VTexFmt::RG3232F:        return "RG3232F";
    case VTexFmt::RGB323232F:     return "RGB323232F";
    case VTexFmt::RGBA32323232F:  return "RGBA32323232F";
    case VTexFmt::JPEG_RGBA8888:  return "JPEG_RGBA8888";
    case VTexFmt::PNG_RGBA8888:   return "PNG_RGBA8888";
    case VTexFmt::JPEG_DXT5:      return "JPEG_DXT5";
    case VTexFmt::PNG_DXT5:       return "PNG_DXT5";
    case VTexFmt::BC6H:           return "BC6H";
    case VTexFmt::BC7:            return "BC7";
    case VTexFmt::ATI2N:          return "ATI2N (BC5)";
    case VTexFmt::IA88:           return "IA88";
    case VTexFmt::ETC2:           return "ETC2";
    case VTexFmt::ETC2_EAC:       return "ETC2_EAC";
    case VTexFmt::R11_EAC:        return "R11_EAC";
    case VTexFmt::RG11_EAC:       return "RG11_EAC";
    case VTexFmt::ATI1N:          return "ATI1N (BC4)";
    case VTexFmt::BGRA8888:       return "BGRA8888";
    case VTexFmt::WEBP_RGBA8888:  return "WEBP_RGBA8888";
    case VTexFmt::WEBP_DXT5:      return "WEBP_DXT5";
    default:                      return "INVALID";
    }
}

static int block_size(VTexFmt f) {
    switch (f) {
    case VTexFmt::DXT1:           return 8;
    case VTexFmt::DXT5:           return 16;
    case VTexFmt::RGBA8888:       return 4;
    case VTexFmt::R16:            return 2;
    case VTexFmt::RG1616:         return 4;
    case VTexFmt::RGBA16161616:   return 8;
    case VTexFmt::R16F:           return 2;
    case VTexFmt::RG1616F:        return 4;
    case VTexFmt::RGBA16161616F:  return 8;
    case VTexFmt::R32F:           return 4;
    case VTexFmt::RG3232F:        return 8;
    case VTexFmt::RGB323232F:     return 12;
    case VTexFmt::RGBA32323232F:  return 16;
    case VTexFmt::BC6H:           return 16;
    case VTexFmt::BC7:            return 16;
    case VTexFmt::IA88:           return 2;
    case VTexFmt::ETC2:           return 8;
    case VTexFmt::ETC2_EAC:       return 16;
    case VTexFmt::BGRA8888:       return 4;
    case VTexFmt::ATI1N:          return 8;
    case VTexFmt::ATI2N:          return 16;
    case VTexFmt::I8:             return 1;
    default:                      return 1;
    }
}

static bool is_block_compressed(VTexFmt f) {
    return f == VTexFmt::DXT1 || f == VTexFmt::DXT5 ||
           f == VTexFmt::BC6H || f == VTexFmt::BC7 ||
           f == VTexFmt::ETC2 || f == VTexFmt::ETC2_EAC ||
           f == VTexFmt::ATI1N || f == VTexFmt::ATI2N;
}

static bool is_raw_image(VTexFmt f) {
    return f == VTexFmt::JPEG_RGBA8888 || f == VTexFmt::JPEG_DXT5 ||
           f == VTexFmt::PNG_RGBA8888  || f == VTexFmt::PNG_DXT5 ||
           f == VTexFmt::WEBP_RGBA8888 || f == VTexFmt::WEBP_DXT5;
}

static int mip_level_size(int size, int level) {
    return std::max(size >> level, 1);
}

static size_t calc_mip_buffer_size(int w, int h, int depth, VTexFmt fmt) {
    const int bpp = block_size(fmt);

    if (is_block_compressed(fmt)) {
        int misalign = w % 4;
        if (misalign > 0) w += 4 - misalign;
        misalign = h % 4;
        if (misalign > 0) h += 4 - misalign;
        if (w < 4 && w > 0) w = 4;
        if (h < 4 && h > 0) h = 4;
        if (depth < 4 && depth > 1) depth = 4;
        const int numBlocks = (w * h) >> 4;
        return (size_t)numBlocks * depth * bpp;
    }

    return (size_t)w * h * depth * bpp;
}

enum VTexFlags : uint16_t {
    VTEX_FLAG_NONE          = 0,
    VTEX_FLAG_CUBE_TEXTURE  = 1 << 4,
    VTEX_FLAG_VOLUME_TEXTURE= 1 << 5,
    VTEX_FLAG_TEXTURE_ARRAY = 1 << 6,
};

struct VtexInfo {
    uint16_t version = 0;
    uint16_t flags = 0;
    uint16_t width = 0, height = 0, depth = 1;
    uint8_t  format = 0, num_mips = 1;
    uint16_t non_pow2_w = 0, non_pow2_h = 0;
    bool     is_actually_compressed = false;
    std::vector<int32_t> compressed_mip_sizes;
};

static bool parse_vtex_data(const uint8_t* blk, size_t sz, VtexInfo& out) {
    if (sz < 32) return false;
    size_t p = 0;
    auto r16 = [&]{ uint16_t v; std::memcpy(&v,blk+p,2); p+=2; return v; };
    auto r32 = [&]{ uint32_t v; std::memcpy(&v,blk+p,4); p+=4; return v; };
    auto r8  = [&]{ return blk[p++]; };

    out.version = r16();
    out.flags   = r16();
    p += 16;            
    out.width  = r16();
    out.height = r16();
    out.depth  = r16();
    out.format   = r8();
    out.num_mips = r8();
    r32();              

    const size_t extra_hdr = p;
    const uint32_t extra_off = r32();
    const uint32_t extra_cnt = r32();

    if (extra_cnt > 0) {
        size_t ep = extra_hdr + extra_off;
        for (uint32_t i = 0; i < extra_cnt; ++i) {
            if (ep + 12 > sz) break;
            uint32_t type, raw_off, bsz;
            std::memcpy(&type,    blk+ep+0, 4);
            std::memcpy(&raw_off, blk+ep+4, 4);
            std::memcpy(&bsz,     blk+ep+8, 4);
            ep += 12;
            if (raw_off < 8) continue;
            const size_t da = ep + raw_off - 8;

            if (type == 3  && da + 6 <= sz) {
                
                uint16_t skip_v, nw, nh;
                std::memcpy(&skip_v, blk+da, 2);
                std::memcpy(&nw, blk+da+2, 2);
                std::memcpy(&nh, blk+da+4, 2);
                if (nw > 0 && nh > 0 && out.width >= nw && out.height >= nh) {
                    if (!(nw == 1 && out.width != 4)) out.non_pow2_w = nw;
                    if (!(nh == 1 && out.height != 4)) out.non_pow2_h = nh;
                }
            }
            else if (type == 4  && da + 12 <= sz) {
                uint32_t int1;
                std::memcpy(&int1, blk+da, 4);
                out.is_actually_compressed = (int1 == 1);
                uint32_t mips_off, mips_cnt;
                std::memcpy(&mips_off, blk+da+4, 4);
                std::memcpy(&mips_cnt, blk+da+8, 4);
                size_t sp = da + 4 + mips_off;
                out.compressed_mip_sizes.reserve(mips_cnt);
                for (uint32_t mi = 0; mi < mips_cnt && sp+4 <= sz; ++mi, sp+=4) {
                    int32_t s; std::memcpy(&s, blk+sp, 4);
                    out.compressed_mip_sizes.push_back(s);
                }
            }
        }
    }
    return out.width > 0 && out.height > 0;
}

static DXGI_FORMAT vtex_to_dxgi(VTexFmt f) {
    switch (f) {
    case VTexFmt::DXT1:           return DXGI_FORMAT_BC1_UNORM;
    case VTexFmt::DXT5:           return DXGI_FORMAT_BC3_UNORM;
    case VTexFmt::ATI1N:          return DXGI_FORMAT_BC4_UNORM;
    case VTexFmt::ATI2N:          return DXGI_FORMAT_BC5_UNORM;
    case VTexFmt::BC6H:           return DXGI_FORMAT_BC6H_UF16;
    case VTexFmt::BC7:            return DXGI_FORMAT_BC7_UNORM;
    case VTexFmt::RGBA8888:       return DXGI_FORMAT_R8G8B8A8_UNORM;
    case VTexFmt::BGRA8888:       return DXGI_FORMAT_B8G8R8A8_UNORM;
    case VTexFmt::R16:            return DXGI_FORMAT_R16_UNORM;
    case VTexFmt::RG1616:         return DXGI_FORMAT_R16G16_UNORM;
    case VTexFmt::RGBA16161616:   return DXGI_FORMAT_R16G16B16A16_UNORM;
    case VTexFmt::R16F:           return DXGI_FORMAT_R16_FLOAT;
    case VTexFmt::RG1616F:        return DXGI_FORMAT_R16G16_FLOAT;
    case VTexFmt::RGBA16161616F:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case VTexFmt::R32F:           return DXGI_FORMAT_R32_FLOAT;
    case VTexFmt::RG3232F:        return DXGI_FORMAT_R32G32_FLOAT;
    case VTexFmt::RGB323232F:     return DXGI_FORMAT_R32G32B32_FLOAT;
    case VTexFmt::RGBA32323232F:  return DXGI_FORMAT_R32G32B32A32_FLOAT;
    case VTexFmt::I8:             return DXGI_FORMAT_R8_UNORM;
    case VTexFmt::IA88:           return DXGI_FORMAT_R8G8_UNORM;
    default:                      return DXGI_FORMAT_UNKNOWN;
    }
}

static UINT calc_row_pitch(int w, VTexFmt fmt) {
    if (is_block_compressed(fmt)) {
        const int bw = std::max(1, (w + 3) / 4);
        return (UINT)(bw * block_size(fmt));
    }
    return (UINT)(w * block_size(fmt));
}

static ID3D11ShaderResourceView* make_srv(ID3D11Device* dev,
                                           const uint8_t* pixel_data,
                                           int w, int h, VTexFmt fmt) {
    if (!dev || !pixel_data || w <= 0 || h <= 0) return nullptr;

    DXGI_FORMAT dxgi_fmt = vtex_to_dxgi(fmt);
    if (dxgi_fmt == DXGI_FORMAT_UNKNOWN) {
        set_error("No DXGI mapping for format " + std::string(vtex_fmt_name(fmt)));
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width            = w;
    td.Height           = h;
    td.MipLevels        = 1;
    td.ArraySize        = 1;
    td.Format           = dxgi_fmt;
    td.SampleDesc.Count = 1;
    td.Usage            = D3D11_USAGE_IMMUTABLE;
    td.BindFlags        = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init{};
    init.pSysMem     = pixel_data;
    init.SysMemPitch = calc_row_pitch(w, fmt);

    ID3D11Texture2D* tex = nullptr;
    HRESULT hr = dev->CreateTexture2D(&td, &init, &tex);
    if (FAILED(hr)) {
        set_error("CreateTexture2D failed (HRESULT=0x" +
                  std::to_string((unsigned long)hr) + ") fmt=" +
                  vtex_fmt_name(fmt) + " " + std::to_string(w) + "x" + std::to_string(h));
        return nullptr;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format                    = dxgi_fmt;
    sd.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels       = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    hr = dev->CreateShaderResourceView(tex, &sd, &srv);
    tex->Release();
    if (FAILED(hr)) {
        set_error("CreateSRV failed (HRESULT=0x" +
                  std::to_string((unsigned long)hr) + ")");
        return nullptr;
    }
    return srv;
}

static ID3D11ShaderResourceView* make_srv_rgba(ID3D11Device* dev,
                                                const uint8_t* rgba,
                                                int w, int h) {
    if (!dev || !rgba || w <= 0 || h <= 0) return nullptr;
    D3D11_TEXTURE2D_DESC td{};
    td.Width=w; td.Height=h; td.MipLevels=1; td.ArraySize=1;
    td.Format=DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count=1;
    td.Usage=D3D11_USAGE_IMMUTABLE; td.BindFlags=D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA init{rgba, (UINT)(w*4), 0};
    ID3D11Texture2D* tex=nullptr;
    if (FAILED(dev->CreateTexture2D(&td, &init, &tex))) return nullptr;
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    sd.Texture2D.MipLevels = 1;
    ID3D11ShaderResourceView* srv = nullptr;
    HRESULT hr = dev->CreateShaderResourceView(tex, &sd, &srv);
    tex->Release();
    return FAILED(hr) ? nullptr : srv;
}

static vpk::VPKDir& get_vpk() {
    static vpk::VPKDir dir;
    static bool tried = false;
    if (!tried) {
        tried = true;
        for (const auto& p : vpk::cs2_default_vpk_paths())
            if (dir.open(p)) break;
    }
    return dir;
}

struct DecodeResult {
    std::vector<uint8_t> pixels;
    int width  = 0;
    int height = 0;
    VTexFmt format = VTexFmt::UNKNOWN;
    bool is_decoded_rgba = false; 
};

static DecodeResult decode(const std::vector<uint8_t>& file) {
    DecodeResult result;
    const uint8_t* data = file.data();
    const size_t   sz   = file.size();

    ResHeader hdr;
    if (!parse_res_header(data, sz, hdr)) {
        set_error("Failed to parse resource header");
        return result;
    }

    const ResBlock* data_blk = find_block(hdr, "DATA");
    if (!data_blk || (size_t)data_blk->offset + data_blk->size > sz) {
        set_error("DATA block not found or truncated");
        return result;
    }

    VtexInfo vi;
    if (!parse_vtex_data(data + data_blk->offset, data_blk->size, vi)) {
        set_error("Failed to parse VTEX DATA block");
        return result;
    }

    if (vi.version != 1) {
        set_error("Unknown vtex version: " + std::to_string(vi.version));
        return result;
    }

    const auto fmt = static_cast<VTexFmt>(vi.format);
    result.format = fmt;

    const int actual_w = (vi.non_pow2_w > 0) ? vi.non_pow2_w : vi.width;
    const int actual_h = (vi.non_pow2_h > 0) ? vi.non_pow2_h : vi.height;
    
    const int W = vi.width;
    const int H = vi.height;

    const size_t data_offset = (size_t)data_blk->offset + data_blk->size;
    if (data_offset >= sz) {
        set_error("No pixel data after DATA block");
        return result;
    }

    if (is_raw_image(fmt)) {
        int sw, sh, sch;
        uint8_t* pix = stbi_load_from_memory(data + data_offset, (int)(sz - data_offset), &sw, &sh, &sch, 4);
        if (pix) {
            result.width  = sw;
            result.height = sh;
            result.pixels.assign(pix, pix + (size_t)sw * sh * 4);
            result.is_decoded_rgba = true;
            stbi_image_free(pix);
            return result;
        }
        set_error("stbi_load failed for raw image format " + std::string(vtex_fmt_name(fmt)));
        return result;
    }

    result.width  = actual_w;
    result.height = actual_h;

    size_t offset = data_offset;
    if (vi.num_mips > 1) {
        for (int mip = (int)vi.num_mips - 1; mip > 0; --mip) {
            const int mw = mip_level_size(W, mip);
            const int mh = mip_level_size(H, mip);
            int md = (vi.flags & VTEX_FLAG_VOLUME_TEXTURE)
                   ? mip_level_size(vi.depth, mip) : vi.depth;
            if (vi.flags & VTEX_FLAG_CUBE_TEXTURE) {
                md *= 6;
            }

            size_t mip_size = calc_mip_buffer_size(mw, mh, md, fmt);

            if (!vi.compressed_mip_sizes.empty() && (size_t)mip < vi.compressed_mip_sizes.size()) {
                const int32_t compressed_size = vi.compressed_mip_sizes[(size_t)mip];
                if (compressed_size > 0 && mip_size > (size_t)compressed_size) {
                    mip_size = (size_t)compressed_size;
                }
            }

            offset += mip_size;
        }
    }

    if (offset >= sz) {
        set_error("Pixel offset past end of file after skipping mips (offset=" +
                  std::to_string(offset) + " sz=" + std::to_string(sz) + ")");
        return result;
    }

    const int depth0 = (vi.flags & VTEX_FLAG_VOLUME_TEXTURE) ? vi.depth : vi.depth;
    const int total_depth = (vi.flags & VTEX_FLAG_CUBE_TEXTURE) ? depth0 * 6 : depth0;
    const size_t expected = calc_mip_buffer_size(W, H, total_depth, fmt);
    if (expected == 0) {
        set_error("Mip 0 buffer size is 0 for " + std::string(vtex_fmt_name(fmt)) +
                  " " + std::to_string(W) + "x" + std::to_string(H));
        return result;
    }

    const size_t avail = sz - offset;

    size_t comp_sz = 0;
    if (vi.is_actually_compressed && !vi.compressed_mip_sizes.empty()) {
        if (vi.compressed_mip_sizes[0] > 0)
            comp_sz = (size_t)vi.compressed_mip_sizes[0];
    }

    if (comp_sz > 0 && comp_sz < expected) {
        if (comp_sz > avail) {
            set_error("Compressed mip data truncated (need " + std::to_string(comp_sz) +
                      " have " + std::to_string(avail) + ")");
            return result;
        }
        result.pixels.resize(expected);
        if (!lz4_decompress(data + offset, comp_sz, result.pixels.data(), expected)) {
            const size_t r = ZSTD_decompress(result.pixels.data(), expected, data + offset, comp_sz);
            if (ZSTD_isError(r)) {
                set_error("Decompression failed (LZ4 and ZSTD) for mip 0, comp_sz=" +
                          std::to_string(comp_sz) + " expected=" + std::to_string(expected));
                result.pixels.clear();
                return result;
            }
        }
    } else {
        if (avail < expected) {
            set_error("Not enough pixel data (need " + std::to_string(expected) +
                      " have " + std::to_string(avail) + ") fmt=" +
                      std::string(vtex_fmt_name(fmt)) + " " +
                      std::to_string(W) + "x" + std::to_string(H) +
                      " mips=" + std::to_string(vi.num_mips));
            return result;
        }
        result.pixels.assign(data + offset, data + offset + expected);
    }

    if (vi.flags & VTEX_FLAG_CUBE_TEXTURE) {
        const size_t face_size = calc_mip_buffer_size(W, H, 1, fmt);
        if (result.pixels.size() >= face_size) {
            result.pixels.resize(face_size);
        }
    }

    if (vi.depth > 1 && !(vi.flags & VTEX_FLAG_CUBE_TEXTURE)) {
        const size_t slice_size = calc_mip_buffer_size(W, H, 1, fmt);
        if (result.pixels.size() >= slice_size) {
            result.pixels.resize(slice_size);
        }
    }

    return result;
}

std::vector<Entry> list_all(const std::string& filter) {
    std::vector<Entry> result;
    const auto paths = get_vpk().list_files("", ".vtex_c");
    result.reserve(paths.size());
    for (const auto& path : paths) {
        if (!filter.empty() && path.find(filter) == std::string::npos) continue;
        Entry e;
        e.path = path;
        const auto slash = path.rfind('/');
        e.name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
        result.push_back(std::move(e));
    }
    return result;
}

static LoadResult do_load(ID3D11Device* device, const std::vector<uint8_t>& file_data) {
    clear_error();

    if (!device) { set_error("D3D11 device is null"); return {}; }
    if (file_data.empty()) { set_error("File data is empty"); return {}; }

    auto dr = decode(file_data);
    if (dr.pixels.empty()) return {}; 

    ID3D11ShaderResourceView* srv = nullptr;
    if (dr.is_decoded_rgba) {
        srv = make_srv_rgba(device, dr.pixels.data(), dr.width, dr.height);
        if (!srv) set_error("Failed to create SRV from decoded RGBA data");
    } else {
        srv = make_srv(device, dr.pixels.data(), dr.width, dr.height, dr.format);
        
    }

    if (!srv) return {};
    return { srv, (float)dr.width / (float)dr.height, dr.width, dr.height };
}

LoadResult load(ID3D11Device* device, const std::string& path) {
    clear_error();
    if (path.empty()) { set_error("Empty path"); return {}; }
    const auto bytes = get_vpk().read_file(path);
    if (!bytes || bytes->empty()) {
        set_error("VPK read failed: " + path);
        return {};
    }
    return do_load(device, *bytes);
}

LoadResult load(ID3D11Device* device, const std::vector<uint8_t>& file_data) {
    return do_load(device, file_data);
}

void release(ID3D11ShaderResourceView* srv) {
    if (srv) srv->Release();
}

}
