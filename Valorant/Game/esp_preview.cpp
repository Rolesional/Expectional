#include "esp_preview.hpp"
#include "esp_layout.hpp"
#include "../offsets_embed_resource.h"

#include <Windows.h>

#include "catalyst_player_esp.hpp"
#include "esp_extras.hpp"
#include "globals.hpp"
#include "../AnanbabanOverlay/expectional_ananbaban_overlay.hpp"
#include "../AnanbabanOverlay/expectional_overlay_window.hpp"
#include "../OSImGui/shade_imgui_settings.h"
#include "../../Includes/Imgui/imgui_internal.h"

#include <DirectXMath.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <wincodec.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "windowscodecs.lib")

using namespace DirectX;

namespace {

constexpr float kPreviewW = 340.f;

struct Vtx {
	float px, py, pz;
	float nx, ny, nz;
	float u, v;
};

struct ViewRec {
	uint32_t off = 0;
	uint32_t len = 0;
	uint32_t stride = 0;
};

struct AccRec {
	int view = -1;
	int comp = 0;
	int count = 0;
	bool normalized = false;
	char type[8] = {};
};

struct MeshGpu {
	ID3D11Buffer* vb = nullptr;
	ID3D11Buffer* ib = nullptr;
	ID3D11ShaderResourceView* albedo = nullptr;
	UINT indexCount = 0;
	float minP[3] = {};
	float maxP[3] = {};
	bool ready = false;
};

struct Pipe {
	ID3D11VertexShader* vs = nullptr;
	ID3D11PixelShader* ps = nullptr;
	ID3D11InputLayout* layout = nullptr;
	ID3D11Buffer* cb = nullptr;
	ID3D11SamplerState* samp = nullptr;
	ID3D11RasterizerState* rs = nullptr;
	ID3D11DepthStencilState* ds = nullptr;
	ID3D11BlendState* blend = nullptr;
	ID3D11Texture2D* color = nullptr;
	ID3D11RenderTargetView* rtv = nullptr;
	ID3D11ShaderResourceView* srv = nullptr;
	ID3D11Texture2D* depth = nullptr;
	ID3D11DepthStencilView* dsv = nullptr;
	int rtW = 0;
	int rtH = 0;
};

MeshGpu g_mesh;
Pipe g_pipe;
ID3D11Device* g_dev = nullptr;
std::string g_status;
bool g_loadTried = false;

float g_yaw = 0.35f;
float g_pitch = 0.08f;
constexpr float kPitchHome = 0.08f;
float g_yawAtDrag = 0.35f;
float g_dist = 4.75f;
float g_panX = 0.f;
float g_panY = 0.f;
bool g_dragRot = false;
bool g_dragPan = false;
bool g_easeHome = false;

static const char* kHlsl = R"HLSL(
cbuffer CB : register(b0) {
    row_major float4x4 mvp;
    row_major float4x4 world;
    float4 camPos;
    float4 lightDir;
    float4 misc;
};
Texture2D albedo : register(t0);
SamplerState samp : register(s0);
struct VSIn {
    float3 pos : POSITION;
    float3 nrm : NORMAL;
    float2 uv : TEXCOORD;
};
struct VSOut {
    float4 clip : SV_Position;
    float3 nrm : TEXCOORD0;
    float3 wpos : TEXCOORD1;
    float2 uv : TEXCOORD2;
};
VSOut vs_main(VSIn i) {
    VSOut o;
    float3 wp = mul(float4(i.pos, 1), world).xyz;
    o.clip = mul(float4(i.pos, 1), mvp);
    o.nrm = mul(float4(i.nrm, 0), world).xyz;
    o.wpos = wp;
    o.uv = i.uv;
    return o;
}
float4 ps_main(VSOut i) : SV_Target {
    float3 n = normalize(i.nrm);
    float3 v = normalize(camPos.xyz - i.wpos);
    if (dot(n, v) < 0) n = -n;
    float3 l = normalize(lightDir.xyz);
    float ndl = saturate(dot(n, l));
    float3 base = float3(0.62, 0.58, 0.55);
    if (misc.x > 0.5) base = albedo.Sample(samp, i.uv).rgb;
    float wrap = ndl * 0.65 + 0.35;
    float rim = pow(saturate(1.0 - dot(n, v)), 2.2) * 0.18;
    return float4(base * wrap + rim, 1);
}
)HLSL";

template <typename T>
void Rel(T*& p)
{
	if (p) {
		p->Release();
		p = nullptr;
	}
}

void ReleaseMesh()
{
	Rel(g_mesh.vb);
	Rel(g_mesh.ib);
	Rel(g_mesh.albedo);
	g_mesh.indexCount = 0;
	g_mesh.ready = false;
}

void ReleasePipe()
{
	Rel(g_pipe.vs);
	Rel(g_pipe.ps);
	Rel(g_pipe.layout);
	Rel(g_pipe.cb);
	Rel(g_pipe.samp);
	Rel(g_pipe.rs);
	Rel(g_pipe.ds);
	Rel(g_pipe.blend);
	Rel(g_pipe.rtv);
	Rel(g_pipe.srv);
	Rel(g_pipe.color);
	Rel(g_pipe.dsv);
	Rel(g_pipe.depth);
	g_pipe.rtW = 0;
	g_pipe.rtH = 0;
}

bool LoadPreviewBytes(std::vector<uint8_t>& out)
{
	HMODULE mod = GetModuleHandleW(nullptr);
	HRSRC res = FindResourceW(mod, MAKEINTRESOURCEW(IDR_PLAYER_PREVIEW_GLB), RT_RCDATA);
	if (!res)
		return false;
	HGLOBAL hg = LoadResource(mod, res);
	if (!hg)
		return false;
	const DWORD n = SizeofResource(mod, res);
	const void* p = LockResource(hg);
	if (!p || n < 32 || n > 80u * 1024u * 1024u)
		return false;
	const auto* b = static_cast<const uint8_t*>(p);
	out.assign(b, b + n);
	return true;
}

int FindInt(const std::string& s, const char* key, int def = -1)
{
	const std::string pat = std::string("\"") + key + "\":";
	const size_t p = s.find(pat);
	if (p == std::string::npos)
		return def;
	return atoi(s.c_str() + p + pat.size());
}

bool ParseFloat3(const std::string& s, size_t from, size_t to, const char* key, float o[3])
{
	const std::string pat = std::string("\"") + key + "\":[";
	const size_t p = s.find(pat, from);
	if (p == std::string::npos || p >= to)
		return false;
	return sscanf_s(s.c_str() + p + pat.size(), "%f,%f,%f", &o[0], &o[1], &o[2]) == 3;
}

