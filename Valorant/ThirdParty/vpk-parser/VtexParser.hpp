#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <d3d11.h>
#include <string>
#include <vector>

namespace vtex {

struct Entry {
    std::string path;
    std::string name;
};

struct LoadResult {
    ID3D11ShaderResourceView* srv    = nullptr;
    float                     aspect = 1.0f;
    int                       width  = 0;
    int                       height = 0;
};

std::vector<Entry> list_all(const std::string& filter = "");
LoadResult         load(ID3D11Device* device, const std::string& path);
LoadResult         load(ID3D11Device* device, const std::vector<uint8_t>& file_data);
void               release(ID3D11ShaderResourceView* srv);

const std::string& last_error();

}
