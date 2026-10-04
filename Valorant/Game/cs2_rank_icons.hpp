#pragma once

struct ID3D11Device;
struct ID3D11ShaderResourceView;

void ExpectionalRankIconsShutdown();

void ExpectionalRankIconsFrame(ID3D11Device* device);

void* ExpectionalRankIconTexture(bool wingman, int sub);