std::vector<std::pair<size_t, size_t>> ArrayObjects(const std::string& s, const char* key)
{
	std::vector<std::pair<size_t, size_t>> out;
	const std::string pat = std::string("\"") + key + "\":";
	size_t p = s.find(pat);
	if (p == std::string::npos)
		return out;
	p = s.find('[', p + pat.size());
	if (p == std::string::npos)
		return out;
	int arr = 1;
	int depth = 0;
	size_t start = 0;
	for (size_t i = p + 1; i < s.size(); ++i) {
		const char c = s[i];
		if (c == '[')
			++arr;
		else if (c == ']') {
			--arr;
			if (arr == 0)
				break;
		} else if (c == '{') {
			if (arr == 1 && depth == 0)
				start = i;
			++depth;
		} else if (c == '}') {
			--depth;
			if (arr == 1 && depth == 0 && start)
				out.emplace_back(start, i + 1);
		}
	}
	return out;
}

int FindIntRange(const std::string& s, size_t from, size_t to, const char* key, int def)
{
	const std::string pat = std::string("\"") + key + "\":";
	const size_t p = s.find(pat, from);
	if (p == std::string::npos || p >= to)
		return def;
	return atoi(s.c_str() + p + pat.size());
}

bool DecodeWebpRgba(const uint8_t* bytes, uint32_t size, std::vector<uint8_t>& rgba, UINT& w, UINT& h)
{
	rgba.clear();
	w = h = 0;
	if (!bytes || !size)
		return false;
	const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool coOk = SUCCEEDED(co) || co == RPC_E_CHANGED_MODE;
	if (!coOk)
		return false;

	IWICImagingFactory* factory = nullptr;
	IWICStream* stream = nullptr;
	IWICBitmapDecoder* decoder = nullptr;
	IWICBitmapFrameDecode* frame = nullptr;
	IWICFormatConverter* conv = nullptr;
	bool ok = false;
	do {
		if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
			break;
		if (FAILED(factory->CreateStream(&stream)))
			break;
		if (FAILED(stream->InitializeFromMemory(const_cast<BYTE*>(bytes), size)))
			break;
		if (FAILED(factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder)))
			break;
		if (FAILED(decoder->GetFrame(0, &frame)))
			break;
		if (FAILED(factory->CreateFormatConverter(&conv)))
			break;
		if (FAILED(conv->Initialize(frame, GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0.f, WICBitmapPaletteTypeCustom)))
			break;
		if (FAILED(conv->GetSize(&w, &h)) || !w || !h || w > 8192 || h > 8192)
			break;
		rgba.resize(static_cast<size_t>(w) * h * 4u);
		const UINT stride = w * 4u;
		if (FAILED(conv->CopyPixels(nullptr, stride, static_cast<UINT>(rgba.size()), rgba.data())))
			break;
		ok = true;
	} while (false);
	Rel(conv);
	Rel(frame);
	Rel(decoder);
	Rel(stream);
	Rel(factory);
	if (SUCCEEDED(co))
		CoUninitialize();
	if (!ok)
		rgba.clear();
	return ok;
}

bool CreateAlbedo(ID3D11Device* device, const uint8_t* bytes, uint32_t size)
{
	std::vector<uint8_t> rgba;
	UINT w = 0, h = 0;
	if (!DecodeWebpRgba(bytes, size, rgba, w, h))
		return false;
	D3D11_TEXTURE2D_DESC td{};
	td.Width = w;
	td.Height = h;
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	D3D11_SUBRESOURCE_DATA sd{};
	sd.pSysMem = rgba.data();
	sd.SysMemPitch = w * 4u;
	ID3D11Texture2D* tex = nullptr;
	if (FAILED(device->CreateTexture2D(&td, &sd, &tex)))
		return false;
	const HRESULT hr = device->CreateShaderResourceView(tex, nullptr, &g_mesh.albedo);
	tex->Release();
	return SUCCEEDED(hr);
}

