#include "weapon_knife_icons.hpp"
#include "weapon_knife_png.inl"

#include "stb_image.h"

#include "../AnanbabanOverlay/expectional_ananbaban_overlay.hpp"

#include <d3d11.h>
#include <cstring>
#include <vector>

namespace {

struct KnifeSlot {
	const char* key = nullptr;
	ID3D11ShaderResourceView* srv = nullptr;
	int w = 0;
	int h = 0;
	bool failed = false;
};

std::vector<KnifeSlot> g_slots;
ID3D11Device* g_device = nullptr;

static void ReleaseSlots()
{
	for (KnifeSlot& s : g_slots) {
		if (s.srv) {
			s.srv->Release();
			s.srv = nullptr;
		}
	}
	g_slots.clear();
}

static ID3D11ShaderResourceView* CreateSrv(ID3D11Device* device, const unsigned char* png, unsigned png_size, int* out_w, int* out_h)
{
	if (out_w)
		*out_w = 0;
	if (out_h)
		*out_h = 0;
	int w = 0, h = 0, n = 0;
	unsigned char* px = stbi_load_from_memory(png, static_cast<int>(png_size), &w, &h, &n, 4);
	if (!px || w <= 0 || h <= 0) {
		if (px)
			stbi_image_free(px);
		return nullptr;
	}

	D3D11_TEXTURE2D_DESC desc{};
	desc.Width = static_cast<UINT>(w);
	desc.Height = static_cast<UINT>(h);
	desc.MipLevels = 1;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

	D3D11_SUBRESOURCE_DATA sub{};
	sub.pSysMem = px;
	sub.SysMemPitch = static_cast<UINT>(w * 4);

	ID3D11Texture2D* tex = nullptr;
	HRESULT hr = device->CreateTexture2D(&desc, &sub, &tex);
	stbi_image_free(px);
	if (FAILED(hr) || !tex)
		return nullptr;

	D3D11_SHADER_RESOURCE_VIEW_DESC srvd{};
	srvd.Format = desc.Format;
	srvd.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvd.Texture2D.MipLevels = 1;
	ID3D11ShaderResourceView* srv = nullptr;
	hr = device->CreateShaderResourceView(tex, &srvd, &srv);
	tex->Release();
	if (FAILED(hr) || !srv)
		return nullptr;
	if (out_w)
		*out_w = w;
	if (out_h)
		*out_h = h;
	return srv;
}

static void EnsureSlots(ID3D11Device* device)
{
	if (!device)
		return;
	if (g_device != device) {
		ReleaseSlots();
		g_device = device;
	}
	if (!g_slots.empty())
		return;
	g_slots.resize(kKnifePngCount);
	for (unsigned i = 0; i < kKnifePngCount; ++i) {
		KnifeSlot& s = g_slots[i];
		s.key = kKnifePngs[i].key;
		s.srv = CreateSrv(device, kKnifePngs[i].data, kKnifePngs[i].size, &s.w, &s.h);
		s.failed = s.srv == nullptr;
	}
}

} 

bool ExpectionalKnifeIcon(const char* key, void** out_tex, int* out_w, int* out_h)
{
	if (out_tex)
		*out_tex = nullptr;
	if (out_w)
		*out_w = 0;
	if (out_h)
		*out_h = 0;
	if (!key || !key[0] || !g_ExpectionalMainDX11Device)
		return false;
	EnsureSlots(g_ExpectionalMainDX11Device);
	for (const KnifeSlot& s : g_slots) {
		if (!s.key || std::strcmp(s.key, key) != 0)
			continue;
		if (!s.srv)
			return false;
		if (out_tex)
			*out_tex = s.srv;
		if (out_w)
			*out_w = s.w;
		if (out_h)
			*out_h = s.h;
		return true;
	}
	return false;
}
