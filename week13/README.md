# 🧵 Week 13 — PhysX & NvCloth

> PhysX 물리 기반을 정리하고 충돌체·제약 조건을 연결한 뒤, NvCloth 의상 시뮬레이션과 에셋 편집 기능을 구현했습니다.

## 프로젝트 정보

- 주제: 물리 시뮬레이션과 Cloth 시스템
- 작업 기간: 2026.05.29. ~ 2026.06.04. (원본 저장소 이력 기준)
- 참여 인원: 3명
- 원본 저장소: [Jungle_Week13_Team4](https://github.com/CaptainTangerine/Jungle_Week13_Team4)
- 공개 프로젝트: [Project](Project/) · [KraftonEngine.sln](Project/KraftonEngine.sln)

## 프로젝트 개요

이전 주차의 엔진에 PhysX 기반 강체·충돌·제약 물리를 연결하고, 스켈레탈 캐릭터의 래그돌과 차량 시연 장면을 확장했습니다. NvCloth를 사용해 메시로부터 의상 에셋을 생성하고, 에디터에서 고정점과 시뮬레이션 속성을 조정하며 캐릭터 본과 물리 충돌체를 따라 움직이도록 했습니다. 후처리에서는 DOF와 Post Process Volume도 추가했습니다.

## 팀 전체 구현

- PhysX 물리 장면, 강체·충돌체·제약 조건과 스켈레탈 래그돌
- NvCloth 의상 에셋 생성·편집, 캐릭터 부착과 충돌 연동
- 차량 구동·오디오 시연과 물리 에셋 디버그 렌더링
- Depth of Field 후처리, Post Process Volume과 데모 씬

## 개인 기여

### PhysX 물리 기반과 컴포넌트 연결

- PhysX SDK를 프로젝트에 통합하고, 물리 장면을 PhysX 경로로 정리했습니다. 장면 생성 설정과 실패 처리를 보완하고, 컴포넌트의 물리 상태 생성 시점에 강체를 등록하는 시작 경로를 만들었습니다.
- `FBodyInstance`와 `UBodySetup`을 통해 충돌 형상과 런타임 물리 액터를 연결했습니다. 스켈레탈 메시의 PhysicsAsset을 저장·불러와 본별 바디를 초기화하고, `FConstraintInstance`를 통해 본 사이의 제약 조건을 PhysX 조인트에 연결했습니다.
- 정적 메시가 공유하는 BodySetup과 기본 충돌 형상을 구성하고, 삼각형 메시를 PhysX 충돌 데이터로 조리해 정적 메시 충돌에 사용할 수 있게 했습니다. 에디터에서 충돌체를 확인할 수 있도록 시각화도 추가했습니다.
- PhysX Visual Debugger(PVD)의 연결·전송 설정을 프로젝트 옵션에 넣어 물리 장면의 액터, 접촉과 제약 상태를 외부 도구에서 확인할 수 있게 했습니다.

### NvCloth 에셋과 런타임 시뮬레이션

- NvCloth의 CPU Factory와 Solver를 엔진 수명주기에 연결했습니다. `ClothAsset`, `ClothComponent`, 렌더 프록시를 추가해 의상 데이터를 시뮬레이션하고 변형된 메시를 화면에 전달했습니다.
- 원본 메시에서 Cloth 에셋을 생성하는 빌더와 저장·불러오기 경로를 구현했습니다. 위치가 같은 정점을 용접하고, 삼각형 연결에서 굽힘 제약과 고정점 기반 tether를 만들도록 확장했습니다.
- Cloth를 스켈레탈 메시의 본에 부착하고 PhysicsAsset의 충돌체를 Solver에 전달했습니다. 본·충돌체의 이동을 프레임마다 동기화하고, 고정점 주변 충돌과 자기 충돌 처리로 생기는 흔들림을 조정했습니다.

### Cloth 에디터와 안정화

- Cloth 전용 에셋 에디터와 뷰포트를 구현해 메시를 가져오고 정점 고정 상태, 용접·굽힘 제약, 물리 속성을 편집·저장할 수 있게 했습니다. 에셋 생성 기능을 메시 에디터와 Content Browser에도 연결했습니다.
- 시뮬레이션 Tick 순서, 본 부착 변환, Solver 빈도·입력 값과 충돌체 우선순위를 조정해 의상이 캐릭터의 움직임을 따라가도록 보완했습니다. 정적 메시가 물리 장면에 등록되지 않던 문제도 수정했습니다.

## 문제 → 적용 방법 → 결과 → 개선 방향

| 문제 | 적용 방법 | 결과 | 개선 방향 |
|---|---|---|---|
| 컴포넌트와 물리 장면의 생성 시점·충돌 데이터가 분리됨 | PhysX 물리 장면으로 경로 정리, 물리 상태 생성 시 바디 등록, BodySetup·BodyInstance 연결 | 정적·스켈레탈 메시가 물리 장면의 액터와 충돌체를 사용 | 컴포넌트 수명주기와 물리 리소스 해제 경로의 자동 점검 |
| 스켈레탈 메시의 본별 바디가 서로 독립적으로 움직임 | PhysicsAsset 로드, 본별 바디 초기화와 PhysX 제약 조인트 연결 | 본 계층에 맞는 래그돌 물리 기반 구축 | 다양한 본 구조와 제약 범위에 대한 사례 확대 |
| 의상 메시를 효과적으로 시뮬레이션·편집하기 어려움 | 메시 용접, 굽힘·tether 생성, NvCloth Solver 및 전용 에디터 | 에셋에서 고정점과 물리 속성을 조정해 의상 시뮬레이션 | 메시 토폴로지별 자동 생성 결과와 편집 피드백 개선 |
| 의상이 본·물리 충돌체와 어긋나거나 흔들림 | 부착 변환·Tick 순서·Solver 입력 동기화, 고정점 주변 충돌 조정 | 움직이는 캐릭터에 의상을 부착하고 충돌 반응을 연결 | 급격한 애니메이션·큰 프레임 간격에서 안정성 개선 |

## 코드 아카이브

```text
week13/
├── README.md
├── NOTICE.md
└── Project/
    ├── KraftonEngine.sln
    ├── KraftonEngine/
    │   ├── Content/                  # Cloth·차량·데모 씬과 에셋
    │   ├── Source/Engine/Physics/    # PhysX, BodySetup, NvCloth
    │   ├── Source/Engine/Component/  # 물리·Cloth 컴포넌트
    │   ├── Source/Editor/            # Cloth 에셋 에디터
    │   ├── Shaders/                  # 물리 디버그·DOF 후처리
    │   └── ThirdParty/               # PhysX·NvCloth 등 의존성
    └── Scripts/
```

원본 `main`의 최종 추적 파일 3,546개를 보관했습니다. 원본 Git 이력과 별도의 로컬 빌드 산출물은 아카이브에 포함하지 않았습니다.

## 빌드와 실행

Windows에서 Visual Studio 2022 C++ 워크로드, Windows SDK, MSVC v143으로 `Project/KraftonEngine.sln`의 `Release | x64` 구성을 빌드합니다. 프로젝트에 포함된 PhysX·NvCloth·FBX SDK·FMOD와 다른 서드파티 의존성이 필요합니다. 실행 시 작업 디렉터리는 `Project/KraftonEngine`으로 설정합니다.

## 기술 스택

C++, DirectX 11, PhysX, NvCloth, ImGui, FBX SDK, Lua, FMOD

## 회고

물리 기능은 SDK를 연결하는 것만으로 완성되지 않았습니다. 컴포넌트가 언제 바디를 만들고 제거하는지, 에셋의 충돌 형상이 어떻게 물리 장면에 들어가는지, 본과 Cloth Solver가 어느 시점의 변환을 공유하는지까지 맞춰야 했습니다. 의상의 흔들림을 다루면서 시뮬레이션 안정성은 제약 구성과 충돌체 선택, Tick 순서가 함께 결정한다는 점을 배웠습니다.