bool BuildMesh(ID3D11Device* device, const std::vector<uint8_t>& file)
{
	if (file.size() < 20 || memcmp(file.data(), "glTF", 4) != 0)
		return false;
	const uint32_t jsonLen = *reinterpret_cast<const uint32_t*>(file.data() + 12);
	const uint32_t jsonType = *reinterpret_cast<const uint32_t*>(file.data() + 16);
	if (jsonType != 0x4E4F534Au || 20u + jsonLen + 8u > file.size())
		return false;
	const uint32_t binLen = *reinterpret_cast<const uint32_t*>(file.data() + 20 + jsonLen);
	const uint32_t binType = *reinterpret_cast<const uint32_t*>(file.data() + 24 + jsonLen);
	const size_t binOff = 28u + jsonLen;
	if (binType != 0x004E4942u || binOff + binLen > file.size())
		return false;
	const uint8_t* bin = file.data() + binOff;
	const std::string json(reinterpret_cast<const char*>(file.data() + 20), jsonLen);

	const auto viewObjs = ArrayObjects(json, "bufferViews");
	const auto accObjs = ArrayObjects(json, "accessors");
	std::vector<ViewRec> views;
	views.reserve(viewObjs.size());
	for (const auto& r : viewObjs) {
		ViewRec v;
		v.off = static_cast<uint32_t>(FindIntRange(json, r.first, r.second, "byteOffset", 0));
		v.len = static_cast<uint32_t>(FindIntRange(json, r.first, r.second, "byteLength", 0));
		v.stride = static_cast<uint32_t>(FindIntRange(json, r.first, r.second, "byteStride", 0));
		views.push_back(v);
	}
	std::vector<AccRec> accs;
	accs.reserve(accObjs.size());
	for (const auto& r : accObjs) {
		AccRec a;
		a.view = FindIntRange(json, r.first, r.second, "bufferView", -1);
		a.comp = FindIntRange(json, r.first, r.second, "componentType", 0);
		a.count = FindIntRange(json, r.first, r.second, "count", 0);
		a.normalized = json.find("\"normalized\":true", r.first) != std::string::npos
			&& json.find("\"normalized\":true", r.first) < r.second;
		const size_t tp = json.find("\"type\":\"", r.first);
		if (tp != std::string::npos && tp < r.second) {
			const size_t ts = tp + 8;
			const size_t te = json.find('"', ts);
			if (te != std::string::npos && te - ts < sizeof(a.type))
				memcpy(a.type, json.c_str() + ts, te - ts);
		}
		accs.push_back(a);
	}

	const int iPos = FindInt(json, "POSITION", 0);
	const int iNrm = FindInt(json, "NORMAL", 1);
	const int iUv = FindInt(json, "TEXCOORD_0", 2);
	const int iIdx = FindInt(json, "indices", 3);
	if (iPos < 0 || iNrm < 0 || iIdx < 0 || iPos >= (int)accs.size() || iNrm >= (int)accs.size() || iIdx >= (int)accs.size())
		return false;

	float nodeScale[3] = { 1.f, 1.f, 1.f };
	float nodeTrans[3] = { 0.f, 0.f, 0.f };
	const auto nodes = ArrayObjects(json, "nodes");
	if (!nodes.empty()) {
		ParseFloat3(json, nodes[0].first, nodes[0].second, "scale", nodeScale);
		ParseFloat3(json, nodes[0].first, nodes[0].second, "translation", nodeTrans);
	}
	float uvScale[2] = { 1.f, 1.f };
	const size_t baseTex = json.find("\"baseColorTexture\"");
	if (baseTex != std::string::npos) {
		const std::string pat = "\"scale\":[";
		const size_t sp = json.find(pat, baseTex);
		if (sp != std::string::npos) {
			float su = 1.f, sv = 1.f;
			if (sscanf_s(json.c_str() + sp + pat.size(), "%f,%f", &su, &sv) == 2) {
				uvScale[0] = su;
				uvScale[1] = sv;
			}
		}
	}

	auto spanOk = [&](int accIndex, uint32_t elem) -> const uint8_t* {
		if (accIndex < 0 || accIndex >= (int)accs.size())
			return nullptr;
		const AccRec& a = accs[accIndex];
		if (a.view < 0 || a.view >= (int)views.size() || a.count <= 0)
			return nullptr;
		const ViewRec& v = views[a.view];
		const uint64_t need = static_cast<uint64_t>(v.off) + static_cast<uint64_t>(a.count) * elem;
		if (need > binLen)
			return nullptr;
		return bin + v.off;
	};

	const AccRec& ap = accs[iPos];
	const AccRec& an = accs[iNrm];
	const AccRec& ai = accs[iIdx];
	if (ap.count <= 0 || ap.count > 2000000 || ai.count < 3 || ai.count > 8000000)
		return false;
	const uint32_t posStride = (ap.view >= 0 && views[ap.view].stride) ? views[ap.view].stride : 12u;
	const uint32_t nrmStride = (an.view >= 0 && views[an.view].stride) ? views[an.view].stride : 4u;
	const uint8_t* posBytes = spanOk(iPos, posStride);
	const uint8_t* nrmBytes = spanOk(iNrm, nrmStride);
	const uint8_t* idxBytes = spanOk(iIdx, 4u);
	if (!posBytes || !nrmBytes || !idxBytes || ap.comp != 5126 || an.comp != 5120 || ai.comp != 5125)
		return false;

	const bool hasUv = iUv >= 0 && iUv < (int)accs.size() && accs[iUv].count == ap.count && accs[iUv].comp == 5123;
	uint32_t uvStride = 4u;
	const uint8_t* uvBytes = nullptr;
	if (hasUv) {
		uvStride = (accs[iUv].view >= 0 && views[accs[iUv].view].stride) ? views[accs[iUv].view].stride : 4u;
		uvBytes = spanOk(iUv, uvStride);
	}

	std::vector<Vtx> verts(static_cast<size_t>(ap.count));
	float minP[3] = { 1e9f, 1e9f, 1e9f };
	float maxP[3] = { -1e9f, -1e9f, -1e9f };
	for (int i = 0; i < ap.count; ++i) {
		float p[3];
		memcpy(p, posBytes + static_cast<size_t>(i) * posStride, sizeof(p));
		Vtx& v = verts[static_cast<size_t>(i)];
		v.px = p[0] * nodeScale[0] + nodeTrans[0];
		v.py = p[1] * nodeScale[1] + nodeTrans[1];
		v.pz = p[2] * nodeScale[2] + nodeTrans[2];
		minP[0] = (std::min)(minP[0], v.px);
		minP[1] = (std::min)(minP[1], v.py);
		minP[2] = (std::min)(minP[2], v.pz);
		maxP[0] = (std::max)(maxP[0], v.px);
		maxP[1] = (std::max)(maxP[1], v.py);
		maxP[2] = (std::max)(maxP[2], v.pz);
		const uint8_t* nb = nrmBytes + static_cast<size_t>(i) * nrmStride;
		const float nx = static_cast<float>(static_cast<int8_t>(nb[0])) / 127.f;
		const float ny = static_cast<float>(static_cast<int8_t>(nb[1])) / 127.f;
		const float nz = static_cast<float>(static_cast<int8_t>(nb[2])) / 127.f;
		const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
		v.nx = nl > 1e-5f ? nx / nl : 0.f;
		v.ny = nl > 1e-5f ? ny / nl : 1.f;
		v.nz = nl > 1e-5f ? nz / nl : 0.f;
		v.u = 0.f;
		v.v = 0.f;
		if (uvBytes) {
			uint16_t uv[2];
			memcpy(uv, uvBytes + static_cast<size_t>(i) * uvStride, sizeof(uv));
			v.u = (uv[0] / 65535.f) * uvScale[0];
			v.v = (uv[1] / 65535.f) * uvScale[1];
		}
	}
	const float cx = (minP[0] + maxP[0]) * 0.5f;
	const float cy = (minP[1] + maxP[1]) * 0.5f;
	const float cz = (minP[2] + maxP[2]) * 0.5f;
	for (Vtx& v : verts) {
		v.px -= cx;
		v.py -= cy;
		v.pz -= cz;
	}
	g_mesh.minP[0] = minP[0] - cx;
	g_mesh.minP[1] = minP[1] - cy;
	g_mesh.minP[2] = minP[2] - cz;
	g_mesh.maxP[0] = maxP[0] - cx;
	g_mesh.maxP[1] = maxP[1] - cy;
	g_mesh.maxP[2] = maxP[2] - cz;
	const float spanY = g_mesh.maxP[1] - g_mesh.minP[1];
	if (spanY > 0.05f) {
		const float fit = 1.90f / spanY;
		for (Vtx& v : verts) {
			v.px *= fit;
			v.py *= fit;
			v.pz *= fit;
		}
		for (int ax = 0; ax < 3; ++ax) {
			g_mesh.minP[ax] *= fit;
			g_mesh.maxP[ax] *= fit;
		}
	}

	if (ai.count < 3)
		return false;

	D3D11_BUFFER_DESC bd{};
	bd.ByteWidth = static_cast<UINT>(verts.size() * sizeof(Vtx));
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	D3D11_SUBRESOURCE_DATA sd{};
	sd.pSysMem = verts.data();
	if (FAILED(device->CreateBuffer(&bd, &sd, &g_mesh.vb)))
		return false;
	bd.ByteWidth = static_cast<UINT>(ai.count) * 4u;
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	sd.pSysMem = idxBytes;
	if (FAILED(device->CreateBuffer(&bd, &sd, &g_mesh.ib))) {
		ReleaseMesh();
		return false;
	}
	g_mesh.indexCount = static_cast<UINT>(ai.count);

	const auto images = ArrayObjects(json, "images");
	if (!images.empty()) {
		const int bv = FindIntRange(json, images[0].first, images[0].second, "bufferView", -1);
		if (bv >= 0 && bv < (int)views.size()) {
			const ViewRec& im = views[bv];
			if (static_cast<uint64_t>(im.off) + im.len <= binLen && im.len > 32)
				CreateAlbedo(device, bin + im.off, im.len);
		}
	}
	return true;
}

