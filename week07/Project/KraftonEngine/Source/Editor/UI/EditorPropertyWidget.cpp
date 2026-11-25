#include "Editor/UI/EditorPropertyWidget.h"

#include "Editor/EditorEngine.h"
#include "Editor/UI/EditorFileUtils.h"
#include "Editor/UI/EditorUIHelpers.h"
#include "Editor/UI/UIStringHelpers.h"

#include "ImGui/imgui.h"
#include "Component/GizmoComponent.h"
#include "Component/LightComponent.h"
#include "Component/MovementComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/SceneComponent.h"
#include "Component/DecalComponent.h"
#include "Core/PropertyTypes.h"
#include "Core/ClassTypes.h"
#include "Resource/ResourceManager.h"
#include "Object/FName.h"
#include "Object/ObjectIterator.h"
#include "Materials/Material.h"
#include "Mesh/ObjManager.h"
#include "Mesh/StaticMesh.h"

#include <algorithm>
#include <cfloat>
#include <cstring>

#include "Materials/MaterialManager.h"

#define SEPARATOR(); ImGui::Spacing(); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); ImGui::Spacing();

namespace
{
	bool ShouldHideInComponentTree(const UActorComponent* Component, bool bShowEditorOnlyComponents)
	{
		if (!Component)
		{
			return true;
		}

		return Component->IsHiddenInComponentTree()
			&& !(bShowEditorOnlyComponents && Component->IsEditorOnlyComponent());
	}
}

void FEditorPropertyWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	ImGui::SetNextWindowSize(ImVec2(350.0f, 500.0f), ImGuiCond_Once);
	if (!ImGui::Begin(GetWindowName(), GetOpenStatePtr()))
	{
		ImGui::End();
		return;
	}

	FSelectionManager& Selection = EditorEngine->GetSelectionManager();
	AActor* PrimaryActor = Selection.GetPrimarySelection();
	if (!PrimaryActor)
	{
		SelectedComponent = nullptr;
		LastSelectedActor = nullptr;
		bActorSelected = true;
		ImGui::Text("No object selected.");
		ImGui::End();
		return;
	}

	// Actor 선택이 바뀌면 초기화
	if (PrimaryActor != LastSelectedActor)
	{
		SelectedComponent = nullptr;
		LastSelectedActor = PrimaryActor;
		bActorSelected = true;
	}

	const TArray<AActor*>& SelectedActors = Selection.GetSelectedActors();
	const int32 SelectionCount = static_cast<int32>(SelectedActors.size());

	// ========== 고정 영역: Actor Info (clickable) ==========
	if (SelectionCount > 1)
	{
		ImGui::Text("Class: %s", PrimaryActor->GetClass()->GetName());

		FString PrimaryName = PrimaryActor->GetFName().ToString();
		if (PrimaryName.empty()) PrimaryName = PrimaryActor->GetClass()->GetName();

		bool bHighlight = bActorSelected;
		if (bHighlight) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
		ImGui::Text("Name: %s (+%d)", PrimaryName.c_str(), SelectionCount - 1);
		if (bHighlight) ImGui::PopStyleColor();
		if (ImGui::IsItemClicked())
		{
			bActorSelected = true;
			SelectedComponent = nullptr;
		}
		ImGui::SameLine();
		char RemoveLabel[64];
		snprintf(RemoveLabel, sizeof(RemoveLabel), "Remove %d Objects", SelectionCount);
		if (ImGui::Button(RemoveLabel))
		{
			for (AActor* Actor : SelectedActors)
			{
				if (Actor && Actor->GetWorld())
				{
					Actor->GetWorld()->DestroyActor(Actor);
				}
			}
			Selection.ClearSelection();
			SelectedComponent = nullptr;
			LastSelectedActor = nullptr;
			ImGui::End();
			return;
		}
	}
	else
	{
		ImGui::Text("Class: %s", PrimaryActor->GetClass()->GetName());

		// Actor 이름: 클릭 가능, 선택 시 하이라이트
		bool bHighlight = bActorSelected;
		if (bHighlight) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
		ImGui::Text("Name: %s", PrimaryActor->GetFName().ToString().c_str());
		if (bHighlight) ImGui::PopStyleColor();
		if (ImGui::IsItemClicked())
		{
			bActorSelected = true;
			SelectedComponent = nullptr;
		}
		ImGui::SameLine();
		if (ImGui::Button("Remove##Actor"))
		{
			if (PrimaryActor->GetWorld())
			{
				PrimaryActor->GetWorld()->DestroyActor(PrimaryActor);
			}
			Selection.ClearSelection();
			SelectedComponent = nullptr;
			LastSelectedActor = nullptr;
			ImGui::End();
			return;
		}
	}

	// ========== 고정 영역: Component Tree ==========
	SEPARATOR();
	ImGui::Checkbox("Show Editor-Only Components", &bShowEditorOnlyComponents);
	ImGui::Separator();
	RenderComponentTree(PrimaryActor);

	// ========== Details ==========
	SEPARATOR();
	ImGui::Text("Details");
	ImGui::Separator();
	RenderDetails(PrimaryActor, SelectedActors);

	ImGui::End();
}

