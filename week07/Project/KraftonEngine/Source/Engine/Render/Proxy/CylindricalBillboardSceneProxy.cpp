#include "Render/Proxy/CylindricalBillboardSceneProxy.h"
#include "Component/CylindricalBillboardComponent.h"
#include "Render/Resource/ShaderManager.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Render/Pipeline/FrameContext.h"
#include "GameFramework/AActor.h"
#include "Materials/Material.h"
#include "Texture/Texture2D.h"

// ============================================================
// FCylindricalBillboardSceneProxy
// ============================================================
FCylindricalBillboardSceneProxy::FCylindricalBillboardSceneProxy(UCylindricalBillboardComponent* InComponent)
	: FBillboardSceneProxy(InComponent)
{
}

void FCylindricalBillboardSceneProxy::UpdateMesh()
{
	FBillboardSceneProxy::UpdateMesh();

	if (!SectionDraws.empty())
	{
		SectionDraws[0].Rasterizer = ERasterizerState::SolidNoCull;
	}
}

void FCylindricalBillboardSceneProxy::UpdatePerViewport(const FFrameContext& Frame)
{
	UCylindricalBillboardComponent* Comp = static_cast<UCylindricalBillboardComponent*>(Owner);
	bVisible = Comp->IsVisible();
	if (!bVisible) return;

	if (SectionDraws.empty()) return;

	// Update DiffuseSRV (material or texture may have changed)
	UMaterial* Mat = Comp->GetMaterial();
	if (Mat)
	{
		UTexture2D* DiffuseTex = nullptr;
		if (Mat->GetTextureParameter("DiffuseTexture", DiffuseTex))
		{
			SectionDraws[0].DiffuseSRV = DiffuseTex->GetSRV();
		}
	}

	FVector BillboardPos = Comp->GetWorldLocation();
	FVector BillboardForward = Frame.CameraForward * 1.0f;

	// 로컬 축 구하기
	FVector LocalAxis = Comp->GetBillboardAxis();
	if (LocalAxis.Dot(LocalAxis) < 0.0001f)
	{
		LocalAxis = FVector(0, 0, 1);
	}
	else
	{
		LocalAxis.Normalize();
	}

	// 빌보드 이전의 순수 월드 회전축 계산
	FMatrix NonBillboardWorldMatrix;
	if (Comp->GetParent())
	{
		NonBillboardWorldMatrix = Comp->GetRelativeMatrix() * Comp->GetParent()->GetWorldMatrix();
	}
	else
	{
		NonBillboardWorldMatrix = Comp->GetRelativeMatrix();
	}

	FVector WorldAxis = NonBillboardWorldMatrix.TransformVector(LocalAxis).Normalized();

	// 카메라 방향을 축에 수직인 평면에 투영
	FVector Forward = BillboardForward - (WorldAxis * BillboardForward.Dot(WorldAxis));
	if (Forward.Dot(Forward) < 0.0001f)
	{
		FVector TempUp = (std::abs(WorldAxis.Dot(FVector(0, 0, 1))) < 0.99f) ? FVector(0, 0, 1) : FVector(0, 1, 0);
		Forward = TempUp.Cross(WorldAxis).Normalized();
	}
	else
	{
		Forward.Normalize();
	}

	FVector Right = WorldAxis.Cross(Forward).Normalized();
	FVector Up = WorldAxis;

	FMatrix RotMatrix;
	RotMatrix.SetAxes(Forward, Right, Up);

	FMatrix BillboardMatrix = FMatrix::MakeScaleMatrix(Comp->GetWorldScale())
		* RotMatrix * FMatrix::MakeTranslationMatrix(BillboardPos);

	PerObjectConstants = FPerObjectConstants::FromWorldMatrix(BillboardMatrix);
	PerObjectConstants.Color = Comp->GetTintColor();
	MarkPerObjectCBDirty();
}