bool EnsurePipeline(ID3D11Device* device)
{
	if (g_pipe.vs && g_pipe.ps && g_pipe.layout && g_pipe.cb)
		return true;
	ID3DBlob* vsb = nullptr;
	ID3DBlob* psb = nullptr;
	ID3DBlob* err = nullptr;
	const UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
	if (FAILED(D3DCompile(kHlsl, strlen(kHlsl), nullptr, nullptr, nullptr, "vs_main", "vs_4_0", flags, 0, &vsb, &err))) {
		if (err)
			g_status.assign(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize());
		Rel(err);
		Rel(vsb);
		return false;
	}
	Rel(err);
	if (FAILED(D3DCompile(kHlsl, strlen(kHlsl), nullptr, nullptr, nullptr, "ps_main", "ps_4_0", flags, 0, &psb, &err))) {
		if (err)
			g_status.assign(static_cast<const char*>(err->GetBufferPointer()), err->GetBufferSize());
		Rel(err);
		Rel(vsb);
		Rel(psb);
		return false;
	}
	Rel(err);
	if (FAILED(device->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &g_pipe.vs))) {
		Rel(vsb);
		Rel(psb);
		return false;
	}
	if (FAILED(device->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &g_pipe.ps))) {
		Rel(vsb);
		Rel(psb);
		return false;
	}
	const D3D11_INPUT_ELEMENT_DESC il[] = {
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	};
		const HRESULT ihr = device->CreateInputLayout(il, 3, vsb->GetBufferPointer(), vsb->GetBufferSize(), &g_pipe.layout);
		Rel(vsb);
		Rel(psb);
		if (FAILED(ihr))
			return false;

		D3D11_BUFFER_DESC cbd{};
		cbd.ByteWidth = 176;
		cbd.Usage = D3D11_USAGE_DYNAMIC;
		cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device->CreateBuffer(&cbd, nullptr, &g_pipe.cb)))
			return false;

		D3D11_SAMPLER_DESC sd{};
		sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(device->CreateSamplerState(&sd, &g_pipe.samp)))
			return false;

		D3D11_RASTERIZER_DESC rd{};
		rd.FillMode = D3D11_FILL_SOLID;
		rd.CullMode = D3D11_CULL_NONE;
		rd.DepthClipEnable = TRUE;
		if (FAILED(device->CreateRasterizerState(&rd, &g_pipe.rs)))
			return false;

		D3D11_DEPTH_STENCIL_DESC dd{};
		dd.DepthEnable = TRUE;
		dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
		dd.DepthFunc = D3D11_COMPARISON_LESS;
		if (FAILED(device->CreateDepthStencilState(&dd, &g_pipe.ds)))
			return false;

		D3D11_BLEND_DESC bd{};
		bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(device->CreateBlendState(&bd, &g_pipe.blend)))
			return false;
		return true;
	}

bool EnsureTarget(ID3D11Device* device, int w, int h)
{
	if (w < 8)
		w = 8;
	if (h < 8)
		h = 8;
	if (g_pipe.color && g_pipe.rtW == w && g_pipe.rtH == h && g_pipe.srv)
		return true;
	Rel(g_pipe.rtv);
	Rel(g_pipe.srv);
	Rel(g_pipe.color);
	Rel(g_pipe.dsv);
	Rel(g_pipe.depth);
	D3D11_TEXTURE2D_DESC td{};
	td.Width = static_cast<UINT>(w);
	td.Height = static_cast<UINT>(h);
	td.MipLevels = 1;
	td.ArraySize = 1;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1;
	td.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
	if (FAILED(device->CreateTexture2D(&td, nullptr, &g_pipe.color)))
		return false;
	if (FAILED(device->CreateRenderTargetView(g_pipe.color, nullptr, &g_pipe.rtv)))
		return false;
	if (FAILED(device->CreateShaderResourceView(g_pipe.color, nullptr, &g_pipe.srv)))
		return false;
	td.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	td.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	if (FAILED(device->CreateTexture2D(&td, nullptr, &g_pipe.depth)))
		return false;
	if (FAILED(device->CreateDepthStencilView(g_pipe.depth, nullptr, &g_pipe.dsv)))
		return false;
	g_pipe.rtW = w;
	g_pipe.rtH = h;
	return true;
}

void EnsureLoaded(ID3D11Device* device)
{
	if (g_dev != device) {
		ReleaseMesh();
		ReleasePipe();
		g_dev = device;
		g_loadTried = false;
		g_status.clear();
	}
	if (g_loadTried)
		return;
	g_loadTried = true;
	if (!EnsurePipeline(device)) {
		if (g_status.empty())
			g_status = "Preview shader failed.";
		return;
	}
	std::vector<uint8_t> file;
	if (!LoadPreviewBytes(file)) {
		g_status = "Preview model was not found.";
		return;
	}
	if (!BuildMesh(device, file)) {
		ReleaseMesh();
		g_status = "Preview model could not be read.";
		return;
	}
	g_mesh.ready = true;
	g_status.clear();
}

struct CBData {
	XMFLOAT4X4 mvp;
	XMFLOAT4X4 world;
	XMFLOAT4 cam;
	XMFLOAT4 light;
	XMFLOAT4 misc;
};

static_assert(sizeof(CBData) == 176, "preview cb size");

bool Project(const XMFLOAT4X4& m, float x, float y, float z, const ImVec2& origin, const ImVec2& size, ImVec2& out)
{
	const float ox = x * m._11 + y * m._21 + z * m._31 + m._41;
	const float oy = x * m._12 + y * m._22 + z * m._32 + m._42;
	const float ow = x * m._14 + y * m._24 + z * m._34 + m._44;
	if (ow < 0.02f)
		return false;
	const float inv = 1.f / ow;
	const float nx = ox * inv;
	const float ny = oy * inv;
	out.x = origin.x + (nx * 0.5f + 0.5f) * size.x;
	out.y = origin.y + (1.f - (ny * 0.5f + 0.5f)) * size.y;
	return true;
}

