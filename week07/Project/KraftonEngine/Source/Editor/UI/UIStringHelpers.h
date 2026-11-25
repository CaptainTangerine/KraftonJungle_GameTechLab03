#pragma once

#include "Core/CoreTypes.h"
#include "Engine/Platform/Paths.h"

#include <algorithm>
#include <cstring>
#include <filesystem>

namespace FUIStringHelpers
{
	inline FString RemoveExtension(const FString& InPath)
	{
		const size_t DotPos = InPath.find_last_of('.');
		if (DotPos == FString::npos)
		{
			return InPath;
		}

		return InPath.substr(0, DotPos);
	}

	inline FString GetStemFromPath(const FString& InPath)
	{
		const size_t SlashPos = InPath.find_last_of("/\\");
		const FString FileName = (SlashPos == FString::npos) ? InPath : InPath.substr(SlashPos + 1);
		return RemoveExtension(FileName);
	}

	inline FString MakeRootRelativePath(const FString& InPath)
	{
		if (InPath.empty())
		{
			return InPath;
		}

		FString NormalizedPath = InPath;
		std::replace(NormalizedPath.begin(), NormalizedPath.end(), '\\', '/');

		FString RootPath = FPaths::ToUtf8(FPaths::RootDir());
		std::replace(RootPath.begin(), RootPath.end(), '\\', '/');
		if (!RootPath.empty() && RootPath.back() != '/')
		{
			RootPath.push_back('/');
		}

		if (!RootPath.empty()
			&& NormalizedPath.size() >= RootPath.size()
			&& _strnicmp(NormalizedPath.c_str(), RootPath.c_str(), RootPath.size()) == 0)
		{
			return NormalizedPath.substr(RootPath.size());
		}

		return NormalizedPath;
	}

	inline FString MakeProjectRelativePath(const std::filesystem::path& InAbsolutePath)
	{
		const std::filesystem::path RootPath = std::filesystem::path(FPaths::RootDir()).lexically_normal();
		const std::filesystem::path RelativePath = InAbsolutePath.lexically_relative(RootPath);
		const std::wstring RelativePathString = RelativePath.generic_wstring();
		if (RelativePath.empty() || RelativePathString.rfind(L"..", 0) == 0)
		{
			return FPaths::ToUtf8(InAbsolutePath.generic_wstring());
		}

		return FPaths::ToUtf8(RelativePath.generic_wstring());
	}
}
