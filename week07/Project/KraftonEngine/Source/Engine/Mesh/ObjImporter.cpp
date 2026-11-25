#include "Mesh/ObjImporter.h"
#include "Mesh/StaticMeshAsset.h"
#include "Materials/Material.h"
#include "Editor/UI/EditorConsoleWidget.h"
#include "Engine/Platform/Paths.h"
#include "Mesh/ObjManager.h"
#include "SimpleJSON/json.hpp"
#include "Materials/MaterialManager.h"
#include <algorithm>
#include <fstream>
#include <filesystem>
#include <charconv>
#include <chrono>

const FVector FallbackColor3 = FVector(1.0f, 0.0f, 1.0f);
const FVector4 FallbackColor4 = FVector4(1.0f, 0.0f, 1.0f, 1.0f);
const FVector WireframeBarycentrics[3] =
{
	FVector(1.0f, 0.0f, 0.0f),
	FVector(0.0f, 1.0f, 0.0f),
	FVector(0.0f, 0.0f, 1.0f)
};

struct FVertexKey {
    uint32 p, t, n;
    bool operator==(const FVertexKey& Other) const {
        return p == Other.p && t == Other.t && n == Other.n;
    }
};

namespace std {
template<>
struct hash<FVertexKey>
{
    size_t operator()(const FVertexKey& Key) const noexcept
    {
        return ((size_t)Key.p) ^ (((size_t)Key.t) << 8) ^ (((size_t)Key.n) << 16);
    }
};
}

struct FStringParser
{
	// Delimiter로 구분된 다음 토큰을 추출하고, InOutView에서 해당 토큰과 구분자 제거
	static std::string_view GetNextToken(std::string_view& InOutView, char Delimiter = ' ')
	{
		size_t DelimiterPosition = InOutView.find(Delimiter);
		std::string_view Token = InOutView.substr(0, DelimiterPosition); // [0, DelimiterPosition) 범위의 토큰 추출
		if (DelimiterPosition != std::string_view::npos)
		{
			InOutView.remove_prefix(DelimiterPosition + 1); // 토큰과 구분자 제거
		}
		else
		{
			InOutView = std::string_view();
		}
		return Token;
	}

	// 다수의 공백을 구분자로 사용하여 다음 토큰을 추출하고, InOutView에서 해당 토큰과 앞의 공백 제거
	static std::string_view GetNextWhitespaceToken(std::string_view& InOutView)
	{
		size_t Start = InOutView.find_first_not_of(" \t");
		if (Start == std::string_view::npos)
		{
			InOutView = std::string_view();
			return std::string_view();
		}
		InOutView.remove_prefix(Start); // 유효한 문자 앞의 공백 제거

		size_t End = InOutView.find_first_of(" \t");
		std::string_view Token = InOutView.substr(0, End); // 공백 이전까지의 토큰 추출

		if (End != std::string_view::npos)
		{
			InOutView.remove_prefix(End);
		}
		else
		{
			InOutView = std::string_view();
		}
		return Token;
	}

	// InOutView의 왼쪽 끝에 있는 공백 제거
	static void TrimLeft(std::string_view& InOutView)
	{
		size_t Start = InOutView.find_first_not_of(" \t");
		if (Start != std::string_view::npos)
		{
			InOutView.remove_prefix(Start);  // 유효한 문자 앞의 공백 제거
		}
		else
		{
			InOutView = std::string_view();
		}
	}

	static bool ParseInt(std::string_view Str, int& OutValue)
	{
		if (Str.empty()) return false;
		std::from_chars_result result = std::from_chars(Str.data(), Str.data() + Str.size(), OutValue);
		return result.ec == std::errc();
	}

	static bool ParseFloat(std::string_view Str, float& OutValue)
	{
		if (Str.empty()) return false;
		std::from_chars_result result = std::from_chars(Str.data(), Str.data() + Str.size(), OutValue);
		return result.ec == std::errc();
	}
};

struct FRawFaceVertex
{
    int32 PosIndex = -1;
    int32 UVIndex = -1;
    int32 NormalIndex = -1;
};

FRawFaceVertex ParseSingleFaceVertex(std::string_view FaceToken)
{
    FRawFaceVertex Result;

    // 첫 번째 토큰: Position
    std::string_view PosStr = FStringParser::GetNextToken(FaceToken, '/');
    FStringParser::ParseInt(PosStr, Result.PosIndex);

    // 두 번째 토큰: UV (있을 수도, 비어있을 수도 있음)
    if (!FaceToken.empty())
    {
        std::string_view UVStr = FStringParser::GetNextToken(FaceToken, '/');
        if (!UVStr.empty())
        {
            FStringParser::ParseInt(UVStr, Result.UVIndex);
        }
    }

    // 세 번째 토큰: Normal
    if (!FaceToken.empty())
    {
        std::string_view NormalStr = FStringParser::GetNextToken(FaceToken, '/');
        FStringParser::ParseInt(NormalStr, Result.NormalIndex);
    }

    return Result;
}

