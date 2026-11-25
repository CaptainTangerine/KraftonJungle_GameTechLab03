#include "Editor/UI/EditorTheme.h"

#include "ImGui/imgui.h"

namespace FEditorTheme
{
	void ConfigureIO(ImGuiIO& InOutIO)
	{
		InOutIO.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		InOutIO.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
		InOutIO.MouseDrawCursor = false;
	}

	void ApplyEditorStyle(ImGuiStyle& InOutStyle)
	{
		InOutStyle.WindowRounding = 6.0f;
		InOutStyle.FrameRounding = 6.0f;
		InOutStyle.GrabRounding = 6.0f;
		InOutStyle.PopupRounding = 6.0f;
		InOutStyle.TabRounding = 6.0f;
		InOutStyle.ScrollbarRounding = 6.0f;
		InOutStyle.WindowBorderSize = 1.0f;
		InOutStyle.FrameBorderSize = 0.0f;

		InOutStyle.Colors[ImGuiCol_Text] = ImVec4(0.93f, 0.94f, 0.96f, 1.0f);
		InOutStyle.Colors[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.56f, 0.62f, 1.0f);
		InOutStyle.Colors[ImGuiCol_WindowBg] = ImVec4(0.08f, 0.09f, 0.11f, 1.0f);
		InOutStyle.Colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.11f, 0.14f, 1.0f);
		InOutStyle.Colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.13f, 0.98f);
		InOutStyle.Colors[ImGuiCol_Border] = ImVec4(0.20f, 0.23f, 0.27f, 1.0f);
		InOutStyle.Colors[ImGuiCol_FrameBg] = ImVec4(0.17f, 0.19f, 0.22f, 1.0f);
		InOutStyle.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.21f, 0.24f, 0.29f, 1.0f);
		InOutStyle.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.16f, 0.30f, 0.53f, 1.0f);
		InOutStyle.Colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.10f, 0.12f, 1.0f);
		InOutStyle.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.10f, 0.12f, 0.15f, 1.0f);
		InOutStyle.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.11f, 0.12f, 0.15f, 1.0f);
		InOutStyle.Colors[ImGuiCol_Button] = ImVec4(0.20f, 0.22f, 0.26f, 0.95f);
		InOutStyle.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.25f, 0.29f, 0.35f, 1.0f);
		InOutStyle.Colors[ImGuiCol_ButtonActive] = ImVec4(0.16f, 0.30f, 0.53f, 1.0f);
		InOutStyle.Colors[ImGuiCol_Header] = ImVec4(0.19f, 0.22f, 0.27f, 1.0f);
		InOutStyle.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.23f, 0.28f, 0.35f, 1.0f);
		InOutStyle.Colors[ImGuiCol_HeaderActive] = ImVec4(0.16f, 0.30f, 0.53f, 1.0f);
		InOutStyle.Colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.16f, 0.20f, 1.0f);
		InOutStyle.Colors[ImGuiCol_TabHovered] = ImVec4(0.22f, 0.26f, 0.33f, 1.0f);
		InOutStyle.Colors[ImGuiCol_TabActive] = ImVec4(0.16f, 0.30f, 0.53f, 1.0f);
		InOutStyle.Colors[ImGuiCol_CheckMark] = ImVec4(0.32f, 0.61f, 0.93f, 1.0f);
		InOutStyle.Colors[ImGuiCol_SliderGrab] = ImVec4(0.32f, 0.61f, 0.93f, 1.0f);
		InOutStyle.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.39f, 0.69f, 0.97f, 1.0f);
	}

	void ApplyFonts(ImGuiIO& InOutIO)
	{
		InOutIO.Fonts->AddFontFromFileTTF(
			"C:\\Windows\\Fonts\\malgun.ttf",
			16.0f,
			nullptr,
			InOutIO.Fonts->GetGlyphRangesKorean());
	}
}
