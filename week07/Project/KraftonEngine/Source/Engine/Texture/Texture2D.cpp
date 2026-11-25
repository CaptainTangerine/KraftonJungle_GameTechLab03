#include "Texture/Texture2D.h"
#include "Object/ObjectFactory.h"
#include "UI/EditorConsoleWidget.h"
#include "Platform/Paths.h"
#include "WICTextureLoader.h"

#include <d3d11.h>
#include <filesystem>

IMPLEMENT_CLASS(UTexture2D, UObject)

std::map<FString, UTexture2D*> UTexture2D::TextureCache;
UTexture2D* UTexture2D::DefaultWhite = nullptr;
UTexture2D* UTexture2D::DefaultBlack = nullptr;
UTexture2D* UTexture2D::DefaultNormal = nullptr;

UTexture2D::~UTexture2D()
{
	if (SRV)
	{
		if (TrackedTextureMemory > 0)
		{
			MemoryStats::SubTextureMemory(TrackedTextureMemory);
			TrackedTextureMemory = 0;
		}

		SRV->Release();
		SRV = nullptr;
	}

	// 캐시에서 제거
	auto It = TextureCache.find(SourceFilePath);
	if (It != TextureCache.end() && It->second == this)
	{
		TextureCache.erase(It);
	}

	if (this == DefaultWhite) DefaultWhite = nullptr;
	if (this == DefaultBlack) DefaultBlack = nullptr;
	if (this == DefaultNormal) DefaultNormal = nullptr;
}

void UTexture2D::ReleaseAllGPU()
{
	ReleaseDefaultTextures();

	// TextureCache의 모든 UTexture2D 객체를 파괴합니다.
	// 소멸자에서 SRV가 해제되므로 여기서 별도로 해제할 필요는 없지만,
	// 안전을 위해 파괴 전 SRV가 살아있는지 확인합니다.
	std::vector<UTexture2D*> ToDestroy;
	for (auto& [Path, Texture] : TextureCache)
	{
		if (Texture)
		{
			ToDestroy.push_back(Texture);
		}
	}
	TextureCache.clear();

	for (UTexture2D* Texture : ToDestroy)
	{
		UObjectManager::Get().DestroyObject(Texture);
	}
}

void UTexture2D::InitializeDefaultTextures(ID3D11Device* Device)
{
	auto CreateSolidColor = [&](uint32 color) -> UTexture2D* {
		D3D11_TEXTURE2D_DESC desc = {};
		desc.Width = 1;
		desc.Height = 1;
		desc.MipLevels = 1;
		desc.ArraySize = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.SampleDesc.Count = 1;
		desc.Usage = D3D11_USAGE_IMMUTABLE;
		desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

		D3D11_SUBRESOURCE_DATA data = {};
		data.pSysMem = &color;
		data.SysMemPitch = 4;

		ID3D11Texture2D* tex = nullptr;
		Device->CreateTexture2D(&desc, &data, &tex);
		
		ID3D11ShaderResourceView* srv = nullptr;
		Device->CreateShaderResourceView(tex, nullptr, &srv);

		UTexture2D* texture = UObjectManager::Get().CreateObject<UTexture2D>();
		texture->SRV = srv;
		texture->Width = 1;
		texture->Height = 1;
		texture->SourceFilePath = "Default";
		
		tex->Release();
		return texture;
	};

	DefaultWhite = CreateSolidColor(0xFFFFFFFF);
	DefaultBlack = CreateSolidColor(0xFF000000);
	DefaultNormal = CreateSolidColor(0xFFFF8080); // 128, 128, 255 (Flat normal)
}

void UTexture2D::ReleaseDefaultTextures()
{
	if (DefaultWhite)
	{
		UObjectManager::Get().DestroyObject(DefaultWhite);
		DefaultWhite = nullptr;
	}
	if (DefaultBlack)
	{
		UObjectManager::Get().DestroyObject(DefaultBlack);
		DefaultBlack = nullptr;
	}
	if (DefaultNormal)
	{
		UObjectManager::Get().DestroyObject(DefaultNormal);
		DefaultNormal = nullptr;
	}
}

UTexture2D* UTexture2D::LoadFromFile(const FString& FilePath, ID3D11Device* Device)
{
	if (FilePath.empty() || !Device) return nullptr;

	// 캐시 히트
	auto It = TextureCache.find(FilePath);
	if (It != TextureCache.end())
	{
		return It->second;
	}

	// 새 UTexture2D 생성
	UTexture2D* Texture = UObjectManager::Get().CreateObject<UTexture2D>();
	if (!Texture->LoadInternal(FilePath, Device))
	{
		UObjectManager::Get().DestroyObject(Texture);
		return nullptr;
	}

	TextureCache[FilePath] = Texture;
	return Texture;
}

bool UTexture2D::LoadInternal(const FString& FilePath, ID3D11Device* Device)
{
	//std::filesystem::path TexPath(FilePath);
	//std::wstring WidePath = TexPath.wstring();
	std::wstring FullPath = FPaths::Combine(FPaths::RootDir(), FPaths::ToWide(FilePath));

	ID3D11Resource* Resource = nullptr;
	HRESULT hr = DirectX::CreateWICTextureFromFileEx(
		Device, FullPath.c_str(),
		0,                                    // maxsize
		D3D11_USAGE_DEFAULT,                  // usage
		D3D11_BIND_SHADER_RESOURCE,           // bindFlags
		0,                                    // cpuAccessFlags
		0,                                    // miscFlags
		DirectX::WIC_LOADER_IGNORE_SRGB,     // sRGB 메타데이터 무시 → UNORM 포맷 강제
		&Resource, &SRV);

	if (FAILED(hr))
	{
		UE_LOG("Failed to load texture: %s", FilePath.c_str());
		return false;
	}

	// 텍스처 크기 추출
	if (Resource)
	{
		TrackedTextureMemory = MemoryStats::CalculateTextureMemory(Resource);

		ID3D11Texture2D* Tex2D = nullptr;
		if (SUCCEEDED(Resource->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&Tex2D)))
		{
			D3D11_TEXTURE2D_DESC Desc;
			Tex2D->GetDesc(&Desc);
			Width = Desc.Width;
			Height = Desc.Height;
			Tex2D->Release();
		}

		if (TrackedTextureMemory > 0)
		{
			MemoryStats::AddTextureMemory(TrackedTextureMemory);
		}
		Resource->Release();
	}

	SourceFilePath = FilePath;
	return true;
}