bool FObjImporter::ParseObj(const FString& ObjFilePath, FObjInfo& OutObjInfo)
{
	OutObjInfo = FObjInfo();

	std::ifstream File(FPaths::ToWide(ObjFilePath), std::ios::binary | std::ios::ate);
	if (!File.is_open())
	{
		UE_LOG("Failed to open OBJ file: %s", ObjFilePath.c_str());
		return false;
	}

	size_t FileSize = static_cast<size_t>(File.tellg());
	File.seekg(0, std::ios::beg);
	TArray<char> Buffer(FileSize);
	if (!File.read(Buffer.data(), FileSize))
	{
		UE_LOG("Failed to read OBJ file: %s", ObjFilePath.c_str());
		return false;
	}

	std::string_view FileView(Buffer.data(), Buffer.size());

	// UTF-8 BOM 스킵
	if (FileView.size() >= 3 && FileView[0] == '\xEF' && FileView[1] == '\xBB' && FileView[2] == '\xBF')
	{
		FileView.remove_prefix(3);
	}

	TArray<FRawFaceVertex> FaceVertices;
	FaceVertices.reserve(6); // Heuristic

	int32 CurrentSmoothingGroup = 0;

	while (!FileView.empty())
	{
		std::string_view Line = FStringParser::GetNextToken(FileView, '\n');

		// CRLF 제거
		if (!Line.empty() && Line.back() == '\r')
		{
			Line.remove_suffix(1);
		}

		if (Line.empty() || Line[0] == '#')
		{
			continue;
		}

		std::string_view Prefix = FStringParser::GetNextToken(Line);

		if (Prefix == "v")
		{
			FVector Position;
			FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), Position.X);
			FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), Position.Y);
			FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), Position.Z);
			OutObjInfo.Positions.emplace_back(Position);
		}
		else if (Prefix == "vt")
		{
			FVector2 UV;
			FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), UV.U);
			FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), UV.V);
			OutObjInfo.UVs.emplace_back(UV);
		}
		else if (Prefix == "vn")
		{
			FVector Normal;
			FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), Normal.X);
			FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), Normal.Y);
			FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), Normal.Z);
			OutObjInfo.Normals.emplace_back(Normal);
		}
		else if (Prefix == "f")
		{
			// default material section 추가 (usemtl이 없이 f가 먼저 나오는 경우)
			if (OutObjInfo.Sections.empty())
			{
				FStaticMeshSection DefaultSection;
				DefaultSection.MaterialSlotName = "None";
				DefaultSection.FirstIndex = 0;
				DefaultSection.NumTriangles = 0;
				OutObjInfo.Sections.emplace_back(DefaultSection);
			}

			while (!Line.empty())
			{
				std::string_view FaceToken = FStringParser::GetNextToken(Line, ' ');
				if (!FaceToken.empty())
				{
					FaceVertices.push_back(ParseSingleFaceVertex(FaceToken));
				}
			}

			if (FaceVertices.size() < 3)
			{
				UE_LOG("Face with less than 3 vertices");
				continue;
			}

			// Fan triangulation
			for (size_t i = 1; i + 1 < FaceVertices.size(); ++i)
			{
				const std::array<FRawFaceVertex, 3> TriangleVerts = { FaceVertices[0], FaceVertices[i], FaceVertices[i + 1] };
				for (int j = 0; j < 3; ++j)
				{
					constexpr int32 InvalidIndex = -1;
					OutObjInfo.PosIndices.emplace_back(TriangleVerts[j].PosIndex - 1);
					OutObjInfo.UVIndices.emplace_back(TriangleVerts[j].UVIndex > 0 ? TriangleVerts[j].UVIndex - 1 : InvalidIndex);
					OutObjInfo.NormalIndices.emplace_back(TriangleVerts[j].NormalIndex > 0 ? TriangleVerts[j].NormalIndex - 1 : InvalidIndex);
				}
				OutObjInfo.FaceSmoothingGroups.emplace_back(CurrentSmoothingGroup);
			}
			FaceVertices.clear();
		}
		else
		{
			if (Prefix == "mtllib")
			{
				size_t CommentPos = Line.find('#');
				if (CommentPos != std::string_view::npos) { Line = Line.substr(0, CommentPos); }
				FStringParser::TrimLeft(Line);
				OutObjInfo.MaterialLibraryFilePath = FPaths::ResolveAssetPath(ObjFilePath, std::string(Line));
				UE_LOG("Found material library: %s", OutObjInfo.MaterialLibraryFilePath.c_str());
			}
			else if (Prefix == "usemtl")
			{
				size_t CommentPos = Line.find('#');
				if (CommentPos != std::string_view::npos) { Line = Line.substr(0, CommentPos); }
				FStringParser::TrimLeft(Line);

				if (!OutObjInfo.Sections.empty())
				{
					OutObjInfo.Sections.back().NumTriangles = (static_cast<uint32>(OutObjInfo.PosIndices.size()) - OutObjInfo.Sections.back().FirstIndex) / 3;
				}
				FStaticMeshSection Section;
				Section.MaterialSlotName = std::string(Line);
				if (Section.MaterialSlotName.empty())
				{
					Section.MaterialSlotName = "None";
				}
				Section.FirstIndex = static_cast<uint32>(OutObjInfo.PosIndices.size());
				OutObjInfo.Sections.emplace_back(Section);
			}
			else if (Prefix == "o")
			{
				size_t CommentPos = Line.find('#');
				if (CommentPos != std::string_view::npos) { Line = Line.substr(0, CommentPos); }
				FStringParser::TrimLeft(Line);

				OutObjInfo.ObjectName = std::string(Line);
			}
			else if (Prefix == "s")
			{
				std::string_view SVal = FStringParser::GetNextWhitespaceToken(Line);
				if (SVal == "off" || SVal == "0")
					CurrentSmoothingGroup = 0;
				else
				{
					int32 Group = 0;
					FStringParser::ParseInt(SVal, Group);
					CurrentSmoothingGroup = Group;
				}
			}
		}
	}

	if (!OutObjInfo.Sections.empty())
	{
		OutObjInfo.Sections.back().NumTriangles = (static_cast<uint32>(OutObjInfo.PosIndices.size()) - OutObjInfo.Sections.back().FirstIndex) / 3;
	}

	if (OutObjInfo.UVs.empty())
	{
		OutObjInfo.UVs.emplace_back(FVector2{ 0.0f, 0.0f });
	}

	return true;
}

