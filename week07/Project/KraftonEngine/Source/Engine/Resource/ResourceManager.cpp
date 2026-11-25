#include "Resource/ResourceManager.h"
#include "Platform/Paths.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <d3d11.h>
#include "DDSTextureLoader.h"
#include "WICTextureLoader.h"
#include "UI/EditorConsoleWidget.h"
#include "Profiling/MemoryStats.h"

namespace
{
	FString ToLower(FString Value)
	{
		for (char& Char : Value)
		{
			Char = static_cast<char>(::tolower(static_cast<unsigned char>(Char)));
		}
		return Value;
	}

	FString ToResourceRelativePath(const std::filesystem::path& Path)
	{
		std::filesystem::path Root(FPaths::RootDir());
		std::filesystem::path Relative = Path.lexically_relative(Root);
		if (Relative.empty())
		{
			Relative = Path;
		}
		return FPaths::ToUtf8(Relative.generic_wstring());
	}
}

void FResourceManager::LoadFromFile(const FString& Path, ID3D11Device* InDevice)
{
	(void)Path;
	LoadDefaultResources(InDevice);
}

void FResourceManager::LoadDefaultResources(ID3D11Device* InDevice)
{
	RegisterDefaultResources();

	if (LoadGPUResources(InDevice))
	{
		UE_LOG("Complete Load Resources!");
	}
	else
	{
		UE_LOG("Failed to Load Resources...");
	}
}

void FResourceManager::RegisterDefaultResources()
{
	ReleaseGPUResources();
	FontResources.clear();
	ParticleResources.clear();
	TextureResources.clear();

	RegisterFont(FName("Default"), "Asset/Font/FontAtlas.dds", 128, 128);
	RegisterImagesInDirectory("Asset/Particle", true, true);
	RegisterImagesInDirectory("Asset/Textures", true, false);
	RegisterImagesInDirectory("Asset/Editor/Icons", true, false);
	RegisterParticle(FName("Lens_Flare"), "Asset/Textures/Lens_Flare.png", 3, 3);
}

void FResourceManager::RegisterImagesInDirectory(const FString& RelativeDirectory, bool bRecursive, bool bAsParticle)
{
	std::filesystem::path FullDirectory = std::filesystem::path(FPaths::RootDir()) / FPaths::ToWide(RelativeDirectory);
	if (!std::filesystem::exists(FullDirectory) || !std::filesystem::is_directory(FullDirectory))
	{
		return;
	}

	auto RegisterPath = [&](const std::filesystem::path& EntryPath)
	{
		if (!std::filesystem::is_regular_file(EntryPath))
		{
			return;
		}

		FString Extension = FPaths::ToUtf8(EntryPath.extension().wstring());
		if (!IsSupportedImageExtension(Extension))
		{
			return;
		}

		FString Name = FPaths::ToUtf8(EntryPath.stem().wstring());
		FString RelativePath = ToResourceRelativePath(EntryPath);
		if (bAsParticle)
		{
			if (ParticleResources.find(Name) != ParticleResources.end())
			{
				UE_LOG("Duplicate particle resource ignored: %s", Name.c_str());
				return;
			}
			RegisterParticle(FName(Name), RelativePath);
		}
		else
		{
			if (TextureResources.find(Name) != TextureResources.end())
			{
				UE_LOG("Duplicate texture resource ignored: %s", Name.c_str());
				return;
			}
			RegisterTexture(FName(Name), RelativePath);
		}
	};

	if (bRecursive)
	{
		for (const auto& Entry : std::filesystem::recursive_directory_iterator(FullDirectory))
		{
			RegisterPath(Entry.path());
		}
	}
	else
	{
		for (const auto& Entry : std::filesystem::directory_iterator(FullDirectory))
		{
			RegisterPath(Entry.path());
		}
	}
}

