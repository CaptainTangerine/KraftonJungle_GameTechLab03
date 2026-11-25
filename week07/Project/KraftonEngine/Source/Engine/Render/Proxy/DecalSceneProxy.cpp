#include "Render/Proxy/DecalSceneProxy.h"

#include "Component/DecalComponent.h"
#include "Render/Resource/ShaderManager.h"

#include "Materials/Material.h"
#include "Texture/Texture2D.h"

namespace
{
	struct FDecalConstants
	{
		FMatrix WorldToDecal;
		FVector4 Color;
	};
}

FDecalSceneProxy::FDecalSceneProxy(UDecalComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	DecalCB = new FConstantBuffer();
	// 최초 1회 초기화
	UpdateMesh();
}

FDecalSceneProxy::~FDecalSceneProxy()
{
	if (DecalCB)
	{
		DecalCB->Release();
		delete DecalCB;
		DecalCB = nullptr;
	}
}

UDecalComponent* FDecalSceneProxy::GetDecalComponent() const
{
	return static_cast<UDecalComponent*>(Owner);
}

void FDecalSceneProxy::UpdateMaterial()
{
	UDecalComponent* DecalComp = GetDecalComponent();
	if (!DecalComp)
	{
		return;
	}

	DecalMaterial = DecalComp->GetMaterial();
	ID3D11ShaderResourceView* DiffuseSRV = nullptr;

	if (DecalMaterial)
	{
		UTexture2D* DiffuseTex = nullptr;
		if (DecalMaterial->GetTextureParameter("DiffuseTexture", DiffuseTex))
		{
			DiffuseSRV = DiffuseTex->GetSRV();
		}
	}

	if (!SectionDraws.empty())
	{
		SectionDraws[0].DiffuseSRV = DiffuseSRV;

		auto& CB = SectionDraws[0].MaterialCB[0].Bind<FDecalConstants>(DecalCB, ECBSlot::PerShader0);
		CB.WorldToDecal = DecalComp->GetWorldMatrix().GetInverse();
		CB.Color = DecalComp->GetColor();
	}
}

void FDecalSceneProxy::UpdateMesh()
{
	MeshBuffer = nullptr;
	SectionDraws.clear();

	FMeshSectionDraw Draw;
	// Decal은 Receiver의 MeshBuffer를 빌려쓰므로 자체 IndexCount는 0으로 둔다 (BuildDecalCommand에서 Receiver것을 사용)
	Draw.FirstIndex = 0;
	Draw.IndexCount = 0;
	Draw.DiffuseSRV = nullptr;

	if (DecalMaterial && DecalMaterial->GetShader())
	{
		Draw.Shader = DecalMaterial->GetShader();
		Pass = DecalMaterial->GetRenderPass();

		// 머티리얼 기반 렌더 상태 전파
		Draw.Blend = DecalMaterial->GetBlendState();
		Draw.DepthStencil = DecalMaterial->GetDepthStencilState();
		Draw.Rasterizer = DecalMaterial->GetRasterizerState();

		DecalMaterial->GetGPUBufferBindingBySlot(ECBSlot::PerShader0, Draw.MaterialCB[0]);
		DecalMaterial->GetGPUBufferBindingBySlot(ECBSlot::PerShader1, Draw.MaterialCB[1]);
	}
	else
	{
		Draw.Shader = FShaderManager::Get().GetShader(EShaderType::Decal);
		Pass = ERenderPass::Decal;

		// 기본 상태
		Draw.Blend = EBlendState::Multiply;
		Draw.DepthStencil = EDepthStencilState::DepthReadOnly;
		Draw.Rasterizer = ERasterizerState::SolidNoCull;
	}

	SectionDraws.push_back(Draw);
	UpdateMaterial();

	bSupportsOutline = false;
}