bool FObjImporter::ParseMtl(const FString& MtlFilePath, TArray<FObjMaterialInfo>& OutMtlInfos)
{
	OutMtlInfos.clear();
	std::ifstream File(FPaths::ToWide(MtlFilePath), std::ios::binary | std::ios::ate);

	if (!File.is_open())
	{
		UE_LOG("Failed to open MTL file: %s", MtlFilePath.c_str());
		return false;
	}

	size_t FileSize = static_cast<size_t>(File.tellg());
	File.seekg(0, std::ios::beg);
	TArray<char> Buffer(FileSize);
	if (!File.read(Buffer.data(), FileSize))
	{
		UE_LOG("Failed to read MTL file: %s", MtlFilePath.c_str());
		return false;
	}

	std::string_view FileView(Buffer.data(), Buffer.size());

	// UTF-8 BOM 스킵
	if (FileView.size() >= 3 && FileView[0] == '\xEF' && FileView[1] == '\xBB' && FileView[2] == '\xBF')
	{
		FileView.remove_prefix(3);
	}

	while (!FileView.empty())
	{
		std::string_view Line = FStringParser::GetNextToken(FileView, '\n');

		// CRLF 제거
		if (!Line.empty() && Line.back() == '\r')
		{
			Line.remove_suffix(1);
		}

		if (Line.empty() || Line[0] == '#')
		{
			continue;
		}

		std::string_view Prefix = FStringParser::GetNextWhitespaceToken(Line);

		if (Prefix == "newmtl")
		{
			FObjMaterialInfo MaterialInfo;
			FStringParser::TrimLeft(Line);
			MaterialInfo.MaterialSlotName = std::string(Line);
			MaterialInfo.Kd = FallbackColor3;
			OutMtlInfos.emplace_back(MaterialInfo);
		}
		else if (Prefix == "Kd")
		{
			if (OutMtlInfos.empty())
			{
				continue;
			}
			FObjMaterialInfo& CurrentMaterial = OutMtlInfos.back();
			FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), CurrentMaterial.Kd.X);
			FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), CurrentMaterial.Kd.Y);
			FStringParser::ParseFloat(FStringParser::GetNextWhitespaceToken(Line), CurrentMaterial.Kd.Z);
		}
		else if (Prefix == "map_Kd" || Prefix == "map_Bump")
		{
			if (OutMtlInfos.empty())
			{
				continue;
			}

			std::string TextureFileName;

			// 토큰 단위로 옵션들을 건너뜁니다.
			while (!Line.empty())
			{
				// 파일명에 공백이 포함될 수 있으므로, 현재 Line의 상태를 백업해둡니다.
				std::string_view LineBeforeToken = Line;
				std::string_view Token = FStringParser::GetNextWhitespaceToken(Line);

				if (Token.empty()) break;

				// 토큰이 '-'로 시작하면 옵션 플래그인지 확인합니다.
				if (Token[0] == '-')
				{
					int32 ArgsToSkip = 0;

					// 1. 3개의 인자를 받는 옵션 (Vector)
					if (Token == "-s" || Token == "-o" || Token == "-t")
					{
						ArgsToSkip = 3;
					}
					// 2. 2개의 인자를 받는 옵션
					else if (Token == "-mm")
					{
						ArgsToSkip = 2;
					}
					// 3. 1개의 인자를 받는 옵션 (Float, String, Bool)
					else if (Token == "-bm" || Token == "-boost" || Token == "-texres" ||
							 Token == "-blendu" || Token == "-blendv" || Token == "-clamp" ||
							 Token == "-cc" || Token == "-imfchan")
					{
						ArgsToSkip = 1;
					}

					// 파악된 옵션의 인자 개수만큼 다음 토큰들을 무시합니다.
					for (int32 i = 0; i < ArgsToSkip; ++i)
					{
						FStringParser::GetNextWhitespaceToken(Line);
					}
				}
				else
				{
					// '-'로 시작하지 않는 첫 번째 토큰을 만났다면, 이것이 파일명의 시작입니다!
					// 파일명 내부에 띄어쓰기가 있을 수 있으므로 토큰을 뽑기 전의 전체 라인을 가져옵니다.
					FStringParser::TrimLeft(LineBeforeToken);
					TextureFileName = FString(LineBeforeToken);
					break;
				}
			}

			// 문자열 끝에 남아있을지 모르는 쓸데없는 공백이나 탭을 정리합니다. (RTrim)
			size_t LastNonSpace = TextureFileName.find_last_not_of(" \t");
			if (LastNonSpace != FString::npos)
			{
				TextureFileName.erase(LastNonSpace + 1);
			}

			// 최종적으로 추출된 파일명 할당
			if (!TextureFileName.empty())
			{
				FString ResolvedPath = FPaths::ResolveAssetPath(MtlFilePath, TextureFileName);
				if (Prefix == "map_Kd")
					OutMtlInfos.back().map_Kd = ResolvedPath;
				else if(Prefix == "map_Bump")
					OutMtlInfos.back().map_Bump = ResolvedPath;
			}
		}
	}

	return true;
}

