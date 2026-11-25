#pragma once
#include "Component/SceneComponent.h"
#include "Math/Vector.h"

class ADirectionalLightActor;
class AAmbientLightActor;
class UDirectionalLightComponent;
class UAmbientLightComponent;
class UBillboardComponent;

// 시간에 따라 태양 방향/색상/강도, 주변광을 자동으로 갱신하는 컴포넌트.
// DaylightActor에 붙여 사용하며, 씬에 있는 DirectionalLight/AmbientLight를 자동으로 찾아 제어한다.

class UDaylightComponent : public USceneComponent
{
public:
    DECLARE_CLASS(UDaylightComponent, USceneComponent)

    UDaylightComponent();
    virtual ~UDaylightComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
    void ContributeSelectedVisuals(FScene& Scene) const override;

    void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
    void PostEditProperty(const char* PropertyName) override;
    void Serialize(FArchive& Ar) override;

    bool  bDaylightEnabled = true;

    float TimeOfDay  = 8.0f;   // 현재 시각 (0 ~ 24 h)
    float TimeScale  = 60.0f;  // 현실 1초 = 시뮬레이션 몇 초

    // PIE 실행 중에만 시간 자동 진행 (기본값)
    // bAutoAdvanceInEditor=true 로 설정하면 에디터에서도 진행
    bool  bAutoAdvanceTime     = true;
    bool  bAutoAdvanceInEditor = false;


    float SunMaxIntensity       = 3.0f;
    float AmbientMaxIntensity   = 0.6f;
    float AmbientNightIntensity = 0.15f;

    bool bOverrideSkyColor = true;  // Ambient Color를 뷰포트 배경으로 반영
    bool bShowOrbitDebug   = true;  // 공전궤도 와이어프레임 항상 표시

    void SetTimeOfDay(float InTime);
    float GetSunElevationDeg() const;
    FVector GetCurrentSunPosition() const { return CalculateSunPosition(); }

	UBillboardComponent* EnsureEditorBillboard();

private:
    void FindLightActors();
    void UpdateLighting();
    void DrawOrbitWireframe(FScene& Scene) const;

    // 24시간 완전체 공전: 태양 위치를 월드 정규화 벡터로 반환 (Z>0=지평선 위)
    // theta = (TimeOfDay/24)*2π - π/2
    //   midnight(0h) → Z=-1 (지평선 아래 최저점)
    //   6h          → 기즈모 Forward 방향 (지평선 교차)
    //   noon(12h)   → Z=+1 (천정, 기즈모 Up 방향)
    //   18h         → -Forward 방향 (지평선 교차)
    FVector  CalculateSunPosition() const;
    FVector4 CalculateSunColor(float SunElevation)     const;
    float    CalculateSunIntensity(const FVector& SunPos) const;
	FVector4 CalculateAmbientColor(float SunElevation) const;
    float    CalculateAmbientIntensity(const FVector& SunPos) const;


    
    TArray<UDirectionalLightComponent*> ControlledSuns;
    TArray<UAmbientLightComponent*>     ControlledAmbients;
};