void BoneLocal(int id, float& x, float& y, float& z)
{
	x = 0.f;
	y = 0.f;
	z = 0.f;
	switch (id) {
	case 7: x = 0.03f; y = 0.84f; z = -0.05f; break;
	case 6: x = 0.02f; y = 0.70f; z = -0.04f; break;
	case 23: x = 0.01f; y = 0.58f; z = -0.03f; break;
	case 4: x = 0.01f; y = 0.40f; z = -0.02f; break;
	case 3: x = 0.01f; y = 0.18f; z = 0.00f; break;
	case 1: x = 0.00f; y = -0.02f; z = 0.00f; break;
	case 9: x = -0.20f; y = 0.58f; z = -0.06f; break;
	case 10: x = -0.26f; y = 0.34f; z = -0.04f; break;
	case 11: x = -0.18f; y = 0.08f; z = 0.05f; break;
	case 13: x = 0.20f; y = 0.58f; z = -0.08f; break;
	case 14: x = 0.26f; y = 0.34f; z = -0.08f; break;
	case 15: x = 0.16f; y = 0.08f; z = 0.02f; break;
	case 17: x = -0.12f; y = -0.10f; z = 0.00f; break;
	case 18: x = -0.16f; y = -0.50f; z = -0.02f; break;
	case 19: x = -0.17f; y = -0.90f; z = 0.01f; break;
	case 20: x = 0.10f; y = -0.10f; z = -0.02f; break;
	case 21: x = 0.10f; y = -0.50f; z = -0.04f; break;
	case 22: x = 0.10f; y = -0.90f; z = -0.06f; break;
	default: break;
	}
}