// MTL 정보에서 머티리얼 JSON 파일로 변환하는 함수
FString FObjImporter::ConvertMtlInfoToJson(const FObjMaterialInfo* MtlInfo)
{
	FString JsonPath = "Asset/Materials/" + MtlInfo->MaterialSlotName + ".json";

	// 이미 존재하면 덮어쓰지 않음 (에디터에서 수정했을 수 있으므로)
	if (std::filesystem::exists(FPaths::ToWide(JsonPath)))
		return JsonPath;

	json::JSON JsonData;
	JsonData["PathFileName"] = JsonPath;
	JsonData["ShaderPath"] = "Shaders/StaticMeshShader.hlsl"; // 기본 셰이더
	JsonData["RenderPass"] = "Opaque";

	if (!MtlInfo->map_Kd.empty())
	{
		JsonData["Textures"]["DiffuseTexture"] = MtlInfo->map_Kd;
		if (!MtlInfo->map_Bump.empty())
		{
			JsonData["Textures"]["NormalTexture"] = MtlInfo->map_Bump;
		}
		JsonData["Parameters"]["SectionColor"][0] = 1.0f;
		JsonData["Parameters"]["SectionColor"][1] = 1.0f;
		JsonData["Parameters"]["SectionColor"][2] = 1.0f;
		JsonData["Parameters"]["SectionColor"][3] = 1.0f;
	}
	else
	{

		JsonData["Parameters"]["SectionColor"][0] = MtlInfo->Kd.X;
		JsonData["Parameters"]["SectionColor"][1] = MtlInfo->Kd.Y;
		JsonData["Parameters"]["SectionColor"][2] = MtlInfo->Kd.Z;
		JsonData["Parameters"]["SectionColor"][3] = 1.0f;
	}

	std::ofstream File(FPaths::ToWide(JsonPath));
	File << JsonData.dump();

	return JsonPath;
}

