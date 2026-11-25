#pragma once
#include "BillboardComponent.h"

// ====================================================================
// CylindricalBillboardComponent
// 
// 빌보드인데, Yaw(좌우)만 돌아가도록 Pitch와 Roll이 고정된 컴포넌트입니다.
// ====================================================================
class UCylindricalBillboardComponent : public UBillboardComponent
{
public:
	DECLARE_CLASS(UCylindricalBillboardComponent, UBillboardComponent)

	void Serialize(FArchive& Ar) override;
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction);
	FMatrix ComputeBillboardMatrix(const FVector& CameraForward) const;
	FPrimitiveSceneProxy* CreateSceneProxy() override;

	void SetBillboardAxis(const FVector& Axis) { BillboardAxis = Axis; }
	FVector GetBillboardAxis() const { return BillboardAxis; }

protected:
	FVector BillboardAxis = FVector(0, 0, 1);
};