void FEditorPropertyWidget::RenderDetails(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors)
{
	if (bActorSelected)
	{
		RenderActorProperties(PrimaryActor, SelectedActors);
	}
	else if (SelectedComponent)
	{
		RenderComponentProperties(PrimaryActor);
	}
	else
	{
		ImGui::TextDisabled("Select an actor or component to view details.");
	}
}

void FEditorPropertyWidget::RenderActorProperties(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors)
{
	ImGui::Text("Actor: %s", PrimaryActor->GetClass()->GetName());
	ImGui::Text("Name: %s", PrimaryActor->GetFName().ToString().c_str());

	if (PrimaryActor->GetRootComponent())
	{
		ImGui::Separator();
		ImGui::Text("Transform");
		ImGui::Spacing();

		FVector Pos = PrimaryActor->GetActorLocation();
		float PosArray[3] = { Pos.X, Pos.Y, Pos.Z };

		USceneComponent* RootComp = PrimaryActor->GetRootComponent();

		FVector Scale = PrimaryActor->GetActorScale();
		float ScaleArray[3] = { Scale.X, Scale.Y, Scale.Z };

		if (FEditorUIHelpers::DrawInlineVec3Control("Location", PosArray, 0.1f))
		{
			FVector Delta = FVector(PosArray[0], PosArray[1], PosArray[2]) - Pos;
			for (AActor* Actor : SelectedActors)
			{
				if (Actor) Actor->AddActorWorldOffset(Delta);
			}
			EditorEngine->GetGizmo()->UpdateGizmoTransform();
		}
		{
			// FRotator의 내부 오일러 저장 순서는 Pitch/Yaw/Roll이지만, 에디터는 축 방향과 맞추기 위해
			// X=Roll, Y=Pitch, Z=Yaw 순서로 노출한다. 또한 여기서는 최종 컴포넌트 회전을 바로 덮어쓰지 않고
			// CachedEditRotator를 수정한 뒤 ApplyCachedEditRotator()로 반영하는데, 이는 편집 중인 오일러 입력 상태를
			// 유지한 채 컴포넌트 트랜스폼에 안전하게 커밋하기 위해서다.
			FRotator& CachedRot = RootComp->GetCachedEditRotator();
			FRotator PrevRot = CachedRot;
			float RotXYZ[3] = { CachedRot.Roll, CachedRot.Pitch, CachedRot.Yaw };

			if (FEditorUIHelpers::DrawInlineVec3Control("Rotation", RotXYZ, 0.1f))
			{
				CachedRot.Roll = RotXYZ[0];
				CachedRot.Pitch = RotXYZ[1];
				CachedRot.Yaw = RotXYZ[2];

				if (SelectedActors.size() > 1)
				{
					// 다중 선택 회전 편집에서는 기준 액터의 오일러 델타를 다른 선택된 루트에도 전파하여
					// 이번 프레임의 에디터 조작 결과가 모든 선택 액터에 같은 방식으로 적용되도록 한다.
					FRotator Delta = CachedRot - PrevRot;
					for (AActor* Actor : SelectedActors)
					{
						if (!Actor || Actor == PrimaryActor) continue;
						USceneComponent* Root = Actor->GetRootComponent();
						if (Root)
						{
							FRotator Other = Root->GetCachedEditRotator();
							Root->SetRelativeRotation(Other + Delta);
						}
					}
				}
				RootComp->ApplyCachedEditRotator();
				EditorEngine->GetGizmo()->UpdateGizmoTransform();
			}
		}
		if (FEditorUIHelpers::DrawInlineVec3Control("Scale", ScaleArray, 0.1f))
		{
			FVector Delta = FVector(ScaleArray[0], ScaleArray[1], ScaleArray[2]) - Scale;
			for (AActor* Actor : SelectedActors)
			{
				if (Actor) Actor->SetActorScale(Actor->GetActorScale() + Delta);
			}
		}


	}

	ImGui::Separator();
	bool bVisible = PrimaryActor->IsVisible();
	if (FEditorUIHelpers::DrawInlineCheckboxProperty("Visible", &bVisible))
	{
		PrimaryActor->SetVisible(bVisible);
	}

}

