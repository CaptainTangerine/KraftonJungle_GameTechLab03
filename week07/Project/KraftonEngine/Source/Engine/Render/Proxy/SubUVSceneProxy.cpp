#include "Render/Proxy/SubUVSceneProxy.h"
#include "Component/SubUVComponent.h"
#include "Render/Pipeline/FrameContext.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Resource/MeshBufferManager.h"

// ============================================================
// FSubUVSceneProxy
// ============================================================
FSubUVSceneProxy::FSubUVSceneProxy(USubUVComponent* InComponent)
	: FBillboardSceneProxy(static_cast<UBillboardComponent*>(InComponent))
{
	bShowAABB = false;
}

FSubUVSceneProxy::~FSubUVSceneProxy()
{
	UVRegionCB.Release();
}

void FSubUVSceneProxy::UpdateMesh()
{
	USubUVComponent* Comp = GetSubUVComponent();

	// TexturedQuad (FVertexPNCT with UVs) for rendering
	MeshBuffer = &FMeshBufferManager::Get().GetMeshBuffer(EMeshShape::TexturedQuad);
	Pass = ERenderPass::AlphaBlend;

	// Set DiffuseSRV from particle resource
	ID3D11ShaderResourceView* DiffuseSRV = nullptr;
	const FParticleResource* Particle = Comp->GetParticle();
	if (Particle && Particle->IsLoaded())
	{
		DiffuseSRV = Particle->SRV;
	}

	SectionDraws.clear();
	if (MeshBuffer)
	{
		FMeshSectionDraw Draw;
		Draw.Shader = FShaderManager::Get().GetShader(EShaderType::SubUV);
		Draw.FirstIndex = 0;
		Draw.IndexCount = MeshBuffer->GetIndexBuffer().GetIndexCount();
		Draw.DiffuseSRV = DiffuseSRV;
		Draw.Blend = EBlendState::AlphaBlend;
		Draw.DepthStencil = EDepthStencilState::Default;
		Draw.Rasterizer = ERasterizerState::SolidBackCull;

		// MaterialCB[0] bind (UV region, b2 slot)
		Draw.MaterialCB[0].Bind<FSubUVRegionConstants>(&UVRegionCB, ECBSlot::PerShader0);

		SectionDraws.push_back(Draw);
	}
}

void FSubUVSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	USubUVComponent* Comp = GetSubUVComponent();
	bVisible = Comp->IsVisible();
	if (!bVisible) return;

	const FParticleResource* Particle = Comp->GetParticle();
	if (!Particle || !Particle->IsLoaded())
	{
		bVisible = false;
		return;
	}

	if (SectionDraws.empty()) return;

	// Update DiffuseSRV (may change during play)
	SectionDraws[0].DiffuseSRV = Particle->SRV;

	// Billboard matrix
	FVector BillboardForward = Frame.CameraForward * -1.0f;
	FMatrix RotMatrix;
	RotMatrix.SetAxes(BillboardForward, Frame.CameraRight, Frame.CameraUp);
	FMatrix BillboardMatrix = FMatrix::MakeScaleMatrix(Comp->GetWorldScale())
		* RotMatrix * FMatrix::MakeTranslationMatrix(Comp->GetWorldLocation());

	PerObjectConstants = FPerObjectConstants::FromWorldMatrix(BillboardMatrix);
	MarkPerObjectCBDirty();

	// Update UV region from frame index
	const uint32 Cols = Comp->GetColumns();
	const uint32 Rows = Comp->GetRows();
	if (Cols > 0 && Rows > 0)
	{
		const float FrameW = 1.0f / static_cast<float>(Cols);
		const float FrameH = 1.0f / static_cast<float>(Rows);
		const uint32 FrameIdx = Comp->GetFrameIndex();
		const uint32 Col = FrameIdx % Cols;
		const uint32 Row = FrameIdx / Cols;

		FSubUVRegionConstants& Region = SectionDraws[0].MaterialCB[0].As<FSubUVRegionConstants>();
		Region.U = Col * FrameW;
		Region.V = Row * FrameH;
		Region.Width = FrameW;
		Region.Height = FrameH;
	}
}

USubUVComponent* FSubUVSceneProxy::GetSubUVComponent() const
{
	return static_cast<USubUVComponent*>(Owner);
}
