#include "GPUOcclusionCulling.h"
#include "Render/Proxy/PrimitiveSceneProxy.h"
#include "Profiling/Stats.h"

#include <cstring>
#include <d3dcompiler.h>

// ── CB layouts matching HLSL ──

struct FHiZParamsCB
{
	uint32 SrcWidth;
	uint32 SrcHeight;
	uint32 ActualWidth;
	uint32 ActualHeight;
};

struct FOcclusionTestParamsCB
{
	FMatrix ViewProj;        // 64 bytes
	float   ViewportWidth;   // 4
	float   ViewportHeight;  // 4
	uint32  NumAABBs;        // 4
	uint32  HiZMipCount;     // 4
};

// ================================================================
// Helper: compile a compute shader from file
// ================================================================
static ID3D11ComputeShader* CompileCS(ID3D11Device* Dev, const wchar_t* Path, const char* Entry)
{
	ID3DBlob* csBlob = nullptr;
	ID3DBlob* errBlob = nullptr;

	HRESULT hr = D3DCompileFromFile(Path, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
		Entry, "cs_5_0", 0, 0, &csBlob, &errBlob);

	if (FAILED(hr))
	{
		if (errBlob)
		{
			MessageBoxA(nullptr, (char*)errBlob->GetBufferPointer(),
				"Compute Shader Compile Error", MB_OK | MB_ICONERROR);
			errBlob->Release();
		}
		return nullptr;
	}

	ID3D11ComputeShader* cs = nullptr;
	hr = Dev->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &cs);
	csBlob->Release();

	return SUCCEEDED(hr) ? cs : nullptr;
}

// ================================================================
// Initialize / Release
// ================================================================

void FGPUOcclusionCulling::Initialize(ID3D11Device* InDevice)
{
	Device = InDevice;

	HiZCopyCS = CompileCS(Device, L"Shaders/HiZGenerate.hlsl", "CSCopyDepth");
	HiZDownsampleCS = CompileCS(Device, L"Shaders/HiZGenerate.hlsl", "CSDownsample");
	OcclusionTestCS = CompileCS(Device, L"Shaders/OcclusionTest.hlsl", "CSOcclusionTest");

	if (!HiZCopyCS || !HiZDownsampleCS || !OcclusionTestCS)
	{
		Release();
		return;
	}

	// Constant buffer — large enough for the bigger of the two CB structs
	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.ByteWidth = sizeof(FOcclusionTestParamsCB);
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	Device->CreateBuffer(&cbDesc, nullptr, &ParamsCB);

	bInitialized = (ParamsCB != nullptr);
}

void FGPUOcclusionCulling::Release()
{
	ReleaseHiZResources();
	ReleaseBuffers();

	if (ParamsCB) { ParamsCB->Release();        ParamsCB = nullptr; }
	if (HiZCopyCS) { HiZCopyCS->Release();       HiZCopyCS = nullptr; }
	if (HiZDownsampleCS) { HiZDownsampleCS->Release(); HiZDownsampleCS = nullptr; }
	if (OcclusionTestCS) { OcclusionTestCS->Release();  OcclusionTestCS = nullptr; }

	OccludedBits.clear();
	bHasResults = false;
	for (uint32 i = 0; i < STAGING_COUNT; i++)
	{
		StagingProxies[i].clear();
		StagingProxyCount[i] = 0;
	}
	WriteIndex = 0;
	FrameCount = 0;
	bInitialized = false;
	Device = nullptr;
}

void FGPUOcclusionCulling::InvalidateResults()
{
	for (uint32 i = 0; i < STAGING_COUNT; i++)
	{
		StagingProxies[i].clear();
		StagingProxyCount[i] = 0;
		StagingMaxProxyId[i] = 0;
	}
	OccludedBits.clear();
	bHasResults = false;
	WriteIndex = 0;
	FrameCount = 0;

	// Pre-gather 상태도 리셋
	bPreGathered = false;
	PreGatherWritePos = 0;
	PreGatherMaxId = 0;
	CPUAABBStaging.clear();
}

// ================================================================
// Hi-Z resources (recreated on viewport resize)
// ================================================================