void FEditorPropertyWidget::RenderComponentTree(AActor* Actor)
{
	ImGui::Text("Components");

	if (SelectedComponent && ShouldHideInComponentTree(SelectedComponent, bShowEditorOnlyComponents))
	{
		SelectedComponent = nullptr;
		bActorSelected = true;
	}

	// Get All Component Classes
	TArray<UClass*>& AllClasses = UClass::GetAllClasses();

	TArray<UClass*> ComponentClasses;
	for (UClass* Cls : AllClasses)
	{
		if (Cls->IsA(UActorComponent::StaticClass()) && !Cls->HasAnyClassFlags(CF_Abstract))
			ComponentClasses.push_back(Cls);
	}

	std::sort(ComponentClasses.begin(), ComponentClasses.end(),
		[](const UClass* A, const UClass* B)
		{
			return strcmp(A->GetName(), B->GetName()) < 0;
		});

	auto GetComponentPopupGroupClass = [](UClass* Cls) -> UClass*
	{
		if (!Cls)
		{
			return nullptr;
		}


		/**
		 * 아래 리스트는 아주 중요합니다!.
		 * 리스트 내에 있는 컴포넌트들이 분류를 위한 기준이 되어
		 * 컴포넌트 리스트에서 --- 표시와 함께 나타납니다.
		 * \param Actor
		 */
		static UClass* GroupAnchors[] = {
			ULightComponent::StaticClass(),
			UMovementComponent::StaticClass(),
			UPrimitiveComponent::StaticClass(),
			USceneComponent::StaticClass(),
		};

		for (UClass* CurrentClass = Cls; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
		{
			for (UClass* AnchorClass : GroupAnchors)
			{
				if (CurrentClass == AnchorClass)
				{
					return AnchorClass;
				}
			}
		}

		for (UClass* ParentClass = Cls->GetSuperClass(); ParentClass; ParentClass = ParentClass->GetSuperClass())
		{
			if (!ParentClass->IsA(UActorComponent::StaticClass()))
			{
				break;
			}
			if (ParentClass->HasAnyClassFlags(CF_Abstract))
			{
				return ParentClass;
			}
		}

		return nullptr;
	};

	TArray<UClass*> SeparatorParents;
	TArray<UClass*> UngroupedClasses;
	for (UClass* Cls : ComponentClasses)
	{
		UClass* GroupClass = GetComponentPopupGroupClass(Cls);
		if (GroupClass)
		{
			bool bAlreadyAdded = false;
			for (UClass* ExistingParent : SeparatorParents)
			{
				if (ExistingParent == GroupClass)
				{
					bAlreadyAdded = true;
					break;
				}
			}

			if (!bAlreadyAdded)
			{
				SeparatorParents.push_back(GroupClass);
			}
		}
		else
		{
			UngroupedClasses.push_back(Cls);
		}
	}

	std::sort(SeparatorParents.begin(), SeparatorParents.end(),
		[](const UClass* A, const UClass* B)
		{
			return strcmp(A->GetName(), B->GetName()) < 0;
		});

	static UClass* SelectedClass = nullptr;
	auto IsCurrentSelectionValid = [&]()
	{
		for (UClass* Cls : ComponentClasses)
		{
			if (Cls == SelectedClass)
			{
				return true;
			}
		}
		return false;
	};

	if (ComponentClasses.empty())
	{
		SelectedClass = nullptr;
	}
	else if (!IsCurrentSelectionValid())
	{
		SelectedClass = ComponentClasses.front();
	}
	const char* Preview = SelectedClass ? SelectedClass->GetName() : "None";

	const float PopupMinWidth = 220.0f;
	const ImVec2 PopupMinSize(PopupMinWidth, 320.0f);
	const ImVec2 PopupMaxSize(FLT_MAX, 520.0f);
	const ImGuiStyle& Style = ImGui::GetStyle();
	const float ComboWidth = ImGui::GetContentRegionAvail().x;
	const float ComboHeight = ImGui::GetFrameHeight();
	const ImVec2 ComboButtonSize(ComboWidth, ComboHeight);
	if (ImGui::InvisibleButton("##ComponentClassButton", ComboButtonSize))
	{
		ImGui::OpenPopup("##ComponentClassPopup");
	}

	const ImVec2 ComboMin = ImGui::GetItemRectMin();
	const ImVec2 ComboMax = ImGui::GetItemRectMax();
	const bool bHovered = ImGui::IsItemHovered();
	const bool bPopupOpen = ImGui::IsPopupOpen("##ComponentClassPopup");
	ImDrawList* DrawList = ImGui::GetWindowDrawList();

	const ImU32 FrameColor = ImGui::GetColorU32((bHovered || bPopupOpen) ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg);
	const ImU32 BorderColor = ImGui::GetColorU32(ImGuiCol_Border);
	const ImU32 TextColor = ImGui::GetColorU32(ImGuiCol_Text);
	const float ArrowWidth = ComboHeight;
	const ImVec2 ArrowMin(ComboMax.x - ArrowWidth, ComboMin.y);
	const ImVec2 ArrowMax(ComboMax.x, ComboMax.y);
	const float Rounding = Style.FrameRounding;

	DrawList->AddRectFilled(ComboMin, ComboMax, FrameColor, Rounding);
	DrawList->AddRect(ComboMin, ComboMax, BorderColor, Rounding);
	DrawList->AddLine(ImVec2(ArrowMin.x, ArrowMin.y), ImVec2(ArrowMin.x, ArrowMax.y), BorderColor);

	const float ArrowCenterX = (ArrowMin.x + ArrowMax.x) * 0.5f;
	const float ArrowCenterY = (ArrowMin.y + ArrowMax.y) * 0.5f;
	DrawList->AddTriangleFilled(
		ImVec2(ArrowCenterX - 4.0f, ArrowCenterY - 2.0f),
		ImVec2(ArrowCenterX + 4.0f, ArrowCenterY - 2.0f),
		ImVec2(ArrowCenterX, ArrowCenterY + 3.0f),
		TextColor);

	const ImVec2 TextMin(ComboMin.x + Style.FramePadding.x, ComboMin.y + Style.FramePadding.y);
	const ImVec2 TextMax(ArrowMin.x - Style.FramePadding.x, ComboMax.y - Style.FramePadding.y);
	DrawList->PushClipRect(TextMin, TextMax, true);
	DrawList->AddText(TextMin, TextColor, Preview);
	DrawList->PopClipRect();

	ImGui::SetNextWindowPos(ComboMin, ImGuiCond_Appearing, ImVec2(1.0f, 0.0f));
	ImGui::SetNextWindowSizeConstraints(PopupMinSize, PopupMaxSize);
	if (ImGui::BeginPopup("##ComponentClassPopup"))
	{
		int ParentIndex = 0;
		int UngroupedIndex = 0;
		while (ParentIndex < static_cast<int>(SeparatorParents.size()) || UngroupedIndex < static_cast<int>(UngroupedClasses.size()))
		{
			const bool bHasParentGroup = ParentIndex < static_cast<int>(SeparatorParents.size());
			const bool bHasUngrouped = UngroupedIndex < static_cast<int>(UngroupedClasses.size());
			const bool bUseParentGroup =
				bHasParentGroup &&
				(!bHasUngrouped || strcmp(SeparatorParents[ParentIndex]->GetName(), UngroupedClasses[UngroupedIndex]->GetName()) <= 0);

			if (bUseParentGroup)
			{
				UClass* ParentClass = SeparatorParents[ParentIndex++];
				FString SeparatorLabel = "----";
				SeparatorLabel += ParentClass->GetName();
				SeparatorLabel += "----";
				ImGui::TextDisabled("%s", SeparatorLabel.c_str());

				for (UClass* Cls : ComponentClasses)
				{
					if (GetComponentPopupGroupClass(Cls) != ParentClass)
					{
						continue;
					}

					bool bSelected = (SelectedClass == Cls);
					if (ImGui::Selectable(Cls->GetName(), bSelected))
					{
						SelectedClass = Cls;
						ImGui::CloseCurrentPopup();
					}
					if (bSelected)
						ImGui::SetItemDefaultFocus();
				}
			}
			else
			{
				UClass* Cls = UngroupedClasses[UngroupedIndex++];
				bool bSelected = (SelectedClass == Cls);
				if (ImGui::Selectable(Cls->GetName(), bSelected))
				{
					SelectedClass = Cls;
					ImGui::CloseCurrentPopup();
				}
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}
		}
		ImGui::EndPopup();
	}

	USceneComponent* Root = Actor->GetRootComponent();

	// Add Component
	if (SelectedClass && ImGui::Button("Add"))
	{
		UActorComponent* Comp = Actor->AddComponentByClass(SelectedClass);
		if (!Comp)
		{
			return;
		}

		if (SelectedClass->IsA(USceneComponent::StaticClass()))
		{
			if (SelectedComponent != nullptr && SelectedComponent->GetClass()->IsA(USceneComponent::StaticClass()))
				Cast<USceneComponent>(Comp)->AttachToComponent(Cast<USceneComponent>(SelectedComponent));
			else
				Cast<USceneComponent>(Comp)->AttachToComponent(Root);

			if (ULightComponentBase* LightComponent = Cast<ULightComponentBase>(Comp))
			{
				LightComponent->EnsureEditorBillboard();
			}
			else if(UDecalComponent* DecalComponent = Cast<UDecalComponent>(Comp))
			{
				DecalComponent->EnsureEditorBillboard();
			}
		}
	}

	ImGui::Separator();

	if (Root)
	{
		RenderSceneComponentNode(Root);
	}

	// Non-scene ActorComponents
	for (UActorComponent* Comp : Actor->GetComponents())
	{
		if (!Comp) continue;
		if (Comp->IsA<USceneComponent>()) continue;
		if (ShouldHideInComponentTree(Comp, bShowEditorOnlyComponents)) continue;

		FString Name = Comp->GetFName().ToString();
		const FString TypeName = Comp->GetClass()->GetName();
		const FString DefaultNamePrefix = TypeName + "_";
		const bool bUseTypeAsLabel = Name.empty()
			|| Name == TypeName
			|| Name.rfind(DefaultNamePrefix, 0) == 0;
		const char* Label = bUseTypeAsLabel ? TypeName.c_str() : Name.c_str();

		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (!bActorSelected && SelectedComponent == Comp)
			Flags |= ImGuiTreeNodeFlags_Selected;

		ImGui::TreeNodeEx(Comp, Flags, "%s", Label);
		if (ImGui::IsItemClicked())
		{
			SelectedComponent = Comp;
			bActorSelected = false;
		}
	}
}

void FEditorPropertyWidget::RenderSceneComponentNode(USceneComponent* Comp)
{
	if (!Comp) return;
	if (ShouldHideInComponentTree(Comp, bShowEditorOnlyComponents)) return;

	FString Name = Comp->GetFName().ToString();
	if (Name.empty()) Name = Comp->GetClass()->GetName();

	const auto& Children = Comp->GetChildren();
	bool bHasVisibleChildren = false;
	for (USceneComponent* Child : Children)
	{
		if (Child && !ShouldHideInComponentTree(Child, bShowEditorOnlyComponents))
		{
			bHasVisibleChildren = true;
			break;
		}
	}

	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
	if (!bHasVisibleChildren)
		Flags |= ImGuiTreeNodeFlags_Leaf;
	if (!bActorSelected && SelectedComponent == Comp)
		Flags |= ImGuiTreeNodeFlags_Selected;

	bool bIsRoot = (Comp->GetParent() == nullptr);
	bool bOpen = ImGui::TreeNodeEx(
		Comp, Flags, "%s%s (%s)",
		bIsRoot ? "[Root] " : "",
		Name.c_str(),
		Comp->GetClass()->GetName()
	);

	if (ImGui::IsItemClicked())
	{
		SelectedComponent = Comp;
		bActorSelected = false;
	}

	if (bOpen)
	{
		for (USceneComponent* Child : Children)
		{
			RenderSceneComponentNode(Child);
		}
		ImGui::TreePop();
	}
}

void FEditorPropertyWidget::RenderComponentProperties(AActor* Actor)
{
	ImGui::Text("Component: %s", SelectedComponent->GetClass()->GetName());
	ImGui::Text("Name: %s", SelectedComponent->GetFName().ToString().c_str());
	ImGui::SameLine();
	if (SelectedComponent != Actor->GetRootComponent())
	{
		if (ImGui::Button("Remove##Component"))
		{
			if (SelectedComponent != nullptr)
			{
				Actor->RemoveComponent(SelectedComponent);
				SelectedComponent = nullptr;
				return;
			}
		}
	}

	ImGui::Separator();

	// PropertyDescriptor 기반 자동 위젯 렌더링
	TArray<FPropertyDescriptor> Props;
	SelectedComponent->GetEditableProperties(Props);

	bool bIsRoot = false;
	if (SelectedComponent->IsA<USceneComponent>())
	{
		USceneComponent* SceneComp = static_cast<USceneComponent*>(SelectedComponent);
		bIsRoot = (SceneComp->GetParent() == nullptr);
	}

	// Transform 프로퍼티 이름 목록
	auto IsTransformProp = [](const FString& Name) {
		return Name == "Location"
			|| Name == "Rotation"
			|| Name == "Scale";
		};

	// Pass 1: Transform 프로퍼티 먼저 (Root가 아닐 때만)
	// Transform 변경은 배열 재할당을 일으키지 않으므로 break 불필요
	if (!bIsRoot)
	{
		for (int32 i = 0; i < (int32)Props.size(); ++i)
		{
			if (IsTransformProp(Props[i].Name))
				RenderPropertyWidget(Props, i);
		}
		ImGui::Separator();
	}

	// Pass 2: 나머지 프로퍼티
	// StaticMeshRef 변경은 컴포넌트 내부의 머티리얼 슬롯 기반 디스크립터를 다시 만들 수 있고, 그 과정에서
	// Props 배열 내용이 재할당되면서 현재 이후 항목의 Props[i].ValuePtr가 무효화될 수 있다. 이 경우에는
	// 즉시 현재 렌더 패스를 중단하고, 다음 프레임에 안정적인 디스크립터 목록을 다시 구성하도록 한다.
	for (int32 i = 0; i < (int32)Props.size(); ++i)
	{
		if (IsTransformProp(Props[i].Name))
			continue;

		bool bChanged = RenderPropertyWidget(Props, i);
		if (bChanged && Props[i].Type == EPropertyType::StaticMeshRef)
			break;
	}

	// 프로퍼티 직접 편집 후 월드 행렬 갱신
	if (SelectedComponent->IsA<USceneComponent>())
	{
		static_cast<USceneComponent*>(SelectedComponent)->MarkTransformDirty();
	}
}

bool FEditorPropertyWidget::RenderPropertyWidget(TArray<FPropertyDescriptor>& Props, int32& Index)
{
	ImGui::PushID(Index);
	FPropertyDescriptor& Prop = Props[Index];
	bool bChanged = false;

	switch (Prop.Type)
	{
	case EPropertyType::Bool:
	{
		bool* Val = static_cast<bool*>(Prop.ValuePtr);
		bChanged = ImGui::Checkbox(Prop.Name.c_str(), Val);
		break;
	}
	case EPropertyType::ByteBool:
	{
		uint8* Val = static_cast<uint8*>(Prop.ValuePtr);
		bool bVal = (*Val != 0);
		if (ImGui::Checkbox(Prop.Name.c_str(), &bVal))
		{
			*Val = bVal ? 1 : 0;
			bChanged = true;
		}
		break;
	}
	case EPropertyType::Int:
	{
		int32* Val = static_cast<int32*>(Prop.ValuePtr);
		bChanged = ImGui::DragInt(Prop.Name.c_str(), Val);
		break;
	}
	case EPropertyType::Float:
	{
		float* Val = static_cast<float*>(Prop.ValuePtr);
		if (Prop.Min != 0.0f || Prop.Max != 0.0f)
			bChanged = ImGui::DragFloat(Prop.Name.c_str(), Val, Prop.Speed, Prop.Min, Prop.Max);
		else
			bChanged = ImGui::DragFloat(Prop.Name.c_str(), Val, Prop.Speed);
		break;
	}
	case EPropertyType::Vec3:
	{
		float* Val = static_cast<float*>(Prop.ValuePtr);
		bChanged = ImGui::DragFloat3(Prop.Name.c_str(), Val, Prop.Speed);
		break;
	}
	case EPropertyType::Rotator:
	{
		// FRotator 메모리 레이아웃 [Pitch,Yaw,Roll] → UI X=Roll(X축), Y=Pitch(Y축), Z=Yaw(Z축)
		FRotator* Rot = static_cast<FRotator*>(Prop.ValuePtr);
		float RotXYZ[3] = { Rot->Roll, Rot->Pitch, Rot->Yaw };
		bChanged = ImGui::DragFloat3(Prop.Name.c_str(), RotXYZ, Prop.Speed);
		if (bChanged)
		{
			Rot->Roll = RotXYZ[0];
			Rot->Pitch = RotXYZ[1];
			Rot->Yaw = RotXYZ[2];
			if (SelectedComponent && SelectedComponent->IsA<USceneComponent>())
			{
				static_cast<USceneComponent*>(SelectedComponent)->ApplyCachedEditRotator();
			}
		}
		break;
	}
	case EPropertyType::Vec4:
	{
		float* Val = static_cast<float*>(Prop.ValuePtr);
		bChanged = ImGui::DragFloat4(Prop.Name.c_str(), Val, Prop.Speed);
		break;
	}
	case EPropertyType::Color4:
	{
		float* Val = static_cast<float*>(Prop.ValuePtr);
		bChanged = ImGui::ColorEdit4(Prop.Name.c_str(), Val);
		break;
	}
	case EPropertyType::String:
	{
		FString* Val = static_cast<FString*>(Prop.ValuePtr);
		char Buf[256];
		strncpy_s(Buf, sizeof(Buf), Val->c_str(), _TRUNCATE);
		if (ImGui::InputText(Prop.Name.c_str(), Buf, sizeof(Buf)))
		{
			*Val = Buf;
			bChanged = true;
		}
		break;
	}
	case EPropertyType::SceneComponentRef:
	{
		FString* Val = static_cast<FString*>(Prop.ValuePtr);
		UMovementComponent* MovementComp = SelectedComponent ? Cast<UMovementComponent>(SelectedComponent) : nullptr;
		FString Preview = MovementComp ? MovementComp->GetUpdatedComponentDisplayName() : FString("None");

		if (ImGui::BeginCombo(Prop.Name.c_str(), Preview.c_str()))
		{
			bool bSelectedAuto = Val->empty();
			if (ImGui::Selectable("Auto (Root)", bSelectedAuto))
			{
				Val->clear();
				bChanged = true;
			}
			if (bSelectedAuto)
			{
				ImGui::SetItemDefaultFocus();
			}

			if (MovementComp)
			{
				for (USceneComponent* Candidate : MovementComp->GetOwnerSceneComponents())
				{
					if (!Candidate)
					{
						continue;
					}

					FString CandidatePath = MovementComp->BuildUpdatedComponentPath(Candidate);
					FString CandidateName = Candidate->GetFName().ToString();
					if (CandidateName.empty())
					{
						CandidateName = Candidate->GetClass()->GetName();
					}
					if (!CandidatePath.empty())
					{
						CandidateName += " (" + CandidatePath + ")";
					}

					bool bSelected = (*Val == CandidatePath);
					if (ImGui::Selectable(CandidateName.c_str(), bSelected))
					{
						*Val = CandidatePath;
						bChanged = true;
					}
					if (bSelected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
			}

			ImGui::EndCombo();
		}
		break;
	}
	case EPropertyType::StaticMeshRef:
	{
		FString* Val = static_cast<FString*>(Prop.ValuePtr);

		FString Preview = Val->empty() ? "None" : FUIStringHelpers::GetStemFromPath(*Val);
		if (*Val == "None") Preview = "None";

		float ButtonWidth = ImGui::CalcTextSize("Import OBJ").x + ImGui::GetStyle().FramePadding.x * 2.0f;
		FEditorUIHelpers::DrawPropertyLabel(Prop.Name.c_str());
		FEditorUIHelpers::SetNextPropertyFieldWidth(ButtonWidth);

		if (ImGui::BeginCombo("##Value", Preview.c_str()))
		{
			bool bSelectedNone = (*Val == "None");
			if (ImGui::Selectable("None", bSelectedNone))
			{
				*Val = "None";
				bChanged = true;
			}
			if (bSelectedNone)
				ImGui::SetItemDefaultFocus();

			const TArray<FMeshAssetListItem>& MeshFiles = FObjManager::GetAvailableMeshFiles();
			for (const FMeshAssetListItem& Item : MeshFiles)
			{
				bool bSelected = (*Val == Item.FullPath);
				if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
				{
					*Val = Item.FullPath;
					bChanged = true;
				}
				if (bSelected)
					ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		// .obj 임포트 버튼
		ImGui::SameLine();
		if (ImGui::Button("Import OBJ"))
		{
			FString ObjPath = FEditorFileUtils::OpenFileDialog({
				.Filter = L"OBJ Files (*.obj)\0*.obj\0All Files (*.*)\0*.*\0",
				.Title = L"Import OBJ Mesh",
				.bFileMustExist = true,
				.bPathMustExist = true,
				.bPromptOverwrite = false,
				.bReturnRelativeToProjectRoot = true,
			});
			if (!ObjPath.empty())
			{
				ID3D11Device* Device = GEngine->GetRenderer().GetFD3DDevice().GetDevice();
				UStaticMesh* Loaded = FObjManager::LoadObjStaticMesh(ObjPath, Device);
				if (Loaded)
				{
					*Val = FObjManager::GetBinaryFilePath(ObjPath);
					bChanged = true;
				}
			}
		}
		break;
	}
	case EPropertyType::MaterialSlot:
	{
		FMaterialSlot* Slot = static_cast<FMaterialSlot*>(Prop.ValuePtr);
		int32          ElemIdx = (strncmp(Prop.Name.c_str(), "Element ", 8) == 0) ? atoi(&Prop.Name[8]) : -1;

		FString SlotName = "None";
		if (ElemIdx != -1 && SelectedComponent && SelectedComponent->IsA<UStaticMeshComponent>())
		{
			UStaticMeshComponent* SMC = static_cast<UStaticMeshComponent*>(SelectedComponent);
			if (SMC->GetStaticMesh() && ElemIdx < (int32)SMC->GetStaticMesh()->GetStaticMaterials().size())
				SlotName = SMC->GetStaticMesh()->GetStaticMaterials()[ElemIdx].MaterialSlotName;
		}

		// 좌측: Element 인덱스 + 슬롯 이름
		ImGui::BeginGroup();
		ImGui::Text("Element %d", ElemIdx);
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.5f, 0.5f, 0.5f, 1.0f));
		ImGui::TextUnformatted(SlotName.c_str());
		ImGui::PopStyleColor();
		if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", SlotName.c_str());
		ImGui::EndGroup();

		ImGui::SameLine(FEditorUIHelpers::PropertyLabelWidth);

		// 우측: Material 콤보
		ImGui::BeginGroup();
		ImGui::SetNextItemWidth(-1);

		FString Preview = (Slot->Path.empty() || Slot->Path == "None") ? "None" : FUIStringHelpers::GetStemFromPath(Slot->Path);
		if (ImGui::BeginCombo("##Value", Preview.c_str()))
		{
			// "None" 선택지 기본 제공
			bool bSelectedNone = (Slot->Path == "None" || Slot->Path.empty());
			if (ImGui::Selectable("None", bSelectedNone))
			{
				Slot->Path = "None";
				bChanged = true;
			}
			if (bSelectedNone) ImGui::SetItemDefaultFocus();

			// TObjectIterator 대신 FMaterialManager 파일 목록 스캔 데이터 사용
			const TArray<FMaterialAssetListItem>& MatFiles = FMaterialManager::Get().GetAvailableMaterialFiles();
			for (const FMaterialAssetListItem& Item : MatFiles)
			{
				bool bSelected = (Slot->Path == Item.FullPath);
				if (ImGui::Selectable(Item.DisplayName.c_str(), bSelected))
				{
					Slot->Path = Item.FullPath; // 데이터는 전체 경로로 저장
					bChanged = true;
				}
				if (bSelected) ImGui::SetItemDefaultFocus();
			}
			ImGui::EndCombo();
		}

		ImGui::EndGroup();
		break;
	}
	case EPropertyType::Name:
	{
		FName* Val = static_cast<FName*>(Prop.ValuePtr);
		FString Current = Val->ToString();

		// 리소스 키와 매칭되는 프로퍼티면 콤보 박스로 렌더링
		TArray<FString> Names;
		if (strcmp(Prop.Name.c_str(), "Font") == 0)
			Names = FResourceManager::Get().GetFontNames();
		else if (strcmp(Prop.Name.c_str(), "Particle") == 0)
			Names = FResourceManager::Get().GetParticleNames();
		else if (strcmp(Prop.Name.c_str(), "Texture") == 0)
			Names = FResourceManager::Get().GetTextureNames();

		if (!Names.empty())
		{
			if (ImGui::BeginCombo(Prop.Name.c_str(), Current.c_str()))
			{
				for (const auto& Name : Names)
				{
					bool bSelected = (Current == Name);
					if (ImGui::Selectable(Name.c_str(), bSelected))
					{
						*Val = FName(Name);
						bChanged = true;
					}
					if (bSelected)
						ImGui::SetItemDefaultFocus();
				}
				ImGui::EndCombo();
			}

			if (strcmp(Prop.Name.c_str(), "Particle") == 0)
			{
				const FParticleResource* Particle = FResourceManager::Get().FindParticle(*Val);
				if (Particle && Particle->IsLoaded())
				{
					ImGui::Spacing();
					const float AvailableWidth = ImGui::GetContentRegionAvail().x;
					const float PreviewSize = std::clamp(AvailableWidth, 64.0f, 192.0f);
					ImGui::Image(reinterpret_cast<ImTextureID>(Particle->SRV), ImVec2(PreviewSize, PreviewSize));
				}
			}
		}
		else
		{
			char Buf[256];
			strncpy_s(Buf, sizeof(Buf), Current.c_str(), _TRUNCATE);
			if (ImGui::InputText(Prop.Name.c_str(), Buf, sizeof(Buf)))
			{
				*Val = FName(Buf);
				bChanged = true;
			}
		}
		break;
	}
	}

	if (bChanged && SelectedComponent)
	{
		SelectedComponent->PostEditProperty(Prop.Name.c_str());
	}

	ImGui::PopID();
	return bChanged;
}
