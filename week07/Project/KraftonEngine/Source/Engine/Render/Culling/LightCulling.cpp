#include "LightCulling.h"
#include <d3dcompiler.h>
#include <cmath>
#include <algorithm>
#include <vector>
#include "Render/Device/D3DDevice.h"
#include "Render/Pipeline/RenderConstants.h"

#pragma comment(lib, "d3dcompiler.lib")

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
			OutputDebugStringA((char*)errBlob->GetBufferPointer());
			errBlob->Release();
		}
		return nullptr;
	}

	ID3D11ComputeShader* cs = nullptr;
	hr = Dev->CreateComputeShader(csBlob->GetBufferPointer(), csBlob->GetBufferSize(), nullptr, &cs);
	csBlob->Release();

	return SUCCEEDED(hr) ? cs : nullptr;
}

void FLightCulling::Initialize(ID3D11Device* InDevice)
{
	Device = InDevice;
	LightCullingCS = CompileCS(Device, L"Shaders/LightCullingCS.hlsl", "CSMain");

	if (!LightCullingCS) return;

	D3D11_BUFFER_DESC cbDesc = {};
	cbDesc.ByteWidth = sizeof(FLightCullingParams);
	cbDesc.Usage = D3D11_USAGE_DYNAMIC;
	cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	Device->CreateBuffer(&cbDesc, nullptr, &ParamsCB);

	bInitialized = (ParamsCB != nullptr);
}

void FLightCulling::Release()
{
	if (ParamsCB) { ParamsCB->Release(); ParamsCB = nullptr; }
	if (LightCullingCS) { LightCullingCS->Release(); LightCullingCS = nullptr; }
	
	AllLights.Release();
	LightIndexList.Release();
	LightGrid.Release();
	IndexCounter.Release();

	bInitialized = false;
}

void FLightCulling::UpdateBuffers(ID3D11Device* InDevice, uint32 Width, uint32 Height, uint32 LightCount)
{
	uint32 NewGridX = (Width + TILE_SIZE - 1) / TILE_SIZE;
	uint32 NewGridY = (Height + TILE_SIZE - 1) / TILE_SIZE;
	uint32 NewGridZ = 32;

	if (NewGridX != GridX || NewGridY != GridY || NewGridZ != GridZ)
	{
		GridX = NewGridX;
		GridY = NewGridY;
		GridZ = NewGridZ;
		
		uint32 TotalClusters = GridX * GridY * GridZ;
		LightGrid.Create(InDevice, sizeof(FTileInfo), TotalClusters, true, false);
		
		// Pool size for LightIndexList: 32M entries (covers 32 slices * 256k clusters * 128 lights max)
		const uint32 MaxIndexEntries = 1024 * 1024 * 32;
		LightIndexList.Create(InDevice, sizeof(uint32), MaxIndexEntries, true, false);		
		// Atomic counter for LightIndexList
		IndexCounter.Create(InDevice, sizeof(uint32), 1, true, false);
	}

	if (LightCount > CurrentLightCapacity)
	{
		CurrentLightCapacity = LightCount > 0 ? LightCount : 64;
		CurrentLightCapacity *= 2;
		AllLights.Create(InDevice, sizeof(FLightDataGPU), CurrentLightCapacity, false, true);
	}
}