void FGPUOcclusionCulling::CreateHiZResources(uint32 Width, uint32 Height)
{
	// 다음 Power of Two 계산
	uint32 PoTWidth = 1;  while (PoTWidth < Width)   PoTWidth <<= 1;
	uint32 PoTHeight = 1; while (PoTHeight < Height) PoTHeight <<= 1;

	if (HiZWidth == PoTWidth && HiZHeight == PoTHeight && HiZTextureA)
		return;

	ReleaseHiZResources();
	HiZWidth = PoTWidth;
	HiZHeight = PoTHeight;

	uint32 maxDim = (PoTWidth > PoTHeight) ? PoTWidth : PoTHeight;
	HiZMipCount = 1;
	uint32 tmp = maxDim;
	while (tmp > 1) { tmp >>= 1; HiZMipCount++; }

	D3D11_TEXTURE2D_DESC desc = {};
	desc.Width = PoTWidth;
	desc.Height = PoTHeight;
	desc.MipLevels = HiZMipCount;
	desc.ArraySize = 1;
	desc.Format = DXGI_FORMAT_R32G32_FLOAT;
	desc.SampleDesc.Count = 1;
	desc.Usage = D3D11_USAGE_DEFAULT;
	desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

	if (FAILED(Device->CreateTexture2D(&desc, nullptr, &HiZTextureA))) return;
	if (FAILED(Device->CreateTexture2D(&desc, nullptr, &HiZTextureB))) return;

	// OcclusionTest용 Full-chain SRVs (R32G32_FLOAT)
	D3D11_SHADER_RESOURCE_VIEW_DESC srvAll = {};
	srvAll.Format = DXGI_FORMAT_R32G32_FLOAT;
	srvAll.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
	srvAll.Texture2D.MipLevels = HiZMipCount;
	srvAll.Texture2D.MostDetailedMip = 0;
	Device->CreateShaderResourceView(HiZTextureA, &srvAll, &HiZSRV_A);
	Device->CreateShaderResourceView(HiZTextureB, &srvAll, &HiZSRV_B);

	HiZUAVs_A.resize(HiZMipCount, nullptr);
	HiZUAVs_B.resize(HiZMipCount, nullptr);
	HiZSRVs_A.resize(HiZMipCount, nullptr);
	HiZSRVs_B.resize(HiZMipCount, nullptr);

	for (uint32 i = 0; i < HiZMipCount; i++)
	{
		D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2D;
		uavDesc.Texture2D.MipSlice = i;
		Device->CreateUnorderedAccessView(HiZTextureA, &uavDesc, &HiZUAVs_A[i]);
		Device->CreateUnorderedAccessView(HiZTextureB, &uavDesc, &HiZUAVs_B[i]);

		D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
		srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Texture2D.MostDetailedMip = i;
		Device->CreateShaderResourceView(HiZTextureA, &srvDesc, &HiZSRVs_A[i]);
		Device->CreateShaderResourceView(HiZTextureB, &srvDesc, &HiZSRVs_B[i]);
	}
}
void FGPUOcclusionCulling::ReleaseHiZResources()
{
	for (auto* v : HiZUAVs_A) if (v) v->Release();
	for (auto* v : HiZUAVs_B) if (v) v->Release();
	for (auto* v : HiZSRVs_A) if (v) v->Release();
	for (auto* v : HiZSRVs_B) if (v) v->Release();
	HiZUAVs_A.clear(); HiZUAVs_B.clear();
	HiZSRVs_A.clear(); HiZSRVs_B.clear();

	if (HiZSRV_A) { HiZSRV_A->Release(); HiZSRV_A = nullptr; }
	if (HiZSRV_B) { HiZSRV_B->Release(); HiZSRV_B = nullptr; }
	if (HiZTextureA) { HiZTextureA->Release(); HiZTextureA = nullptr; }
	if (HiZTextureB) { HiZTextureB->Release(); HiZTextureB = nullptr; }
	HiZWidth = HiZHeight = HiZMipCount = 0;
}

// ================================================================
// Hi-Z generation
// ================================================================

