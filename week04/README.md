# 🧱 Week 04 — Static Mesh & Material Editor

> OBJ/MTL 에셋 파이프라인과 다중 재질 편집, 4분할 뷰포트를 갖춘 DirectX 11 기반 에디터

[← 전체 주차 보기](../README.md)

## 프로젝트 정보

- 프로젝트명: Static Mesh & Material Editor
- 기간: 2026.03.27. ~ 2026.04.02.
- 팀 구성: 4명
- 원본 저장소: [W4_Jungle_Team2](https://github.com/keonwookang0914/W4_Jungle_Team2)
- 공개 프로젝트: [Project](Project/) · [W4_Jungle_Team2.sln](Project/W4_Jungle_Team2.sln)

## 프로젝트 개요

3주차의 DirectX 11 에디터를 확장해 외부 OBJ 모델과 MTL 머티리얼을 불러오고, 섹션별 재질과 텍스처를 확인·교체할 수 있는 에셋 파이프라인을 구현했습니다. 정적 메시의 파싱, 바이너리 캐시, GPU 버퍼 생성과 렌더링을 연결하고 4분할 뷰포트, 씬 편집, Material Editor와 전용 ObjViewer 모드를 하나의 프로젝트에 구성했습니다.

개인 기여는 4주차 시작 이후의 커밋만 분리한 다음, 본인 계정과 작성자 서명을 대조하고 각 비병합 커밋의 변경 파일과 실제 diff를 확인해 기능 단위로 정리했습니다. 공개 문서에는 팀원의 실명이나 개별 커밋 기록을 싣지 않았습니다.

## 팀 전체 구현

- Perspective와 세 방향 Orthographic 카메라를 사용하는 4분할 뷰포트
- 스플리터 드래그, 단일 뷰포트 최대화와 뷰포트별 포커스·입력 관리
- 클릭·박스 선택, 이동·회전·스케일 기즈모와 월드·로컬 모드
- OBJ의 위치·UV·법선·face·재질 정보를 정적 메시 데이터로 변환
- MTL의 색상·광택·투명도·텍스처 맵 파싱
- `usemtl` 기준 섹션 분리와 섹션별 MaterialSlot·override material
- Material Editor를 이용한 재질 확인·교체와 텍스처 미리보기
- Lit·Unlit·Wireframe 뷰 모드와 Grid·Gizmo·Bounding Volume Show Flag
- 메시 레이캐스트 피킹, 선택 마스크와 후처리 외곽선
- 씬과 카메라 상태의 JSON 저장·복원
- OBJ 바이너리 베이킹, 원본 수정 시간 검증과 캐시 우선 로딩
- 에디터와 분리된 OBJ 모델 확인용 ObjViewer 구성
- `UObject` 순회용 `TObjectIterator`와 객체·리소스 관리 보강

## 개인 기여

### MTL 파서와 머티리얼 레지스트리

- `newmtl`, `Ka`, `Kd`, `Ks`, `Ke`, `Ns`, `d`, `Tr`, `illum`을 읽어 `FMaterial` 데이터로 변환하는 MTL 로더를 구현했습니다.
- `map_Kd`, `map_Ka`, `map_Ks`, `map_bump` 경로를 해석하고 각 텍스처의 존재 플래그를 머티리얼에 저장했습니다.
- OBJ 파일 기준으로 상대 MTL 경로를 조합하고, MTL 주변 하위 디렉터리를 재귀 탐색해 텍스처 파일의 실제 경로를 찾도록 개선했습니다.
- Resource Manager에 머티리얼 레지스트리와 텍스처 캐시를 추가하고 이름으로 머티리얼을 조회할 수 있게 했습니다.
- 필요한 텍스처를 DirectX SRV로 로드하고 맵이 없거나 실패한 경우 기본 흰색 텍스처를 사용하도록 구성했습니다.

### StaticMesh와 섹션별 재질 렌더링

- `UStaticMesh`, `UMeshComponent`, `UStaticMeshComponent` 구조를 추가하고 정점·인덱스·섹션·MaterialSlot을 분리했습니다.
- OBJ의 `usemtl` 구간을 섹션으로 유지하고 각 섹션이 대응하는 머티리얼 슬롯을 참조하도록 연결했습니다.
- 메시 컴포넌트에 섹션별 override material 배열을 두어 원본 메시를 변경하지 않고 재질을 교체할 수 있게 했습니다.
- Render Collector가 섹션의 시작 인덱스와 개수, 머티리얼 상수와 텍스처 SRV를 렌더 커맨드에 담도록 구현했습니다.
- OBJ의 V축 방향과 DirectX 텍스처 좌표 방향 차이를 보정하고 StaticMesh 전용 셰이더·렌더 패스를 연결했습니다.

### Material Editor

- 선택한 `UStaticMeshComponent`의 섹션 목록과 머티리얼 상세 정보를 표시하는 ImGui 기반 편집 패널을 구현했습니다.
- 각 섹션의 슬롯 이름과 머티리얼 누락 상태를 표시하고, 레지스트리의 다른 머티리얼로 즉시 교체할 수 있게 했습니다.
- Diffuse·Ambient·Specular·Emissive 색상과 Shininess·Opacity 값을 확인할 수 있도록 구성했습니다.
- Diffuse·Ambient·Specular·Bump 텍스처의 썸네일, 파일 이름과 경로를 표시하고 호버 시 확대 미리보기를 제공했습니다.
- 선택 컴포넌트나 메시 에셋이 바뀌면 내부 선택 상태와 포인터를 초기화해 이전 오브젝트의 재질을 잘못 편집하는 문제를 수정했습니다.

### UV 스크롤과 정적 메시 셰이더 실험

- 메시 컴포넌트에 U·V 스크롤 값을 추가하고 Property 패널에서 조정할 수 있게 했습니다.
- 스크롤 값을 정적 메시 상수 버퍼로 전달해 vertex shader에서 UV 좌표에 적용했습니다.
- 비균일 스케일에서도 법선이 올바르게 변환되도록 모델 행렬의 역전치 normal matrix를 HLSL에 구현했습니다.
- Diffuse·Specular 텍스처와 MTL 색상·광택 값을 사용하는 Blinn–Phong 조명 모델을 실험했습니다.
- C++과 HLSL 상수 버퍼의 `bool` 크기 차이를 피하기 위해 맵 존재 플래그를 `uint32`로 통일하고 16바이트 정렬을 맞췄습니다.
- Wireframe 모드에서는 기본 텍스처와 전용 상태를 사용해 재질 텍스처가 선 색상에 영향을 주지 않도록 분리했습니다.

### 동적 에셋 로딩과 한글 경로 지원

- StaticMesh가 요청될 때 같은 이름의 MTL을 찾아 머티리얼과 텍스처를 동적으로 등록하도록 했습니다.
- 한 번 로드한 머티리얼과 텍스처는 레지스트리에서 재사용하고, Material Editor에서도 동일한 데이터를 조회하도록 통합했습니다.
- UTF-8 문자열을 `std::filesystem::path`의 wide 경로로 변환해 한글 파일·폴더를 안전하게 열도록 수정했습니다.
- 바이너리 캐시 경로 생성, 저장, 로드와 헤더 검증이 모두 같은 UTF-8↔wide 변환 경로를 사용하도록 정리했습니다.
- 한글 경로에서 OBJ 바이너리 저장이 실패하거나 저장된 캐시를 다시 열지 못하던 문제를 해결했습니다.

## 문제 해결

### OBJ 섹션과 머티리얼 슬롯의 개수가 일치하지 않는 문제

하나의 머티리얼이 여러 섹션에서 사용되면 슬롯 배열과 섹션 배열의 인덱스를 동일하게 취급할 수 없습니다. StaticMesh에는 고유한 MaterialSlot을 유지하고 각 섹션에는 `MaterialSlotIndex`를 저장했습니다. 컴포넌트의 override 배열은 렌더링할 섹션 순서로 구성해 섹션별 교체가 즉시 반영되도록 했습니다.

### MTL의 텍스처 상대 경로가 에셋마다 다른 문제

MTL이 가리키는 텍스처가 같은 폴더, `Texture` 하위 폴더 또는 더 깊은 디렉터리에 존재할 수 있었습니다. MTL 위치를 탐색 기준으로 삼고 파일 이름을 하위 폴더에서 재귀 검색한 뒤 정규화된 경로를 저장했습니다. 로드된 경로를 키로 사용해 GPU 텍스처가 중복 생성되지 않도록 했습니다.

### Material Editor가 이전 선택의 포인터를 유지하는 문제

서로 다른 오브젝트가 같은 StaticMesh를 사용하면 메시 에셋 주소만 비교해서는 선택 변경을 감지할 수 없었습니다. 마지막 메시 에셋과 마지막 컴포넌트를 함께 추적하고 둘 중 하나가 바뀌면 섹션 인덱스와 머티리얼 포인터를 초기화했습니다.

### C++과 HLSL 상수 버퍼 레이아웃이 어긋나는 문제

C++의 `bool`과 HLSL 상수 버퍼의 논리 플래그는 크기와 패킹 방식이 달라 뒤의 필드가 잘못 읽힐 수 있었습니다. 플래그를 양쪽 모두 32비트 정수로 바꾸고 padding을 명시해 16바이트 경계를 맞췄습니다.

### 한글 경로에서 바이너리 캐시를 저장·로드하지 못하는 문제

UTF-8 문자열을 그대로 `ifstream`과 `ofstream`에 전달하면 Windows의 지역 코드 페이지에 따라 파일을 찾지 못했습니다. 모든 파일 접근 전에 UTF-8을 wide path로 변환하고, 캐시 경로는 `generic_wstring`에서 다시 UTF-8로 변환해 저장·검증·로드에 동일한 규칙을 사용했습니다.

## 코드 아카이브

```text
week04/
├── README.md
├── NOTICE.md
└── Project/
    ├── W4_Jungle_Team2.sln
    ├── nuget.config
    └── W4_Jungle_Team2/
        ├── Asset/          # OBJ·MTL·텍스처·폰트·파티클
        ├── Settings/       # 에디터·ObjViewer 설정
        ├── Shaders/        # HLSL 셰이더
        ├── Source/         # 엔진·에디터·ObjViewer 코드
        ├── ThirdParty/     # Dear ImGui 등 포함형 코드
        ├── packages.config
        └── *.vcxproj
```

원본 저장소의 최종 상태에서 솔루션, 프로젝트 소스, 셰이더와 실행 에셋을 보관했습니다. 기존 Release 배포본, 내장 Python 런타임, 크래시 덤프, NuGet 복원 폴더, IDE 캐시와 새 빌드 산출물은 포함하지 않았습니다.

## 빌드와 실행

- Windows 10/11
- Visual Studio 2022와 Desktop development with C++ 워크로드
- Windows 10 SDK, MSVC v143
- C++20
- DirectX 11 지원 GPU

Visual Studio에서 `Project/W4_Jungle_Team2.sln`을 열어 NuGet 패키지를 복원한 뒤 빌드할 수 있습니다. Developer PowerShell에서는 다음과 같이 실행할 수 있습니다.

```powershell
msbuild .\Project\W4_Jungle_Team2.sln /t:Restore /p:RestorePackagesConfig=true
msbuild .\Project\W4_Jungle_Team2.sln /m /p:Configuration=Release /p:Platform=x64
.\Project\W4_Jungle_Team2\Bin\Release\W4_Jungle_Team2.exe
```

최종 소스는 Visual Studio 2022의 `Release | x64` 구성으로 빌드에 성공했고, 생성된 프로그램 창이 정상적으로 실행되는 것을 확인했습니다. `ObjViewer | x64` 구성은 OBJ 모델 확인에 집중한 별도 실행 모드입니다.

## 기술 스택

- C++20
- DirectX 11, HLSL
- Win32 API
- Dear ImGui
- DirectX Tool Kit for DirectX 11
- SimpleJSON
- Visual Studio 2022, MSVC v143

## 회고

4주차에는 파일을 파싱하는 기능만으로 에셋 파이프라인이 완성되지 않는다는 점을 경험했습니다. OBJ의 섹션과 MTL 슬롯을 보존하고, Resource Manager의 캐시와 GPU 텍스처, 렌더 커맨드, Material Editor까지 같은 데이터 관계를 유지해야 모델이 의도한 모습으로 출력됐습니다.

또한 상대 경로, 다국어 파일명, C++과 HLSL의 메모리 정렬처럼 개발 환경에서는 쉽게 지나칠 수 있는 경계 조건이 실제 에셋 로딩과 렌더링을 막았습니다. 경로 표현을 UTF-8과 wide path 사이에서 명확히 변환하고, 상수 버퍼 레이아웃과 섹션 인덱스의 의미를 분리하면서 데이터가 파일에서 화면까지 이동하는 전체 흐름을 다듬은 주차였습니다.