FVector FObjImporter::RemapPosition(const FVector& ObjPos, EForwardAxis Axis)
{
	// OBJ 원본 좌표 (Ox, Oy, Oz) → 엔진 (Ex, Ey, Ez)
	// 엔진: X=Forward, Y=Right, Z=Up
	// OBJ 기본: Y-up 우수 좌표계
	switch (Axis)
	{
	case EForwardAxis::X:    // OBJ +X → Engine Forward(+X)
		return FVector(ObjPos.X, ObjPos.Z, ObjPos.Y);
	case EForwardAxis::NegX: // OBJ -X → Engine Forward(+X)
		return FVector(-ObjPos.X, -ObjPos.Z, ObjPos.Y);
	case EForwardAxis::Y:    // OBJ +Y → Engine Forward(+X)
		return FVector(ObjPos.Y, ObjPos.X, ObjPos.Z);
	case EForwardAxis::NegY: // OBJ -Y → Engine Forward(+X) — 블렌더 기본
		return FVector(-ObjPos.Y, -ObjPos.X, ObjPos.Z);
	case EForwardAxis::Z:    // OBJ +Z → Engine Forward(+X)
		return FVector(ObjPos.Z, ObjPos.X, ObjPos.Y);
	case EForwardAxis::NegZ: // OBJ -Z → Engine Forward(+X) — OBJ 기본 (Y-up, -Z forward)
		return FVector(-ObjPos.Z, ObjPos.X, ObjPos.Y);
	default:
		return FVector(ObjPos.X, ObjPos.Z, ObjPos.Y);
	}
}