// 원본 뎁스 텍스처로부터 Hi-Z 밉 체인(Min/Max 뎁스)을 생성합니다.
void FGPUOcclusionCulling::GenerateHiZ(ID3D11DeviceContext* Ctx, ID3D11ShaderResourceView* DepthSRV, uint32 Width, uint32 Height)
{
	SCOPE_STAT_CAT("GenerateHiZ", "4_ExecutePass");
	CreateHiZResources(Width, Height);
	if (!HiZTextureA || HiZMipCount == 0) return;

	ID3D11ShaderResourceView* nullSRVs[2] = { nullptr, nullptr };
	ID3D11UnorderedAccessView* nullUAVs[1] = { nullptr };

	// ── Mip 0: Depth -> TextureA[0] ──
	{
		FHiZParamsCB p = { HiZWidth, HiZHeight, Width, Height };
		UpdateParamsCB(Ctx, &p, sizeof(p));

		Ctx->CSSetShader(HiZCopyCS, nullptr, 0);
		Ctx->CSSetConstantBuffers(0, 1, &ParamsCB);
		Ctx->CSSetShaderResources(0, 1, &DepthSRV);
		Ctx->CSSetUnorderedAccessViews(0, 1, &HiZUAVs_A[0], nullptr);
		Ctx->Dispatch((HiZWidth + 7) / 8, (HiZHeight + 7) / 8, 1);

		// Unbind to prevent hazards
		Ctx->CSSetShaderResources(0, 1, nullSRVs);
		Ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
	}

	// ── Mip 1+: Ping-pong Downsample ──
	Ctx->CSSetShader(HiZDownsampleCS, nullptr, 0);
	uint32 mW = HiZWidth, mH = HiZHeight;

	for (uint32 mip = 1; mip < HiZMipCount; mip++)
	{
		uint32 dW = (mW > 1) ? (mW / 2) : 1;
		uint32 dH = (mH > 1) ? (mH / 2) : 1;

		FHiZParamsCB p = { mW, mH, 0, 0 };
		UpdateParamsCB(Ctx, &p, sizeof(p));

		bool bSrcIsA = ((mip - 1) & 1) == 0;
		ID3D11ShaderResourceView* srcSRV = bSrcIsA ? HiZSRVs_A[mip - 1] : HiZSRVs_B[mip - 1];
		ID3D11UnorderedAccessView* dstUAV = bSrcIsA ? HiZUAVs_B[mip] : HiZUAVs_A[mip];

		Ctx->CSSetShaderResources(0, 1, &srcSRV);
		Ctx->CSSetUnorderedAccessViews(0, 1, &dstUAV, nullptr);
		Ctx->Dispatch((dW + 7) / 8, (dH + 7) / 8, 1);

		Ctx->CSSetShaderResources(0, 1, nullSRVs);
		Ctx->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);

		mW = dW; mH = dH;
	}
}

// ================================================================
// AABB + Visibility buffers
// ================================================================

void FGPUOcclusionCulling::CreateBuffers(uint32 ProxyCount)
{
	if (ProxyCount == 0) return;
	if (AABBBufferCapacity >= ProxyCount && AABBBuffer) return;

	ReleaseBuffers();
	uint32 Cap = ProxyCount + (ProxyCount / 4) + 64;
	AABBBufferCapacity = Cap; VisibilityBufferCapacity = Cap;

	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = Cap * sizeof(FGPUOcclusionAABB);
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = sizeof(FGPUOcclusionAABB);
		Device->CreateBuffer(&bd, nullptr, &AABBBuffer);

		D3D11_SHADER_RESOURCE_VIEW_DESC sd = {};
		sd.Format = DXGI_FORMAT_UNKNOWN;
		sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
		sd.Buffer.NumElements = Cap;
		Device->CreateShaderResourceView(AABBBuffer, &sd, &AABBSRV);
	}
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = Cap * sizeof(uint32);
		bd.Usage = D3D11_USAGE_DEFAULT;
		bd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
		bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
		bd.StructureByteStride = sizeof(uint32);
		Device->CreateBuffer(&bd, nullptr, &VisibilityBuffer);

		D3D11_UNORDERED_ACCESS_VIEW_DESC ud = {};
		ud.Format = DXGI_FORMAT_UNKNOWN;
		ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
		ud.Buffer.NumElements = Cap;
		Device->CreateUnorderedAccessView(VisibilityBuffer, &ud, &VisibilityUAV);
	}
	{
		D3D11_BUFFER_DESC bd = {};
		bd.ByteWidth = Cap * sizeof(uint32);
		bd.Usage = D3D11_USAGE_STAGING;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
		for (uint32 i = 0; i < STAGING_COUNT; i++)
			Device->CreateBuffer(&bd, nullptr, &StagingBuffers[i]);
	}
}