bool FResourceManager::IsSupportedImageExtension(const FString& Extension) const
{
	const FString LowerExt = ToLower(Extension);
	return LowerExt == ".dds"
		|| LowerExt == ".png"
		|| LowerExt == ".jpg"
		|| LowerExt == ".jpeg"
		|| LowerExt == ".bmp"
		|| LowerExt == ".tga";
}

bool FResourceManager::LoadGPUResources(ID3D11Device* Device)
{
	if (!Device)
	{
		return false;
	}

	auto LoadSRV = [&](FTextureAtlasResource& Resource) -> bool
	{
		if (Resource.SRV)
		{
			if (Resource.TrackedMemoryBytes > 0)
			{
				MemoryStats::SubTextureMemory(Resource.TrackedMemoryBytes);
				Resource.TrackedMemoryBytes = 0;
			}
			Resource.SRV->Release();
			Resource.SRV = nullptr;
		}

		std::wstring FullPath = FPaths::Combine(FPaths::RootDir(), FPaths::ToWide(Resource.Path));

		// 확장자에 따라 DDS / WIC 로더 분기
		std::filesystem::path Ext = std::filesystem::path(Resource.Path).extension();
		FString ExtStr = ToLower(Ext.string());

		HRESULT hr;
		if (ExtStr == ".dds")
		{
			hr = DirectX::CreateDDSTextureFromFileEx(
				Device,
				FullPath.c_str(),
				0,
				D3D11_USAGE_IMMUTABLE,
				D3D11_BIND_SHADER_RESOURCE,
				0, 0,
				DirectX::DDS_LOADER_DEFAULT,
				nullptr,
				&Resource.SRV
			);
		}
		else
		{
			// .png/.jpg/.bmp/.tga 등 — WIC 경유
			hr = DirectX::CreateWICTextureFromFileEx(
				Device,
				FullPath.c_str(),
				0,
				D3D11_USAGE_IMMUTABLE,
				D3D11_BIND_SHADER_RESOURCE,
				0, 0,
				// 이 버전의 DirectXTK 에는 PREMULTIPLY_ALPHA 플래그가 없다.
				// straight-alpha 보간으로 생기는 검은 헤일로는 PS 측 알파 컷오프(0.5) 로 회피.
				DirectX::WIC_LOADER_FORCE_RGBA32,
				nullptr,
				&Resource.SRV
			);
		}
		if (FAILED(hr) || !Resource.SRV)
		{
			return false;
		}

		ID3D11Resource* TextureResource = nullptr;
		Resource.SRV->GetResource(&TextureResource);
		Resource.TrackedMemoryBytes = MemoryStats::CalculateTextureMemory(TextureResource);
		if (TextureResource)
		{
			TextureResource->Release();
		}

		if (Resource.TrackedMemoryBytes > 0)
		{
			MemoryStats::AddTextureMemory(Resource.TrackedMemoryBytes);
		}

		return true;
	};

	for (auto& [Key, Resource] : FontResources)
	{
		if (!LoadSRV(Resource)) return false;
	}

	for (auto& [Key, Resource] : ParticleResources)
	{
		if (!LoadSRV(Resource)) return false;
	}

	for (auto& [Key, Resource] : TextureResources)
	{
		if (!LoadSRV(Resource)) return false;
	}

	return true;
}

void FResourceManager::ReleaseGPUResources()
{
	for (auto& [Key, Resource] : FontResources)
	{
		if (Resource.TrackedMemoryBytes > 0)
		{
			MemoryStats::SubTextureMemory(Resource.TrackedMemoryBytes);
			Resource.TrackedMemoryBytes = 0;
		}
		if (Resource.SRV) { Resource.SRV->Release(); Resource.SRV = nullptr; }
	}
	for (auto& [Key, Resource] : ParticleResources)
	{
		if (Resource.TrackedMemoryBytes > 0)
		{
			MemoryStats::SubTextureMemory(Resource.TrackedMemoryBytes);
			Resource.TrackedMemoryBytes = 0;
		}
		if (Resource.SRV) { Resource.SRV->Release(); Resource.SRV = nullptr; }
	}
	for (auto& [Key, Resource] : TextureResources)
	{
		if (Resource.TrackedMemoryBytes > 0)
		{
			MemoryStats::SubTextureMemory(Resource.TrackedMemoryBytes);
			Resource.TrackedMemoryBytes = 0;
		}
		if (Resource.SRV) { Resource.SRV->Release(); Resource.SRV = nullptr; }
	}
}