bool FObjImporter::Convert(const FObjInfo& ObjInfo, const TArray<FObjMaterialInfo>& MtlInfos, const FImportOptions& Options, FStaticMesh& OutMesh, TArray<FStaticMaterial>& OutMaterials)
{
	OutMesh = FStaticMesh();
	OutMaterials.clear();

	// Phase 1: usemtl 등장 순서를 기반으로 FStaticMaterial 배열 및 인덱스 맵 생성
	TArray<FString> OrderedMaterialSlots;
	bool bHasNoneSlot = false;

	// OBJ의 Sections(usemtl) 등장 순서대로 고유 슬롯 수집
	for (const FStaticMeshSection& Section : ObjInfo.Sections)
	{
		const FString& CurrentSlotName = Section.MaterialSlotName;

		if (CurrentSlotName == "None")
		{
			bHasNoneSlot = true;
			continue;
		}

		// 기존에 수집된 슬롯과 중복되지 않는 경우에만 추가
		if (std::find(OrderedMaterialSlots.begin(), OrderedMaterialSlots.end(), CurrentSlotName) == OrderedMaterialSlots.end())
		{
			OrderedMaterialSlots.push_back(CurrentSlotName);
		}
	}

	// 수집된 순서대로 머티리얼 생성 및 인덱스 매핑
	for (const FString& TargetSlotName : OrderedMaterialSlots)
	{
		// 슬롯 이름과 일치하는 파싱된 머티리얼 데이터 선형 탐색
		const FObjMaterialInfo* MatchedMaterial = nullptr;
		auto It = std::find_if(MtlInfos.begin(), MtlInfos.end(),
			[&TargetSlotName](const FObjMaterialInfo& Mat) {
				return Mat.MaterialSlotName == TargetSlotName;
			});

		if (It != MtlInfos.end())
		{
			MatchedMaterial = &(*It);
			// 섹션 머티리얼 슬롯 이름과 일치하는 머티리얼 이름이 MTL 파일에서 발견된 경우, 해당 머티리얼 로드 또는 생성
			UE_LOG("Importer TargetSlotName: %s;", TargetSlotName.c_str());

			// Convert() 안에서 기존 직접 세팅 대신
			FString JsonPath = ConvertMtlInfoToJson(MatchedMaterial); // .json 파일 생성
			UMaterial* MaterialObject = FMaterialManager::Get().GetOrCreateMaterial(JsonPath);

			// FStaticMaterial 슬롯 생성 및 OutMaterials에 추가
			FStaticMaterial NewStaticMaterial;
			NewStaticMaterial.MaterialInterface = MaterialObject;
			NewStaticMaterial.MaterialSlotName = TargetSlotName;
			OutMaterials.push_back(NewStaticMaterial);
		}
		else // Material Slot이 MTL 파일에 정의되어 있지 않은 경우
		{
			UMaterial* DefaultMaterial = FMaterialManager::Get().GetOrCreateMaterial("None");

			// FStaticMaterial 슬롯 생성 및 OutMaterials에 추가
			FStaticMaterial NewEmptyStaticMaterial;
			NewEmptyStaticMaterial.MaterialInterface = DefaultMaterial;
			NewEmptyStaticMaterial.MaterialSlotName = TargetSlotName;
			OutMaterials.push_back(NewEmptyStaticMaterial);
		}
	}

	// "None" 슬롯이 존재했다면 맨 마지막에 배치
	if (bHasNoneSlot)
	{
		UMaterial* DefaultMaterial = FMaterialManager::Get().GetOrCreateMaterial("None");

		FStaticMaterial NewDefaultStaticMaterial;
		NewDefaultStaticMaterial.MaterialInterface = DefaultMaterial;
		NewDefaultStaticMaterial.MaterialSlotName = "None";

		OutMaterials.push_back(NewDefaultStaticMaterial);
	}

    // Phase 2: 파편화된 섹션들의 면(Face)을 머티리얼 인덱스 기준으로 재그룹화
	TArray<TArray<uint32>> FacesPerMaterial;
	FacesPerMaterial.resize(OutMaterials.size());

	for (const FStaticMeshSection& RawSection : ObjInfo.Sections)
	{
		// 섹션의 머티리얼 슬롯 이름과 일치하는 OutMaterials 배열의 인덱스 찾기
		auto It = std::find_if(OutMaterials.begin(), OutMaterials.end(),
			[&RawSection](const FStaticMaterial& Mat) {
				return Mat.MaterialSlotName == RawSection.MaterialSlotName;
			});

		size_t MaterialIndex = 0;
		if (It != OutMaterials.end())
		{
			MaterialIndex = std::distance(OutMaterials.begin(), It);
		}
		else
		{
			// "None" 슬롯이 없고 매칭되는 슬롯도 없는 경우, 기본 머티리얼로 할당
			MaterialIndex = OutMaterials.size() - 1; // "None" 슬롯이 마지막에 배치되어 있다고 가정
			UE_LOG("Warning: Material slot '%s' not found. Assigning to Default slot.", RawSection.MaterialSlotName.c_str());
		}

		for (uint32 i = 0; i < RawSection.NumTriangles; ++i)
		{
			uint32 FaceStartIndex = RawSection.FirstIndex + (i * 3);
			FacesPerMaterial[MaterialIndex].push_back(FaceStartIndex);
		}
	}

    // Phase 3: 재그룹화된 면 데이터를 기반으로 최종 정점/인덱스 버퍼 구축
	TMap<FVertexKey, uint32> VertexMap;
	TArray<int32> ReorderedSmoothingGroups;

	for (size_t MaterialIndex = 0; MaterialIndex < OutMaterials.size(); ++MaterialIndex)
	{
		const TArray<uint32>& FaceStarts = FacesPerMaterial[MaterialIndex];
		if (FaceStarts.empty()) continue;

		FStaticMeshSection NewSection;
		NewSection.MaterialIndex = static_cast<int32>(MaterialIndex);
		NewSection.MaterialSlotName = OutMaterials[MaterialIndex].MaterialSlotName;
		NewSection.FirstIndex = static_cast<uint32>(OutMesh.Indices.size());
		NewSection.NumTriangles = static_cast<uint32>(FaceStarts.size());

		for (uint32 FaceStartIndex : FaceStarts)
		{
			uint32 TriangleIndices[3];

			// (P1 - P0) X (P2 - P0) 후 정규화
			FVector P0 = ObjInfo.Positions[ObjInfo.PosIndices[FaceStartIndex]];
			FVector P1 = ObjInfo.Positions[ObjInfo.PosIndices[FaceStartIndex + 1]];
			FVector P2 = ObjInfo.Positions[ObjInfo.PosIndices[FaceStartIndex + 2]];

			FVector Edge1 = P1 - P0;
			FVector Edge2 = P2 - P0;
			FVector FaceNormal = Edge1.Cross(Edge2).Normalized();

			for (int j = 0; j < 3; ++j)
			{
				size_t CurrentIndex = FaceStartIndex + j;
				FVertexKey Key = {
					ObjInfo.PosIndices[CurrentIndex],
					ObjInfo.UVIndices[CurrentIndex],
					ObjInfo.NormalIndices[CurrentIndex]
				};

				if (auto It = VertexMap.find(Key); It != VertexMap.end())
				{
					// 이미 생성된 정점이 있다면 인덱스 재사용
					TriangleIndices[j] = It->second;
				}
				else
				{
					// 새로운 정점 생성
					FNormalVertex NewVertex;

					// 축 리맵 + 스케일 적용
					NewVertex.pos = RemapPosition(ObjInfo.Positions[Key.p], Options.ForwardAxis) * Options.Scale;

					// Normal 리맵
					if (Key.n == -1)
					{
						NewVertex.normal = RemapPosition(FaceNormal, Options.ForwardAxis).Normalized();
					}
					else
					{
						NewVertex.normal = RemapPosition(ObjInfo.Normals[Key.n], Options.ForwardAxis).Normalized();
					}

					// UV 예외 처리
					if (Key.t == -1)
					{
						NewVertex.tex = { 0.0f, 0.0f };
					}
					else
					{
						NewVertex.tex = ObjInfo.UVs[Key.t];
						// UV 변환 (left-bottom -> left-top)
						NewVertex.tex.V = 1.0f - NewVertex.tex.V;
					}

					NewVertex.color = { 1.0f, 1.0f, 1.0f, 1.0f };
					NewVertex.barycentric = FVector(0.0f, 0.0f, 0.0f);

					uint32 NewIndex = static_cast<uint32>(OutMesh.Vertices.size());
					OutMesh.Vertices.push_back(NewVertex);

					VertexMap[Key] = NewIndex;
					TriangleIndices[j] = NewIndex;
				}
			}

			// 와인딩 오더 처리
			OutMesh.Indices.push_back(TriangleIndices[0]);
			if (Options.WindingOrder == EWindingOrder::CCW_to_CW)
			{
				OutMesh.Indices.push_back(TriangleIndices[2]);
				OutMesh.Indices.push_back(TriangleIndices[1]);
			}
			else
			{
				OutMesh.Indices.push_back(TriangleIndices[1]);
				OutMesh.Indices.push_back(TriangleIndices[2]);
			}

			// 최종 인덱스 버퍼 순서에 맞게 스무딩 그룹 처리
			int32 OriginalTriIndex = static_cast<int32>(FaceStartIndex / 3);
			int32 SmoothGroupIndex = 0;
			if (!ObjInfo.FaceSmoothingGroups.empty())
			{
				SmoothGroupIndex = ObjInfo.FaceSmoothingGroups[OriginalTriIndex];
			}

			ReorderedSmoothingGroups.push_back(SmoothGroupIndex);
		}

		OutMesh.Sections.push_back(NewSection);
	}

	ComputeTangents(ReorderedSmoothingGroups, OutMesh);
	ExpandTrianglesForWireframe(OutMesh);

    return true;
}