void DrawPreviewEsp(ImDrawList* dl, const ImVec2& origin, const ImVec2& size, const XMFLOAT4X4& mvp)
{
	if (!Settings::Visuals::enablePlayerEsp)
		return;
	const float hx = (g_mesh.maxP[0] - g_mesh.minP[0]) * 0.5f;
	const float hy = (g_mesh.maxP[1] - g_mesh.minP[1]) * 0.5f;
	if (hx < 0.01f || hy < 0.01f)
		return;

	ImVec2 corners[8];
	int nCorner = 0;
	float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
	for (int i = 0; i < 8; ++i) {
		const float x = (i & 1) ? g_mesh.maxP[0] : g_mesh.minP[0];
		const float y = (i & 2) ? g_mesh.maxP[1] : g_mesh.minP[1];
		const float z = (i & 4) ? g_mesh.maxP[2] : g_mesh.minP[2];
		ImVec2 sp;
		if (!Project(mvp, x, y, z, origin, size, sp))
			continue;
		corners[nCorner++] = sp;
		minX = (std::min)(minX, sp.x);
		minY = (std::min)(minY, sp.y);
		maxX = (std::max)(maxX, sp.x);
		maxY = (std::max)(maxY, sp.y);
	}
	if (nCorner < 2 || maxX - minX < 2.f || maxY - minY < 2.f)
		return;

	const float rx = std::floor(minX);
	const float ry = std::floor(minY);
	const float rw = std::floor(maxX - minX);
	const float rh = std::floor(maxY - minY);

	const ImU32 outlineIm = ImGui::ColorConvertFloat4ToU32(ImVec4(EspUiColors::espcol[0], EspUiColors::espcol[1], EspUiColors::espcol[2], 1.f));
	const ImU32 nameCol = ImGui::ColorConvertFloat4ToU32(ImVec4(EspUiColors::name_esp[0], EspUiColors::name_esp[1], EspUiColors::name_esp[2], 1.f));
	const ImU32 wpnCol = ImGui::ColorConvertFloat4ToU32(ImVec4(EspUiColors::weapon_esp[0], EspUiColors::weapon_esp[1], EspUiColors::weapon_esp[2], 1.f));
	const ImU32 distCol = ImGui::ColorConvertFloat4ToU32(ImVec4(EspUiColors::distance_esp[0], EspUiColors::distance_esp[1], EspUiColors::distance_esp[2], 1.f));
	const ImU32 skelCol = ImGui::ColorConvertFloat4ToU32(ImVec4(EspUiColors::skel_col[0], EspUiColors::skel_col[1], EspUiColors::skel_col[2], 1.f));
	const ImU32 snapCol = ImGui::ColorConvertFloat4ToU32(ImVec4(EspUiColors::snapline_esp[0], EspUiColors::snapline_esp[1], EspUiColors::snapline_esp[2], 1.f));
	const ImU32 headFill = ImGui::ColorConvertFloat4ToU32(ImVec4(
		EspUiColors::head_circle_fill[0], EspUiColors::head_circle_fill[1], EspUiColors::head_circle_fill[2], EspUiColors::head_circle_fill[3]));

	dl->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
	EspLayout::ClearHits();

	auto projectBone = [&](int id, ImVec2& sp) -> bool {
		float x, y, z;
		BoneLocal(id, x, y, z);
		return Project(mvp, x, y, z, origin, size, sp);
	};

	const ImVec2 boneShift = EspLayout::Get(EspLayout::Bones);
	if (Settings::Visuals::bones) {
		static constexpr int kLinks[][2] = {
			{ 1, 3 }, { 3, 4 }, { 4, 23 }, { 23, 6 }, { 6, 7 },
			{ 6, 9 }, { 9, 10 }, { 10, 11 },
			{ 6, 13 }, { 13, 14 }, { 14, 15 },
			{ 1, 17 }, { 17, 18 }, { 18, 19 },
			{ 1, 20 }, { 20, 21 }, { 21, 22 },
		};
		for (const auto& L : kLinks) {
			ImVec2 a, b;
			if (projectBone(L[0], a) && projectBone(L[1], b)) {
				a.x += boneShift.x; a.y += boneShift.y;
				b.x += boneShift.x; b.y += boneShift.y;
				dl->AddLine(a, b, skelCol, 1.4f);
				EspLayout::AddLine(EspLayout::Bones, a, b);
			}
		}
	}

	ImVec2 headSp;
	const bool headOk = projectBone(7, headSp);

	if (Settings::Visuals::eyeRay && headOk) {
		ImVec2 tip;
		float x, y, z;
		BoneLocal(7, x, y, z);
		z += 0.42f;
		if (Project(mvp, x, y, z, origin, size, tip)) {
			const ImVec2 es = EspLayout::Get(EspLayout::Eye);
			ImVec2 a(headSp.x + es.x, headSp.y + es.y);
			ImVec2 b(tip.x + es.x, tip.y + es.y);
			const ImU32 eyeCol = ImGui::ColorConvertFloat4ToU32(ImVec4(EspUiColors::eye_ray[0], EspUiColors::eye_ray[1], EspUiColors::eye_ray[2], 1.f));
			dl->AddLine(a, b, eyeCol, 1.6f);
			EspLayout::AddLine(EspLayout::Eye, a, b);
		}
	}

	catalyst_esp::DrawOffsets off{};
	{
		float labelTop = ry - 2.f;
		const float cx = rx + rw * 0.5f;
		if (Settings::Visuals::bombCarrierEsp) {
			const ImVec2 ts = ImGui::CalcTextSize("[C4]");
			labelTop -= ts.y + 2.f;
			const ImVec2 p = EspLayout::Shift(EspLayout::C4, cx, labelTop);
			const ImU32 c4 = ImGui::ColorConvertFloat4ToU32(ImVec4(EspUiColors::bomb_carrier_tag[0], EspUiColors::bomb_carrier_tag[1], EspUiColors::bomb_carrier_tag[2], 1.f));
			ex_esp::StrokeTextBg(dl, "[C4]", p.x, p.y, c4);
			EspLayout::AddRect(EspLayout::C4, p.x - ts.x * 0.5f, p.y, ts.x, ts.y);
		}
		if (Settings::Visuals::names) {
			const char* nm = "Player";
			const ImVec2 ns = ImGui::CalcTextSize(nm);
			labelTop -= ns.y;
			const ImVec2 p = EspLayout::Shift(EspLayout::Name, cx, labelTop);
			ex_esp::StrokeTextBg(dl, nm, p.x, p.y, nameCol);
			EspLayout::AddRect(EspLayout::Name, p.x - ns.x * 0.5f, p.y, ns.x, ns.y);
		}
	}

	if (Settings::Visuals::bBox && Settings::Visuals::boxMode != 0) {
		const bool wantFill = Settings::Visuals::filledBox || Settings::Visuals::boxMode == 3;
		const int bm = (Settings::Visuals::boxMode == 3) ? 1 : Settings::Visuals::boxMode;
		catalyst_esp::DrawCatalystBox(dl, rx, ry, rw, rh, outlineIm, wantFill, true, bm);
	}

	if (Settings::Visuals::bSnaplines) {
		const float cx = rx + rw * 0.5f;
		const ImVec2 ss = EspLayout::Get(EspLayout::Snap);
		ImVec2 a;
		ImVec2 b;
		switch (Settings::Visuals::snaplineMode) {
		case 1:
			a = ImVec2(cx + ss.x, ry + rh + ss.y);
			b = ImVec2(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);
			break;
		case 2:
			a = ImVec2(cx + ss.x, ry + ss.y);
			b = ImVec2(origin.x + size.x * 0.5f, origin.y);
			break;
		default:
			a = ImVec2(cx + ss.x, ry + rh + ss.y);
			b = ImVec2(origin.x + size.x * 0.5f, origin.y + size.y);
			break;
		}
		dl->AddLine(a, b, snapCol, 1.4f);
		EspLayout::AddLine(EspLayout::Snap, a, b);
	}

	if (Settings::Visuals::headcircle && headOk) {
		const ImVec2 hs = EspLayout::Shift(EspLayout::Head, headSp.x, headSp.y);
		const float rad = (std::max)(rh * 0.08f, 3.f);
		dl->AddCircle(hs, rad, outlineIm, 0, 1.6f);
		dl->AddCircleFilled(hs, rad, headFill, 32);
		EspLayout::AddRect(EspLayout::Head, hs.x - rad, hs.y - rad, rad * 2.f, rad * 2.f);
	}

	constexpr int kHp = 76;
	if (Settings::Visuals::healthBar) {
		const ImVec2 hp = EspLayout::Shift(EspLayout::HealthBar, rx, ry);
		catalyst_esp::DrawHealthBarLeft(dl, hp.x, hp.y, rh, kHp, off, kHp < 100 || Settings::Visuals::healthText);
		const float barX = std::floor(hp.x - 3.5f - 4.f - 1.f);
		EspLayout::AddRect(EspLayout::HealthBar, barX - 2.f, hp.y - 16.f, 14.f, rh + 18.f);
	}

	if (Settings::Visuals::armor) {
		const float ap = 0.55f;
		const float barW = 3.5f;
		const ImVec2 ap0 = EspLayout::Shift(EspLayout::Armor, std::floor(rx - off.left - 4.f - barW), ry);
		const ImU32 armorCol = ImGui::ColorConvertFloat4ToU32(ImVec4(EspUiColors::armor_bar[0], EspUiColors::armor_bar[1], EspUiColors::armor_bar[2], 1.f));
		const float filled = std::floor(rh * ap);
		dl->AddRectFilled(ImVec2(ap0.x - 1.f, ap0.y - 1.f), ImVec2(ap0.x + barW + 1.f, ap0.y + rh + 1.f), IM_COL32(0, 0, 0, 240));
		dl->AddRectFilled(ImVec2(ap0.x, ap0.y), ImVec2(ap0.x + barW, ap0.y + rh), IM_COL32(20, 20, 24, 230));
		dl->AddRectFilled(ImVec2(ap0.x, ap0.y + rh - filled), ImVec2(ap0.x + barW, ap0.y + rh), armorCol);
		EspLayout::AddRect(EspLayout::Armor, ap0.x - 2.f, ap0.y, barW + 6.f, rh);
		off.left += barW + 4.f;
	}

	if (Settings::Visuals::ammoBar || Settings::Visuals::ammoText) {
		const ImVec2 am = EspLayout::Shift(EspLayout::Ammo, rx, ry + rh);
		const ImU32 ammoTxtCol = ImGui::ColorConvertFloat4ToU32(ImVec4(EspUiColors::ammo_text_esp[0], EspUiColors::ammo_text_esp[1], EspUiColors::ammo_text_esp[2], 1.f));
		catalyst_esp::DrawAmmoBarBottom(dl, am.x, am.y, rw, rh, 24, 30, off,
			Settings::Visuals::ammoBar, Settings::Visuals::ammoText, ammoTxtCol);
		EspLayout::AddRect(EspLayout::Ammo, am.x, am.y, rw, 28.f);
	}

	if (Settings::Visuals::weaponEsp || Settings::Visuals::weaponEspIcon) {
		const ImVec2 wp = EspLayout::Shift(EspLayout::Weapon, rx, ry + rh);
		catalyst_esp::DrawWeaponGlyphAndOrText(dl, wp.x, wp.y, rw, "ak47", wpnCol, off,
			Settings::Visuals::weaponEspIcon, Settings::Visuals::weaponEsp);
		EspLayout::AddRect(EspLayout::Weapon, wp.x, wp.y + 2.f, rw, 36.f);
	}

	{
		float fx = rx + rw + 4.f + off.right;
		float fy = ry;
		if (Settings::Visuals::distance) {
			const ImVec2 ts = ImGui::CalcTextSize("18m");
			const ImVec2 p = EspLayout::Shift(EspLayout::Distance, fx, fy);
			ex_esp::StrokeTextBgLeft(dl, "18m", p.x, p.y, distCol);
			EspLayout::AddRect(EspLayout::Distance, p.x, p.y, ts.x, ts.y);
			fy += ts.y + 3.f;
		}
		if (Settings::Visuals::healthText && !Settings::Visuals::healthBar) {
			const float hpNorm = kHp / 100.f;
			const ImU32 hpCol = ImGui::ColorConvertFloat4ToU32(ImVec4(
				EspUiColors::health_text_damaged[0] * (1.f - hpNorm) + EspUiColors::health_text_full[0] * hpNorm,
				EspUiColors::health_text_damaged[1] * (1.f - hpNorm) + EspUiColors::health_text_full[1] * hpNorm,
				EspUiColors::health_text_damaged[2] * (1.f - hpNorm) + EspUiColors::health_text_full[2] * hpNorm,
				1.f));
			const ImVec2 ts = ImGui::CalcTextSize("76");
			const ImVec2 p = EspLayout::Shift(EspLayout::HealthText, fx, fy);
			ex_esp::StrokeTextBgLeft(dl, "76", p.x, p.y, hpCol);
			EspLayout::AddRect(EspLayout::HealthText, p.x, p.y, ts.x, ts.y);
			fy += ts.y + 3.f;
		}
		if (Settings::Visuals::showScoped) {
			const ImU32 zCol = ImGui::ColorConvertFloat4ToU32(ImVec4(EspUiColors::scoped_label[0], EspUiColors::scoped_label[1], EspUiColors::scoped_label[2], 1.f));
			const ImVec2 ts = ImGui::CalcTextSize("zoom");
			const ImVec2 p = EspLayout::Shift(EspLayout::Zoom, fx, fy);
			ex_esp::StrokeTextBgLeft(dl, "zoom", p.x, p.y, zCol);
			EspLayout::AddRect(EspLayout::Zoom, p.x, p.y, ts.x, ts.y);
			fy += ts.y + 2.f;
		}
		if (Settings::Visuals::showBlind) {
			const ImU32 blindCol = ImGui::ColorConvertFloat4ToU32(ImVec4(EspUiColors::blind_label[0], EspUiColors::blind_label[1], EspUiColors::blind_label[2], 1.f));
			const ImVec2 ts = ImGui::CalcTextSize("flashed");
			const ImVec2 p = EspLayout::Shift(EspLayout::Flashed, fx, fy);
			ex_esp::StrokeTextBgLeft(dl, "flashed", p.x, p.y, blindCol);
			EspLayout::AddRect(EspLayout::Flashed, p.x, p.y, ts.x, ts.y);
		}
	}

	dl->PopClipRect();
}