// --- Font ---
FFontResource* FResourceManager::FindFont(const FName& FontName)
{
	auto It = FontResources.find(FontName.ToString());
	return (It != FontResources.end()) ? &It->second : nullptr;
}

const FFontResource* FResourceManager::FindFont(const FName& FontName) const
{
	auto It = FontResources.find(FontName.ToString());
	return (It != FontResources.end()) ? &It->second : nullptr;
}

void FResourceManager::RegisterFont(const FName& FontName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	FFontResource Resource;
	Resource.Name    = FontName;
	Resource.Path    = InPath;
	Resource.Columns = Columns;
	Resource.Rows    = Rows;
	Resource.SRV     = nullptr;
	FontResources[FontName.ToString()] = Resource;
}

// --- Particle ---
FParticleResource* FResourceManager::FindParticle(const FName& ParticleName)
{
	auto It = ParticleResources.find(ParticleName.ToString());
	return (It != ParticleResources.end()) ? &It->second : nullptr;
}

const FParticleResource* FResourceManager::FindParticle(const FName& ParticleName) const
{
	auto It = ParticleResources.find(ParticleName.ToString());
	return (It != ParticleResources.end()) ? &It->second : nullptr;
}

void FResourceManager::RegisterParticle(const FName& ParticleName, const FString& InPath, uint32 Columns, uint32 Rows)
{
	FParticleResource Resource;
	Resource.Name    = ParticleName;
	Resource.Path    = InPath;
	Resource.Columns = Columns;
	Resource.Rows    = Rows;
	Resource.SRV     = nullptr;
	ParticleResources[ParticleName.ToString()] = Resource;
}

TArray<FString> FResourceManager::GetFontNames() const
{
	TArray<FString> Names;
	Names.reserve(FontResources.size());
	for (const auto& [Key, _] : FontResources)
	{
		Names.push_back(Key);
	}
	std::sort(Names.begin(), Names.end());
	return Names;
}

TArray<FString> FResourceManager::GetParticleNames() const
{
	TArray<FString> Names;
	Names.reserve(ParticleResources.size());
	for (const auto& [Key, _] : ParticleResources)
	{
		Names.push_back(Key);
	}
	std::sort(Names.begin(), Names.end());
	return Names;
}

// --- Texture ---
FTextureResource* FResourceManager::FindTexture(const FName& TextureName)
{
	auto It = TextureResources.find(TextureName.ToString());
	return (It != TextureResources.end()) ? &It->second : nullptr;
}

const FTextureResource* FResourceManager::FindTexture(const FName& TextureName) const
{
	auto It = TextureResources.find(TextureName.ToString());
	return (It != TextureResources.end()) ? &It->second : nullptr;
}

void FResourceManager::RegisterTexture(const FName& TextureName, const FString& InPath)
{
	FTextureResource Resource;
	Resource.Name    = TextureName;
	Resource.Path    = InPath;
	Resource.Columns = 1;
	Resource.Rows    = 1;
	Resource.SRV     = nullptr;
	TextureResources[TextureName.ToString()] = Resource;
}

TArray<FString> FResourceManager::GetTextureNames() const
{
	TArray<FString> Names;
	Names.reserve(TextureResources.size());
	for (const auto& [Key, _] : TextureResources)
	{
		Names.push_back(Key);
	}
	std::sort(Names.begin(), Names.end());
	return Names;
}