void FLightCulling::Execute(
	ID3D11DeviceContext* Ctx,
	ID3D11ShaderResourceView* DepthSRV,
	const FMatrix& View,
	const FMatrix& Proj,
	uint32 ViewportWidth,
	uint32 ViewportHeight,
	const float NearClip,
	const float FarClip,
	const TArray<FLightDataGPU>& Lights)
{
	if (!bInitialized) return;

	// CPU-side Filtering and Prioritization (Omitted details for brevity, keep logic as is)
	std::vector<FLightDataGPU> FilteredLights;

	if (Lights.size() > 0)
	{
		// Correctly Extract Camera World Position from Row-Major Inverse View Matrix
		FMatrix InvView = View.GetInverse();

		FVector CameraPos;
		CameraPos.X = InvView.M[3][0];
		CameraPos.Y = InvView.M[3][1];
		CameraPos.Z = InvView.M[3][2];

		struct SortEntry {
			uint32 Idx;
			float DistSq;
		};
		std::vector<SortEntry> Others;

		for (uint32 i = 0; i < (uint32)Lights.size(); ++i)
		{
			if (Lights[i].LightType == 0 || Lights[i].LightType == 1)
			{
				FilteredLights.push_back(Lights[i]);
			}
			else
			{
				FVector Offset = Lights[i].Position - CameraPos;
				float d2 = (Offset.X * Offset.X) + (Offset.Y * Offset.Y) + (Offset.Z * Offset.Z);
				Others.push_back({ i, d2 });
			}
		}

		std::sort(Others.begin(), Others.end(), [](const SortEntry& a, const SortEntry& b) {
			return a.DistSq < b.DistSq;
		});

		uint32 SlotsLeft = (GLOBAL_MAX_LIGHTS > (uint32)FilteredLights.size()) 
                           ? (GLOBAL_MAX_LIGHTS - (uint32)FilteredLights.size()) 
                           : 0;
		uint32 ToAdd = std::min((uint32)Others.size(), SlotsLeft);
		for (uint32 i = 0; i < ToAdd; ++i)
		{
			FilteredLights.push_back(Lights[Others[i].Idx]);
		}
	}

	UpdateBuffers(Device, ViewportWidth, ViewportHeight, static_cast<uint32>(FilteredLights.size()));

	if (FilteredLights.size() > 0)
	{
		AllLights.Update(Ctx, FilteredLights.data(), static_cast<uint32>(FilteredLights.size()));
	}

	D3D11_MAPPED_SUBRESOURCE Mapped = {};
	if (SUCCEEDED(Ctx->Map(ParamsCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &Mapped)))
	{
		FLightCullingParams* Params = (FLightCullingParams*)Mapped.pData;
		Params->View = View;
		Params->Proj = Proj;
		Params->InvProj = Proj.GetInverse();
		Params->ViewportSize = FVector2((float)ViewportWidth, (float)ViewportHeight);
		Params->LightCount = (uint32)FilteredLights.size();
		Params->scale = 32.0f / log2(FarClip / NearClip);
		Params->bias = -(32.0f * log2(NearClip) / log2(FarClip / NearClip));
		Params->NearClip = NearClip;
		Ctx->Unmap(ParamsCB, 0);
	}

	// Reset atomic counter
	uint32 zero = 0;
	UINT clearColor[4] = { 0, 0, 0, 0 };
	Ctx->ClearUnorderedAccessViewUint(IndexCounter.GetUAV(), clearColor);

	Ctx->CSSetShader(LightCullingCS, nullptr, 0);
	Ctx->CSSetConstantBuffers(0, 1, &ParamsCB);

	ID3D11ShaderResourceView* NullLightSRVs[3] = { nullptr, nullptr, nullptr };
	Ctx->PSSetShaderResources(ESystemTexSlot::AllLights, 3, NullLightSRVs);
	Ctx->VSSetShaderResources(ESystemTexSlot::AllLights, 1, NullLightSRVs);

	ID3D11ShaderResourceView* SRVs[] = { DepthSRV, AllLights.GetSRV() };
	Ctx->CSSetShaderResources(0, 2, SRVs);

	ID3D11UnorderedAccessView* UAVs[] = { LightIndexList.GetUAV(), LightGrid.GetUAV(), IndexCounter.GetUAV() };
	Ctx->CSSetUnorderedAccessViews(0, 3, UAVs, nullptr);

	// Dispatch 3D clusters. Thread group size in shader is (4, 4, 4)
	uint32 DispatchX = (GridX + 3) / 4;
	uint32 DispatchY = (GridY + 3) / 4;
	uint32 DispatchZ = (GridZ + 3) / 4;
	Ctx->Dispatch(DispatchX, DispatchY, DispatchZ);

	ID3D11ShaderResourceView* NullSRVs[2] = { nullptr, nullptr };
	Ctx->CSSetShaderResources(0, 2, NullSRVs);
	ID3D11UnorderedAccessView* NullUAVs[3] = { nullptr, nullptr, nullptr };
	Ctx->CSSetUnorderedAccessViews(0, 3, NullUAVs, nullptr);
	Ctx->CSSetShader(nullptr, nullptr, 0);
}
