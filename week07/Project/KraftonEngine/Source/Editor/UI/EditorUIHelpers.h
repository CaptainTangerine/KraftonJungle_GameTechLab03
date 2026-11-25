#pragma once

#include "ImGui/imgui.h"

#include <cstdio>

namespace FEditorUIHelpers
{
	inline constexpr float PropertyLabelWidth = 120.0f;

	// Two-column property-grid helpers use hidden labels, so callers can keep the
	// visible label separate while still providing a stable unique ImGui ID.
	inline const char* MakeHiddenLabel(const char* InLabel, char(&OutBuffer)[256])
	{
		snprintf(OutBuffer, sizeof(OutBuffer), "##%s", InLabel);
		return OutBuffer;
	}

	inline void DrawPropertyLabel(const char* InLabel, float InWidth = PropertyLabelWidth)
	{
		ImGui::AlignTextToFramePadding();
		ImGui::TextUnformatted(InLabel);
		ImGui::SameLine(InWidth);
	}

	inline void SetNextPropertyFieldWidth(float InTrailingReservedWidth = 0.0f)
	{
		const float TotalReservedWidth = InTrailingReservedWidth > 0.0f
			? InTrailingReservedWidth + ImGui::GetStyle().ItemSpacing.x
			: 0.0f;
		ImGui::SetNextItemWidth(-TotalReservedWidth);
	}

	inline bool DrawCheckboxProperty(const char* InLabel, bool* bInOutValue, float InWidth = PropertyLabelWidth)
	{
		char HiddenLabel[256];
		DrawPropertyLabel(InLabel, InWidth);
		return ImGui::Checkbox(MakeHiddenLabel(InLabel, HiddenLabel), bInOutValue);
	}

	inline bool DrawFloatProperty(
		const char* InLabel,
		float* InOutValue,
		float InSpeed = 0.1f,
		float InMin = 0.0f,
		float InMax = 0.0f,
		bool bUseRange = false,
		float InWidth = PropertyLabelWidth)
	{
		char HiddenLabel[256];
		DrawPropertyLabel(InLabel, InWidth);
		SetNextPropertyFieldWidth();
		return bUseRange
			? ImGui::DragFloat(MakeHiddenLabel(InLabel, HiddenLabel), InOutValue, InSpeed, InMin, InMax)
			: ImGui::DragFloat(MakeHiddenLabel(InLabel, HiddenLabel), InOutValue, InSpeed);
	}

	inline bool DrawVec3Control(const char* InLabel, float* InOutValues, float InSpeed = 0.1f, float InWidth = PropertyLabelWidth)
	{
		char HiddenLabel[256];
		DrawPropertyLabel(InLabel, InWidth);
		SetNextPropertyFieldWidth();
		return ImGui::DragFloat3(MakeHiddenLabel(InLabel, HiddenLabel), InOutValues, InSpeed);
	}

	inline bool DrawTextInputProperty(
		const char* InLabel,
		char* InOutBuffer,
		size_t InBufferSize,
		float InWidth = PropertyLabelWidth)
	{
		char HiddenLabel[256];
		DrawPropertyLabel(InLabel, InWidth);
		SetNextPropertyFieldWidth();
		return ImGui::InputText(MakeHiddenLabel(InLabel, HiddenLabel), InOutBuffer, InBufferSize);
	}

	// Inline helpers preserve the default ImGui row layout used by actor details.
	inline bool DrawInlineVec3Control(const char* InLabel, float* InOutValues, float InSpeed = 0.1f)
	{
		return ImGui::DragFloat3(InLabel, InOutValues, InSpeed);
	}

	inline bool DrawInlineCheckboxProperty(const char* InLabel, bool* bInOutValue)
	{
		return ImGui::Checkbox(InLabel, bInOutValue);
	}
}
