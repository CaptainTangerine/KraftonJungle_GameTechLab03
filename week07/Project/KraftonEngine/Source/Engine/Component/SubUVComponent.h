#pragma once

#include "BillboardComponent.h"
#include "Core/ResourceTypes.h"
#include "Object/FName.h"

class USubUVComponent : public UBillboardComponent
{
public:
	DECLARE_CLASS(USubUVComponent, UBillboardComponent)

	USubUVComponent();
	~USubUVComponent() override = default;

	// --- Particle Resource ---
	// FName 키로 ResourceManager에서 FParticleResource*를 찾아 캐싱
	void SetParticle(const FName& InParticleName);
	const FParticleResource* GetParticle() const { return CachedParticle; }
	const FName& GetParticleName() const { return ParticleName; }

	// --- SubUV Frame ---
	void SetFrameIndex(uint32 InIndex);
	uint32 GetFrameIndex() const { return FrameIndex; }
	uint32 GetColumns() const { return static_cast<uint32>(Columns); }
	uint32 GetRows() const { return static_cast<uint32>(Rows); }
	uint32 GetStartFrameIndex() const { return StartFrameIndex; }
	uint32 GetEndFrameIndex() const { return EndFrameIndex; }
	uint32 GetTotalFrameCount() const;
	void SetStartFrameDisplay(int32 InDisplayFrame);
	void SetEndFrameDisplay(int32 InDisplayFrame);

	// --- Playback ---
	void SetFrameRate(float InFPS);
	void SetLoop(bool bInLoop) { bLoop = bInLoop; }
	bool IsLoop()     const { return bLoop; }
	bool IsFinished() const { return !bLoop && bIsExecute; }
	void Play();

	// Sprite Size(Width/Height)는 UBillboardComponent로 끌어올림 — 상속받아 사용.

	// --- Property / Serialization ---
	void GetEditableProperties(TArray<FPropertyDescriptor>& OutProps) override;
	void PostEditProperty(const char* PropertyName) override;

	void Serialize(FArchive& Ar) override;
	void PostDuplicate() override;

	FPrimitiveSceneProxy* CreateSceneProxy() override;
	void UpdateWorldAABB() const override;

protected:
	void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction& ThisTickFunction) override;
private:
	void ClampFrameSettings();
	void SyncFrameDisplayFromInternal();

	FName ParticleName;
	FParticleResource* CachedParticle = nullptr; // ResourceManager 소유, 여기선 참조만

	uint32 FrameIndex = 0;
	uint32 StartFrameIndex = 0;
	uint32 EndFrameIndex = 0;
	int32 Columns = 1;
	int32 Rows = 1;
	int32 StartFrameDisplay = 1;
	int32 EndFrameDisplay = 1;
	float  PlayRate = 30.0f; // 초당 프레임 수
	float  TimeAccumulator = 0.0f;

	bool bLoop = true;
	bool bIsExecute = false;
};