void RenderModel(ID3D11DeviceContext* ctx, int w, int h, XMFLOAT4X4& outMvp)
{
	const XMMATRIX rot = XMMatrixRotationRollPitchYaw(g_pitch, g_yaw, 0.f);
	const XMMATRIX trans = XMMatrixTranslation(g_panX, g_panY, 0.f);
	
	const XMMATRIX world = XMMatrixScaling(-1.f, 1.f, 1.f) * rot * trans;
	const XMVECTOR eye = XMVectorSet(0.f, 0.28f, g_dist, 0.f);
	const XMVECTOR at = XMVectorSet(0.f, 0.02f, 0.f, 0.f);
	const XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
	const XMMATRIX view = XMMatrixLookAtLH(eye, at, up);
	const float aspect = (h > 0) ? (static_cast<float>(w) / static_cast<float>(h)) : 1.f;
	const XMMATRIX proj = XMMatrixPerspectiveFovLH(XMConvertToRadians(34.f), aspect, 0.05f, 40.f);
	const XMMATRIX mvp = world * view * proj;

	CBData cb{};
	XMStoreFloat4x4(&cb.world, world);
	XMStoreFloat4x4(&cb.mvp, mvp);
	XMStoreFloat4(&cb.cam, eye);
	XMStoreFloat4(&cb.light, XMVector3Normalize(XMVectorSet(0.35f, 0.82f, 0.45f, 0.f)));
	cb.misc.x = g_mesh.albedo ? 1.f : 0.f;
	outMvp = cb.mvp;

	D3D11_MAPPED_SUBRESOURCE map{};
	if (FAILED(ctx->Map(g_pipe.cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &map)))
		return;
	memcpy(map.pData, &cb, sizeof(cb));
	ctx->Unmap(g_pipe.cb, 0);

	const float clear[4] = { 17.f / 255.f, 17.f / 255.f, 17.f / 255.f, 1.f };
	ctx->OMSetRenderTargets(1, &g_pipe.rtv, g_pipe.dsv);
	ctx->ClearRenderTargetView(g_pipe.rtv, clear);
	ctx->ClearDepthStencilView(g_pipe.dsv, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0);
	D3D11_VIEWPORT vp{};
	vp.Width = static_cast<float>(w);
	vp.Height = static_cast<float>(h);
	vp.MaxDepth = 1.f;
	ctx->RSSetViewports(1, &vp);
	ctx->RSSetState(g_pipe.rs);
	ctx->OMSetDepthStencilState(g_pipe.ds, 0);
	const float blendF[4] = {};
	ctx->OMSetBlendState(g_pipe.blend, blendF, 0xFFFFFFFFu);
	ctx->IASetInputLayout(g_pipe.layout);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	const UINT stride = sizeof(Vtx);
	const UINT offset = 0;
	ctx->IASetVertexBuffers(0, 1, &g_mesh.vb, &stride, &offset);
	ctx->IASetIndexBuffer(g_mesh.ib, DXGI_FORMAT_R32_UINT, 0);
	ctx->VSSetShader(g_pipe.vs, nullptr, 0);
	ctx->PSSetShader(g_pipe.ps, nullptr, 0);
	ctx->VSSetConstantBuffers(0, 1, &g_pipe.cb);
	ctx->PSSetConstantBuffers(0, 1, &g_pipe.cb);
	ctx->PSSetSamplers(0, 1, &g_pipe.samp);
	if (g_mesh.albedo)
		ctx->PSSetShaderResources(0, 1, &g_mesh.albedo);
	else {
		ID3D11ShaderResourceView* nullSrv = nullptr;
		ctx->PSSetShaderResources(0, 1, &nullSrv);
	}
	ctx->DrawIndexed(g_mesh.indexCount, 0, 0);

	ID3D11ShaderResourceView* nullSrv = nullptr;
	ctx->PSSetShaderResources(0, 1, &nullSrv);
	ID3D11RenderTargetView* nullRtv = nullptr;
	ctx->OMSetRenderTargets(1, &nullRtv, nullptr);
}

} 