void FObjImporter::ComputeTangents(const TArray<int32>& ReorderedSmoothingGroups, FStaticMesh& OutMesh)
{
	// Face 탄젠트 계산
	const uint32 FaceCount = static_cast<uint32>(OutMesh.Indices.size() / 3);
	if (FaceCount == 0) return;

	TArray<FVector> FaceTangents;
	FaceTangents.resize(FaceCount);
	for (int32 FaceIdx = 0; FaceIdx < FaceCount; ++FaceIdx)
	{
		uint32 Index0 = OutMesh.Indices[FaceIdx * 3 + 0];
		uint32 Index1 = OutMesh.Indices[FaceIdx * 3 + 1];
		uint32 Index2 = OutMesh.Indices[FaceIdx * 3 + 2];

		const FNormalVertex& Vertex0 = OutMesh.Vertices[Index0];
		const FNormalVertex& Vertex1 = OutMesh.Vertices[Index1];
		const FNormalVertex& Vertex2 = OutMesh.Vertices[Index2];

		FVector Edge1 = Vertex1.pos - Vertex0.pos;
		FVector Edge2 = Vertex2.pos - Vertex0.pos;

		float DeltaU1 = Vertex1.tex.U - Vertex0.tex.U;
		float DeltaV1 = Vertex1.tex.V - Vertex0.tex.V;

		float DeltaU2 = Vertex2.tex.U - Vertex0.tex.U;
		float DeltaV2 = Vertex2.tex.V - Vertex0.tex.V;

		float Det = DeltaU1 * DeltaV2 - DeltaU2 * DeltaV1;

		// UV가 일직선이거나 같은 UV 예외 처리
		if (fabs(Det) < 1e-8)
		{
			FaceTangents[FaceIdx] = FVector(1.0f, 0.0f, 0.0f);
			continue;
		}

		FVector Tangent = (Edge1 * DeltaV2 - Edge2 * DeltaV1) / Det;
		FaceTangents[FaceIdx] = Tangent;
	}

	// Vertex가 어디 Face에 속해있는지 판별용
	uint32 VertexCnt = OutMesh.Vertices.size();
	TArray<TArray<uint32>> VertexToFace;
	VertexToFace.resize(VertexCnt);
	for (int32 FaceIdx = 0; FaceIdx < FaceCount; ++FaceIdx)
	{
		uint32 Index0 = OutMesh.Indices[FaceIdx * 3 + 0];
		uint32 Index1 = OutMesh.Indices[FaceIdx * 3 + 1];
		uint32 Index2 = OutMesh.Indices[FaceIdx * 3 + 2];

		VertexToFace[Index0].push_back(FaceIdx);
		VertexToFace[Index1].push_back(FaceIdx);
		VertexToFace[Index2].push_back(FaceIdx);
	}

	// Face별 Tangent를 스무딩 그룹을 통해 최종 하나의 Vertex의 Tangent를 계산
	for (int32 VertexIdx = 0; VertexIdx < static_cast<int32>(VertexCnt); ++VertexIdx)
	{
		const TArray<uint32>& AdjFaces = VertexToFace[VertexIdx];
		if (AdjFaces.empty()) continue;

		// 이 정점의 기준 스무딩 그룹 -> 첫 번째 인접 face의 그룹
		int32 RefGroup = ReorderedSmoothingGroups[AdjFaces[0]];

		FVector TotalTangent(0.f, 0.f, 0.f);
		for (uint32 AdjFace : AdjFaces)
		{
			int32 AdjGroup = ReorderedSmoothingGroups[AdjFace];

			// 둘 다 그룹 번호가 있으면 같은 그룹만 합산
			if (RefGroup == 0 || AdjGroup == 0 || RefGroup == AdjGroup)
			{
				TotalTangent += FaceTangents[AdjFace];
			}
		}

		// 그람 슈미트로 Tangent를 VN기준으로 보정
		FVector N = OutMesh.Vertices[VertexIdx].normal;
		N.Normalize();
		TotalTangent -= N * TotalTangent.Dot(N);
		TotalTangent.Normalize();

		OutMesh.Vertices[VertexIdx].tangent = TotalTangent;
	}
}