void FGPUOcclusionCulling::ReleaseBuffers()
{
	if (AABBSRV) AABBSRV->Release(); AABBSRV = nullptr;
	if (AABBBuffer) AABBBuffer->Release(); AABBBuffer = nullptr;
	if (VisibilityUAV) VisibilityUAV->Release(); VisibilityUAV = nullptr;
	if (VisibilityBuffer) VisibilityBuffer->Release(); VisibilityBuffer = nullptr;
	for (uint32 i = 0; i < STAGING_COUNT; i++) {
		if (StagingBuffers[i]) StagingBuffers[i]->Release();
		StagingBuffers[i] = nullptr;
		StagingProxies[i].clear();
		StagingProxyCount[i] = 0;
	}
	AABBBufferCapacity = VisibilityBufferCapacity = 0;
}

void FGPUOcclusionCulling::UpdateParamsCB(ID3D11DeviceContext* Ctx, const void* Data, uint32 Size)
{
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (SUCCEEDED(Ctx->Map(ParamsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
		memcpy(mapped.pData, Data, Size);
		Ctx->Unmap(ParamsCB, 0);
	}
}

// 이전 프레임의 GPU 스테이징 버퍼로부터 오클루전 결과를 읽어옵니다.
void FGPUOcclusionCulling::ReadbackResults(ID3D11DeviceContext* Ctx)
{
	uint32 ReadIdx = (WriteIndex + 1) % STAGING_COUNT;
	if (FrameCount < STAGING_COUNT || StagingProxyCount[ReadIdx] == 0 || !StagingBuffers[ReadIdx]) return;

	SCOPE_STAT_CAT("OcclusionReadback", "1_WorldTick");
	D3D11_MAPPED_SUBRESOURCE mapped;
	if (Ctx->Map(StagingBuffers[ReadIdx], 0, D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT, &mapped) == S_OK) {
		const uint32* vis = static_cast<const uint32*>(mapped.pData);
		const uint32 count = StagingProxyCount[ReadIdx];
		const auto& proxies = StagingProxies[ReadIdx];
		uint32 wordCount = (StagingMaxProxyId[ReadIdx] / 32) + 1;
		OccludedBits.assign(wordCount, 0);
		for (uint32 i = 0; i < count; i++) {
			if (vis[i] == 0) {
				uint32 id = proxies[i];
				OccludedBits[id >> 5] |= (1u << (id & 31));
			}
		}
		bHasResults = true;
		Ctx->Unmap(StagingBuffers[ReadIdx], 0);
	}
}

void FGPUOcclusionCulling::BeginGatherAABB(uint32 ExpectedCount)
{
	StagingProxies[WriteIndex].resize(ExpectedCount);
	CPUAABBStaging.resize(ExpectedCount);
	PreGatherWritePos = 0; PreGatherMaxId = 0; bPreGathered = false;
}

void FGPUOcclusionCulling::GatherAABB(FPrimitiveSceneProxy* Proxy)
{
	if (!Proxy || Proxy->bNeverCull) return;
	uint32 pos = PreGatherWritePos++;
	StagingProxies[WriteIndex][pos] = Proxy->ProxyId;
	if (Proxy->ProxyId > PreGatherMaxId) PreGatherMaxId = Proxy->ProxyId;
	const FBoundingBox& B = Proxy->CachedBounds;
	CPUAABBStaging[pos] = { B.Min.X, B.Min.Y, B.Min.Z, 0.0f, B.Max.X, B.Max.Y, B.Max.Z, 0.0f };
}

void FGPUOcclusionCulling::EndGatherAABB()
{
	StagingProxyCount[WriteIndex] = PreGatherWritePos;
	StagingMaxProxyId[WriteIndex] = PreGatherMaxId;
	bPreGathered = true;
}

// AABB를 업로드하고 오클루전 테스트 컴퓨트 쉐이더를 실행합니다.
void FGPUOcclusionCulling::DispatchOcclusionTest(ID3D11DeviceContext* Ctx, ID3D11ShaderResourceView* DepthSRV, const TArray<FPrimitiveSceneProxy*>& VisibleProxies, const FMatrix& View, const FMatrix& Proj, uint32 ViewportWidth, uint32 ViewportHeight)
{
	if (!bInitialized) return;
	SCOPE_STAT_CAT("OcclusionDispatch", "4_ExecutePass");

	if (!bPreGathered) {
		uint32 visCount = static_cast<uint32>(VisibleProxies.size());
		StagingProxies[WriteIndex].resize(visCount);
		CPUAABBStaging.resize(visCount);
		uint32 writePos = 0; uint32 maxId = 0;
		for (uint32 i = 0; i < visCount; i++) {
			FPrimitiveSceneProxy* Proxy = VisibleProxies[i];
			if (!Proxy || Proxy->bNeverCull) continue;
			StagingProxies[WriteIndex][writePos] = Proxy->ProxyId;
			if (Proxy->ProxyId > maxId) maxId = Proxy->ProxyId;
			const FBoundingBox& B = Proxy->CachedBounds;
			CPUAABBStaging[writePos++] = { B.Min.X, B.Min.Y, B.Min.Z, 0.0f, B.Max.X, B.Max.Y, B.Max.Z, 0.0f };
		}
		StagingProxyCount[WriteIndex] = writePos; StagingMaxProxyId[WriteIndex] = maxId;
	}
	bPreGathered = false;

	uint32 proxyCount = StagingProxyCount[WriteIndex];
	if (proxyCount == 0) { FrameCount++; WriteIndex = (WriteIndex + 1) % STAGING_COUNT; return; }

	CreateBuffers(proxyCount);
	if (!AABBBuffer || !VisibilityBuffer) return;

	D3D11_BOX dstBox = { 0, 0, 0, proxyCount * static_cast<uint32>(sizeof(FGPUOcclusionAABB)), 1, 1 };
	Ctx->UpdateSubresource(AABBBuffer, 0, &dstBox, CPUAABBStaging.data(), 0, 0);

	// Generate Hi-Z if a depth SRV is provided
	if (DepthSRV)
	{
		GenerateHiZ(Ctx, DepthSRV, ViewportWidth, ViewportHeight);
	}

	if (!HiZTextureA || HiZMipCount == 0) return;

	FOcclusionTestParamsCB params = { View * Proj, (float)ViewportWidth, (float)ViewportHeight, proxyCount, HiZMipCount };
	UpdateParamsCB(Ctx, &params, sizeof(params));

	Ctx->CSSetShader(OcclusionTestCS, nullptr, 0);
	Ctx->CSSetConstantBuffers(0, 1, &ParamsCB);

	ID3D11ShaderResourceView* srvs[3] = { AABBSRV, HiZSRV_A, HiZSRV_B };
	Ctx->CSSetShaderResources(0, 3, srvs);
	Ctx->CSSetUnorderedAccessViews(0, 1, &VisibilityUAV, nullptr);
	Ctx->Dispatch((proxyCount + 63) / 64, 1, 1);

	ID3D11ShaderResourceView* nullSRVs[3] = { nullptr, nullptr, nullptr };
	Ctx->CSSetShaderResources(0, 3, nullSRVs);
	Ctx->CSSetUnorderedAccessViews(0, 1, (ID3D11UnorderedAccessView**)&nullSRVs[0], nullptr);

	Ctx->CopyResource(StagingBuffers[WriteIndex], VisibilityBuffer);
	WriteIndex = (WriteIndex + 1) % STAGING_COUNT; FrameCount++;
}

bool FGPUOcclusionCulling::IsOccluded(const FPrimitiveSceneProxy* Proxy) const
{
	uint32 id = Proxy->ProxyId; uint32 word = id >> 5;
	if (word >= static_cast<uint32>(OccludedBits.size())) return false;
	return (OccludedBits[word] & (1u << (id & 31))) != 0;
}
