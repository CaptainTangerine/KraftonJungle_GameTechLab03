#pragma once

struct ImGuiIO;
struct ImGuiStyle;

namespace FEditorTheme
{
	void ConfigureIO(ImGuiIO& InOutIO);
	void ApplyEditorStyle(ImGuiStyle& InOutStyle);
	void ApplyFonts(ImGuiIO& InOutIO);
}
