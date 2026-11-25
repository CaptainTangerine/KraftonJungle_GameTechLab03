#pragma once

#include "Core/CoreTypes.h"
#include "Core/Singleton.h"
#include "Core/ResourceTypes.h"
#include "Object/FName.h"

// 리소스를 관리하는 싱글턴.
// 정해진 에셋 디렉터리를 스캔하고, GPU 리소스를 로드/캐싱합니다.
// 컴포넌트는 소유하지 않고 포인터로 공유 데이터를 참조합니다.

struct ID3D11Device;

class FResourceManager : public TSingleton<FResourceManager>
{
	friend class TSingleton<FResourceManager>;

public:
	// 호환용 래퍼. Path는 사용하지 않고 기본 에셋 디렉터리에서 자동 등록합니다.
	void LoadFromFile(const FString& Path, ID3D11Device* InDevice);

	// 기본 에셋 디렉터리 스캔 후 GPU 리소스 생성
	void LoadDefaultResources(ID3D11Device* InDevice);

	// GPU 리소스 로드 (Device 필요)
	bool LoadGPUResources(ID3D11Device* Device);

	// 모든 GPU 리소스 해제
	void ReleaseGPUResources();

	// --- Font ---
	FFontResource* FindFont(const FName& FontName);
	const FFontResource* FindFont(const FName& FontName) const;
	void RegisterFont(const FName& FontName, const FString& InPath, uint32 Columns = 16, uint32 Rows = 16);

	// --- Font names ---
	TArray<FString> GetFontNames() const;

	// --- Particle ---
	FParticleResource* FindParticle(const FName& ParticleName);
	const FParticleResource* FindParticle(const FName& ParticleName) const;
	void RegisterParticle(const FName& ParticleName, const FString& InPath, uint32 Columns = 1, uint32 Rows = 1);

	// --- Particle names ---
	TArray<FString> GetParticleNames() const;

	// --- Texture (단일 정적 이미지, 1x1 atlas) ---
	FTextureResource* FindTexture(const FName& TextureName);
	const FTextureResource* FindTexture(const FName& TextureName) const;
	void RegisterTexture(const FName& TextureName, const FString& InPath);

	// --- Texture names ---
	TArray<FString> GetTextureNames() const;

private:
	FResourceManager() = default;
	~FResourceManager() { ReleaseGPUResources(); }

	void RegisterDefaultResources();
	void RegisterImagesInDirectory(const FString& RelativeDirectory, bool bRecursive, bool bAsParticle);
	bool IsSupportedImageExtension(const FString& Extension) const;

	TMap<FString, FFontResource>     FontResources;
	TMap<FString, FParticleResource> ParticleResources;
	TMap<FString, FTextureResource>  TextureResources;
};
