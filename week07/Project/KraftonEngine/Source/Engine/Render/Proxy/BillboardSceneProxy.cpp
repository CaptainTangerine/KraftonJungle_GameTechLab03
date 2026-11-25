#include "Render/Proxy/BillboardSceneProxy.h"
#include "Component/BillboardComponent.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Render/Pipeline/FrameContext.h"
#include "GameFramework/AActor.h"
#include "GameFramework/World.h"
#include "Materials/Material.h"
#include "Texture/Texture2D.h"

// ============================================================
// FBillboardSceneProxy
// ============================================================
FBillboardSceneProxy::FBillboardSceneProxy(UBillboardComponent* InComponent)
	: FPrimitiveSceneProxy(InComponent)
{
	bPerViewportUpdate = true;
	bShowAABB = false;
}

UBillboardComponent* FBillboardSceneProxy::GetBillboardComponent() const
{
	return static_cast<UBillboardComponent*>(Owner);
}

// ============================================================
// UpdateMesh — TexturedQuad + Material shader/states
// ============================================================
void FBillboardSceneProxy::UpdateMesh()
{
	UBillboardComponent* Comp = GetBillboardComponent();
	UMaterial* Mat = Comp ? Comp->GetMaterial() : nullptr;

	MeshBuffer = nullptr;
	ID3D11ShaderResourceView* LocalDiffuseSRV = nullptr;
	FShader* Shader = nullptr;

	if (Mat)
	{
		// TexturedQuad (FVertexPNCT with UVs)
		MeshBuffer = &FMeshBufferManager::Get().GetMeshBuffer(EMeshShape::TexturedQuad);
		
		Shader = Mat->GetShader();
		if (!Shader)
		{
			Shader = FShaderManager::Get().GetShader(EShaderType::Billboard);
		}

		Pass = Mat->GetRenderPass();

		UTexture2D* DiffuseTex = nullptr;
		if (Mat->GetTextureParameter("DiffuseTexture", DiffuseTex))
		{
			LocalDiffuseSRV = DiffuseTex->GetSRV();
		}
	}
	else
	{
		MeshBuffer = Owner->GetMeshBuffer();
		Shader = FShaderManager::Get().GetShader(EShaderType::Primitive);
		Pass = ERenderPass::Opaque;
	}

	// 언리얼 방식: 에디터 전용 아이콘 빌보드는 Fog/PostProcess 이후의
	// EditorOverlay 패스에서 그려 Height Fog 등 post-effect의 영향을 받지 않게 한다.
	if (Comp && Comp->IsEditorOnlyComponent())
	{
		Pass = ERenderPass::EditorOverlay;
	}

	SectionDraws.clear();
	if (MeshBuffer)
	{
		FMeshSectionDraw Draw;
		Draw.Shader = Shader;
		Draw.FirstIndex = 0;
		Draw.IndexCount = MeshBuffer->GetIndexBuffer().GetIndexCount();
		Draw.DiffuseSRV = LocalDiffuseSRV;

		if (Mat)
		{
			Draw.Blend = Mat->GetBlendState();
			Draw.DepthStencil = Mat->GetDepthStencilState();
			Draw.Rasterizer = Mat->GetRasterizerState();
			
			Mat->GetGPUBufferBindingBySlot(ECBSlot::PerShader0, Draw.MaterialCB[0]);
			Mat->GetGPUBufferBindingBySlot(ECBSlot::PerShader1, Draw.MaterialCB[1]);
		}
		else
		{
			Draw.Blend = EBlendState::Opaque;
			Draw.DepthStencil = EDepthStencilState::Default;
			Draw.Rasterizer = ERasterizerState::SolidBackCull;
		}
		SectionDraws.push_back(Draw);
	}
}

// ============================================================
// UpdatePerViewport — 뷰포트 카메라 기반 빌보드 행렬 갱신
// ============================================================
void FBillboardSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	UBillboardComponent* Comp = GetBillboardComponent();
	bVisible = Comp->IsVisible();
	if (bVisible && Comp->IsEditorOnlyComponent())
	{
		//에디터에서 만들어진 빌보드는 PIE에서는 보이지 않도록 합니다.
		UWorld* World = Comp->GetWorld();
		if (World && World->HasBegunPlay())
		{
			bVisible = false;
		}
	}
	if (!bVisible) return;

	AActor* OwnerActor = Comp->GetOwner();
	if (OwnerActor && !OwnerActor->IsVisible())
	{
		bVisible = false;
		return;
	}

	// Update DiffuseSRV in SectionDraws if needed (material or texture may have changed)
	if (!SectionDraws.empty())
	{
		UMaterial* Mat = Comp->GetMaterial();
		if (Mat)
		{
			UTexture2D* DiffuseTex = nullptr;
			if (Mat->GetTextureParameter("DiffuseTexture", DiffuseTex))
			{
				SectionDraws[0].DiffuseSRV = DiffuseTex->GetSRV();
			}
		}
	}

	// Frame 카메라 벡터로 per-view 빌보드 행렬 계산
	FVector BillboardForward = Frame.CameraForward * -1.0f;
	FMatrix RotMatrix;
	RotMatrix.SetAxes(BillboardForward, Frame.CameraRight, Frame.CameraUp);
	FMatrix BillboardMatrix = FMatrix::MakeScaleMatrix(Comp->GetWorldScale())
		* RotMatrix * FMatrix::MakeTranslationMatrix(Comp->GetWorldLocation());

	PerObjectConstants = FPerObjectConstants::FromWorldMatrix(BillboardMatrix);
	PerObjectConstants.Color = Comp->GetTintColor(); //안 쓰고 있던 Color 멤버 사용
	MarkPerObjectCBDirty();
}