void FObjImporter::ExpandTrianglesForWireframe(FStaticMesh& OutMesh)
{
	if (OutMesh.Indices.empty() || OutMesh.Vertices.empty())
	{
		return;
	}

	TArray<FNormalVertex> ExpandedVertices;
	TArray<uint32> ExpandedIndices;
	ExpandedVertices.reserve(OutMesh.Indices.size());
	ExpandedIndices.reserve(OutMesh.Indices.size());

	for (uint32 TriangleStart = 0; TriangleStart + 2 < static_cast<uint32>(OutMesh.Indices.size()); TriangleStart += 3)
	{
		for (uint32 Corner = 0; Corner < 3; ++Corner)
		{
			const uint32 SourceIndex = OutMesh.Indices[TriangleStart + Corner];
			FNormalVertex ExpandedVertex = OutMesh.Vertices[SourceIndex];
			ExpandedVertex.barycentric = WireframeBarycentrics[Corner];

			const uint32 ExpandedIndex = static_cast<uint32>(ExpandedVertices.size());
			ExpandedVertices.push_back(ExpandedVertex);
			ExpandedIndices.push_back(ExpandedIndex);
		}
	}

	OutMesh.Vertices = std::move(ExpandedVertices);
	OutMesh.Indices = std::move(ExpandedIndices);
}

bool FObjImporter::Import(const FString& ObjFilePath, FStaticMesh& OutMesh, TArray<FStaticMaterial>& OutMaterials)
{
	return Import(ObjFilePath, FImportOptions::Default(), OutMesh, OutMaterials);
}

bool FObjImporter::Import(const FString& ObjFilePath, const FImportOptions& Options, FStaticMesh& OutMesh, TArray<FStaticMaterial>& OutMaterials)
{
	auto StartTime = std::chrono::high_resolution_clock::now();

	OutMaterials.clear();

	FObjInfo ObjInfo;
	if (!FObjImporter::ParseObj(ObjFilePath, ObjInfo))
	{
		UE_LOG("ParseObj failed for: %s", ObjFilePath.c_str());
		return false;
	}

	TArray<FObjMaterialInfo> ParsedMtlInfos;
	if (!ObjInfo.MaterialLibraryFilePath.empty()) {
		if (!FObjImporter::ParseMtl(ObjInfo.MaterialLibraryFilePath, ParsedMtlInfos))
		{
			UE_LOG("ParseMtl failed for: %s", ObjInfo.MaterialLibraryFilePath.c_str());
			ObjInfo.MaterialLibraryFilePath.clear();
			ParsedMtlInfos.clear();
		}
	}

	if (!FObjImporter::Convert(ObjInfo, ParsedMtlInfos, Options, OutMesh, OutMaterials)){
		UE_LOG("Convert failed for: %s", ObjFilePath.c_str());
		return false;
	}
	OutMesh.PathFileName = ObjFilePath;

	auto EndTime = std::chrono::high_resolution_clock::now();
	std::chrono::duration<double> Duration = EndTime - StartTime;
	UE_LOG("OBJ Imported successfully. File: %s. Time taken: %.4f seconds", ObjFilePath.c_str(), Duration.count());

	return true;
}
