# ✨ Week 12 — Particle System & HDR Bloom

> DirectX 11 엔진에 편집 가능한 파티클 시스템을 구축하고 HDR Bloom 후처리로 발광 효과를 연결했습니다.

[← 전체 주차 보기](../README.md)

## 프로젝트 정보

- 주제: 파티클 시스템과 HDR Bloom
- 작업 기간: 2026.05.22. ~ 2026.05.28. (원본 저장소 이력 기준)
- 참여 인원: 3명
- 원본 저장소: [Jungle_Week12_Team1](https://github.com/CaptainTangerine/Jungle_Week12_Team1)
- 공개 프로젝트: [Project](Project/) · [KraftonEngine.sln](Project/KraftonEngine.sln)

## 프로젝트 개요

이전 주차의 엔진·에디터에 파티클 에셋, 시뮬레이션, 렌더링과 편집 기능을 더했습니다. Unreal Engine의 [Cascade](https://dev.epicgames.com/documentation/en-us/unreal-engine/cascade-particle-editor-reference?application_version=4.27)를 참고해 파티클 효과를 Emitter와 Module의 조합으로 구성했습니다. 스프라이트·메시·리본·빔 등 여러 형태의 파티클을 다루고 벡터 필드와 이벤트로 움직임을 확장했습니다. HDR 장면 색상에 Bloom을 합성해 파티클의 발광 표현도 구현했습니다.

## Cascade를 참고한 설계와 Payload

Cascade의 `ParticleSystem → Emitter → Module` 구성은 하나의 효과에 여러 Emitter를 넣고, 각 Emitter의 스폰·이동·색·충돌 같은 동작을 모듈로 조합할 수 있습니다. 효과마다 시뮬레이션 코드를 따로 만드는 대신 에셋과 모듈 설정을 바꿔 재사용하고, LOD별 구성을 편집하기 위해 이 방식을 참고했습니다. 우리 엔진에서는 `ParticleSystem → Emitter → LODLevel → Module` 계층으로 구현했습니다.

모듈마다 필요한 실행 상태는 다릅니다. 이를 모두 `FBaseParticle`에 고정 필드로 넣으면 사용하지 않는 모듈의 데이터까지 모든 입자가 가지게 됩니다. **Payload**는 기본 입자 데이터 뒤에 필요한 모듈의 상태만 추가로 배치하는 공간입니다. Emitter가 활성 모듈의 `GetParticlePayloadSize()`를 모아 정렬된 오프셋과 입자 stride를 계산하고, 모듈은 캐시된 오프셋으로 자기 데이터를 읽고 갱신합니다. 예를 들어 SubUV는 현재 이미지 인덱스, 충돌 모듈은 남은 충돌 횟수·감쇠·지연 상태를 입자별 Payload에 저장합니다.

입자별 상태와 별도로 **Instance Payload**는 Emitter 인스턴스에서 모듈이 공유하는 상태를 보관합니다. 이동 거리에 따라 입자를 생성하는 `SpawnPerUnit` 모듈은 이전 위치와 소수점 스폰 잔여량을 여기에 저장합니다. 이 구분으로 모듈을 추가해도 기본 입자 구조를 계속 늘리지 않고, 입자마다 필요한 값과 Emitter 전체에 한 번만 필요한 값을 각각 관리할 수 있습니다.

## 팀 전체 구현

- 파티클 시스템·Emitter·LOD·Module의 에셋 구조와 런타임 시뮬레이션
- 스프라이트·메시·리본·빔 파티클 렌더링과 SubUV·벡터 필드 효과
- 파티클 편집 뷰포트, 속성·곡선·LOD 편집과 쇼케이스 씬
- HDR Bloom, 노출·톤 매핑과 감마 보정을 포함한 후처리

## 개인 기여

### 파티클 데이터와 시뮬레이션 기반

- `ParticleSystem → Emitter → LODLevel → Module` 계층을 구성하고 편집한 파티클 설정을 에셋으로 저장·불러올 수 있게 했습니다. Emitter별 스폰·업데이트 모듈을 분리해 효과를 조합할 수 있도록 했습니다.
- `ParticleSystemComponent`와 Emitter 인스턴스, 동적 렌더 데이터를 연결해 에셋 설정이 시뮬레이션과 렌더 경로로 이어지도록 했습니다. 빔·리본 형태를 위한 Emitter 데이터와 렌더 전달 구조의 기반도 만들었습니다.
- 수명·크기·색·속도·스폰율 등에 고정값만 넣던 구조를 분포 값으로 확장했습니다. 상수·범위·커브에서 값을 평가하도록 해 같은 모듈로 시간 변화와 무작위 변화를 표현했습니다.
- 모듈별 Particle·Instance Payload 크기와 오프셋을 계산해 런타임 캐시에 저장했습니다. 각 모듈이 자신의 상태를 기본 입자 구조와 분리해 관리하도록 했습니다.

### 이벤트·충돌·애니메이션 모듈

- 파티클 생성·소멸·충돌 이벤트를 발생시키는 모듈과 이를 받는 모듈을 구현했습니다. 서로 다른 Emitter 사이에서도 이벤트를 전달해 한 효과가 다른 효과의 스폰을 유도할 수 있게 했습니다.
- 충돌 모듈에 월드 충돌 검사, 반사·감쇠, 최대 충돌 횟수와 종료 동작을 적용하고 충돌 이벤트를 연결했습니다. SubImage 모듈을 추가해 스프라이트 시트의 프레임을 파티클 생애에 맞춰 변경했습니다.

### HDR Bloom과 렌더 순서 문제 해결

- HDR SceneColor에서 밝은 영역을 추출하고 다운샘플·블러·합성하는 Bloom 패스를 구현했습니다. 임계값, 강도, 반경, soft knee와 노출 값을 조절할 수 있게 하고 파티클 발광 표현에 연결했습니다.
- 감마 보정을 켜면 Bloom이 사라지는 현상을 추적했습니다. 렌더 패스 번호가 정렬 키의 비트 폭을 초과해 명령 구간이 뒤섞이는 문제를 수정하고 Bloom 합성 뒤 톤 매핑과 sRGB 변환이 수행되도록 정리했습니다. MRT 출력과 렌더 타깃 바인딩이 맞지 않아 발생한 D3D11 경고도 해결했습니다.
- 구현 흐름과 오류 분석을 [HDR Bloom 구현 가이드](Project/Docs/HDRBloomImplementationGuide.md)와 [후처리 문제 해결 기록](Project/Docs/HDRBloomGammaCorrectionPostmortem.md)으로 남겼습니다.

## 문제 → 적용 방법 → 결과 → 개선 방향

| 문제 | 적용 방법 | 결과 | 개선 방향 |
|---|---|---|---|
| 효과마다 파티클 동작을 코드에 고정해야 함 | System·Emitter·LOD·Module 계층과 에셋 직렬화, 분포 값 도입 | 모듈 조합과 값·커브 편집으로 여러 효과를 구성 | 모듈 간 의존성과 기본값을 에디터에서 더 명확히 표시 |
| 모듈별 상태를 모두 기본 입자에 넣으면 입자 데이터가 불필요하게 커짐 | 모듈별 Particle Payload와 Emitter Instance Payload의 크기·오프셋 계산 | 활성 모듈에 필요한 상태만 각 실행 단위에 배치 | LOD 전환 시 Payload 보존 범위와 메모리 사용량 점검 |
| 한 파티클의 충돌·소멸이 다른 효과로 이어지지 않음 | 이벤트 발생·수신 모듈과 Emitter 간 전달, 충돌 이벤트 연결 | 연쇄 스폰과 반응형 효과를 구성 | 다량 이벤트의 처리량과 순서 관리 |
| 발광 파티클이 일반 장면 색상에서 빛번짐 없이 표시됨 | HDR SceneColor 기반 prefilter·downsample·blur·composite | 밝은 파티클 주변에 Bloom을 합성 | 장면 규모별 품질·비용 조절 |
| 감마 보정 활성화 시 Bloom 결과가 사라짐 | 렌더 패스 정렬 키의 비트 폭과 후처리 순서 수정 | Bloom 합성 결과가 최종 출력까지 유지 | 렌더 패스 추가 시 정렬 키 범위와 타깃 바인딩 점검 자동화 |

## 코드 아카이브

```text
week12/
├── README.md
├── NOTICE.md
└── Project/
    ├── KraftonEngine.sln
    ├── Docs/                         # HDR Bloom 구현·문제 해결 기록
    ├── KraftonEngine/
    │   ├── Content/                  # 파티클 에셋·쇼케이스 씬
    │   ├── Shaders/PostProcess/      # Bloom·감마 보정 셰이더
    │   ├── Source/Engine/Particle/   # 파티클 데이터·모듈·시뮬레이션
    │   ├── Source/Editor/            # 파티클 편집 UI
    │   └── ThirdParty/
    └── Scripts/
```

원본 `main`의 최종 추적 파일 1,944개를 보관했습니다. 원본 Git 이력과 빌드 산출물은 아카이브에 포함하지 않았습니다.

## 빌드와 실행

Windows에서 Visual Studio 2022 C++ 워크로드, Windows SDK, MSVC v143으로 `Project/KraftonEngine.sln`의 `Release | x64` 구성을 빌드합니다. 프로젝트의 FBX SDK·FMOD·Lua·PhysX 등 서드파티 의존성과 NuGet 패키지가 필요합니다. 실행 시 작업 디렉터리는 `Project/KraftonEngine`으로 설정합니다.

## 기술 스택

C++, DirectX 11, HLSL, ImGui, FBX SDK, Lua, FMOD, PhysX

## 회고

Cascade의 모듈식 구성을 참고하면서 효과의 재사용성과 실행 상태의 저장 방식을 함께 설계해야 했습니다. Payload로 모듈별 상태를 분리하고 편집 가능한 데이터, Emitter 간 이벤트, 시뮬레이션과 렌더 데이터를 연결해 여러 효과를 조합할 수 있었습니다. Bloom 문제를 해결하면서 후처리 알고리즘뿐 아니라 렌더 명령의 정렬 범위와 색 공간 변환 순서까지 함께 살펴야 최종 화면이 의도대로 나온다는 점을 배웠습니다.