void ExpectionalDrawEspPreview()
{
	if (!Settings::bMenu)
		return;
	ImGuiWindow* menu = ImGui::FindWindowByName("Expectional");
	if (!menu)
		return;

	const ImGuiIO& io = ImGui::GetIO();
	float x = menu->Pos.x + menu->Size.x;
	if (x + kPreviewW > io.DisplaySize.x - 4.f)
		x = menu->Pos.x - kPreviewW;
	if (x < 4.f)
		x = 4.f;

	ImGui::SetNextWindowPos(ImVec2(x, menu->Pos.y), ImGuiCond_Always);
	ImGui::SetNextWindowSize(ImVec2(kPreviewW, menu->Size.y), ImGuiCond_Always);
	ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(17.f / 255.f, 17.f / 255.f, 17.f / 255.f, 0.97f));
	ImGui::PushStyleColor(ImGuiCol_Border, c::background::stroke);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.f, 8.f));
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f);
	const ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
	ImGui::Begin("##EspPreview", nullptr, wf);

	ImDrawList* dl = ImGui::GetWindowDrawList();
	const ImVec2 wp = ImGui::GetWindowPos();
	const ImVec2 ws = ImGui::GetWindowSize();
	dl->AddLine(ImVec2(wp.x, wp.y + 40.f), ImVec2(wp.x + ws.x, wp.y + 40.f), ImGui::GetColorU32(c::background::stroke), 1.5f);
	if (font::lexend_bold) {
		dl->AddText(font::lexend_bold, 18.f, ImVec2(wp.x + 16.f, wp.y + 11.f), ImGui::ColorConvertFloat4ToU32(c::accent), "ESP");
		const ImVec2 es = font::lexend_bold->CalcTextSizeA(18.f, FLT_MAX, 0.f, "ESP");
		dl->AddText(font::lexend_bold, 18.f, ImVec2(wp.x + 16.f + es.x, wp.y + 11.f), IM_COL32(255, 255, 255, 255), " Preview");
	} else {
		ImGui::SetCursorPos(ImVec2(12.f, 12.f));
		ImGui::TextColored(c::accent, "ESP Preview");
	}

	ID3D11Device* device = g_ExpectionalMainDX11Device;
	ID3D11DeviceContext* ctx = ExpectionalOverlayWindow::m_pContext;
	ImGui::SetCursorPos(ImVec2(10.f, 48.f));
	ImVec2 avail = ImGui::GetContentRegionAvail();
	avail.y -= 18.f;
	if (avail.y < 32.f)
		avail.y = 32.f;

	if (!device || !ctx) {
		ImGui::TextUnformatted("Preview device is not ready.");
	} else {
		EnsureLoaded(device);
		if (!g_mesh.ready) {
			ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + avail.x);
			ImGui::TextWrapped("%s", g_status.empty() ? "Loading preview..." : g_status.c_str());
			ImGui::PopTextWrapPos();
		} else if (EnsureTarget(device, static_cast<int>(avail.x), static_cast<int>(avail.y)) && g_pipe.srv) {
			if (!g_dragRot && !g_dragPan) {
				if (g_easeHome) {
					const float blend = 1.f - std::exp(-4.2f * io.DeltaTime);
					g_pitch += (kPitchHome - g_pitch) * blend;
					float yawGap = g_yawAtDrag - g_yaw;
					constexpr float kPi = 3.14159265f;
					while (yawGap > kPi) yawGap -= kPi * 2.f;
					while (yawGap < -kPi) yawGap += kPi * 2.f;
					g_yaw += yawGap * blend;
					if (std::fabs(g_pitch - kPitchHome) < 0.01f && std::fabs(yawGap) < 0.02f) {
						g_pitch = kPitchHome;
						g_yaw = g_yawAtDrag;
						g_easeHome = false;
					}
				} else {
					g_yaw -= io.DeltaTime * 0.45f;
				}
			}
			XMFLOAT4X4 mvp{};
			static double s_nextPreview = 0.0;
			static XMFLOAT4X4 s_cachedMvp{};
			static bool s_havePreview = false;
			const double nowPreview = ImGui::GetTime();
			if (!s_havePreview || nowPreview >= s_nextPreview) {
				RenderModel(ctx, g_pipe.rtW, g_pipe.rtH, mvp);
				s_cachedMvp = mvp;
				s_havePreview = true;
				s_nextPreview = nowPreview + (1.0 / 60.0);
			} else {
				mvp = s_cachedMvp;
			}
			const ImVec2 imgPos = ImGui::GetCursorScreenPos();
			const ImVec2 imgSz(static_cast<float>(g_pipe.rtW), static_cast<float>(g_pipe.rtH));
			ImGui::Image(reinterpret_cast<ImTextureID>(g_pipe.srv), imgSz);
			ImGui::SetCursorScreenPos(imgPos);
			ImGui::InvisibleButton("##esp_preview_orbit", imgSz);
			if (ImGui::IsItemActivated() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				g_dragRot = true;
				g_yawAtDrag = g_yaw;
			}
			if (g_dragRot && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
				g_dragRot = false;
				g_easeHome = true;
			}
			if (g_dragRot) {
				g_yaw += io.MouseDelta.x * 0.01f;
				g_pitch += io.MouseDelta.y * 0.01f;
				g_pitch = std::clamp(g_pitch, -1.15f, 1.15f);
			}
			if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				g_dragPan = true;
			if (!ImGui::IsMouseDown(ImGuiMouseButton_Right))
				g_dragPan = false;
			if (g_dragPan) {
				g_panX += io.MouseDelta.x * 0.004f;
				g_panY -= io.MouseDelta.y * 0.004f;
				g_panX = std::clamp(g_panX, -1.2f, 1.2f);
				g_panY = std::clamp(g_panY, -1.2f, 1.2f);
			}
			if (ImGui::IsItemHovered()) {
				g_dist -= io.MouseWheel * 0.28f;
				g_dist = std::clamp(g_dist, 1.35f, 8.f);
			}
			DrawPreviewEsp(dl, imgPos, imgSz, mvp);
		}
	}

	ImGui::SetCursorPos(ImVec2(12.f, ws.y - 20.f));
	ImGui::TextDisabled("Left drag   Right drag   Scroll");
	ImGui::End();
	ImGui::PopStyleVar(3);
	ImGui::PopStyleColor(2);
}
